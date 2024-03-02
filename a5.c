#include "a5-pthread.h"
#include "a5.h"

static const char ping_request[] = "GET /ping HTTP/1.1\r\n\r\n";
static const char echo_request[] = "GET /echo HTTP/1.1\r\n";
static const char write_request[] = "POST /write HTTP/1.1\r\n";
static const char read_request[] = "GET /read HTTP/1.1\r\n";
static const char file_request[] = "GET /%s HTTP/1.1\r\n";
static const char stats_request[] = "GET /stats HTTP/1.1\r\n";

static const char stats_response_body[] = "Requests: %d\nHeader bytes: %d\nBody bytes: %d\nErrors: %d\nError bytes: %d";

static const char ok200_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n";
static const char err404_response[] = "HTTP/1.1 404 Not Found";
static const char err400_response[] = "HTTP/1.1 400 Bad Request";

static const char content_len_header[] = "Content-Length: %d";

static char written[1024] = "<empty>";
static int written_size = 7;
static pthread_mutex_t write_lock;

static const char ping_header[] = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n";
static const char ping_body[] = "pong";

static int reqs = 0;
static int head_bytes = 0;
static int body_bytes = 0;
static int errs = 0;
static int err_bytes = 0;
static pthread_mutex_t stats_lock;

static pthread_t * ts;
static int * clients;
static int front = 0, rear = 0, n_threads = 0;
static pthread_mutex_t mutex;
static sem_t slots, items;


// Function declarations:
static void *handle_client_request(int client);
static void handle_ping(int client);
static void handle_echo(int client, char* request);
static void handle_write(int client, char* request);
static void handle_read(int client);
static void handle_stats(int client);
static void handle_file(int client, char* request);
static void send_request(int client, char* head, int head_size, char* body, int body_size);
static void send_error(int client, const char* error);
static void * consumer(void * arg);

//
// prepare_socket
//
// given a port number, starts up a socket and attaches an address to it
// returns the sockfd associated to that socket
//
static int prepare_socket(int p) {
    // Step 1: Creating a socket
    int port = htons(p);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket() failed");
        exit(1);
    }

    // Step 2: Binding port to an address
    static struct sockaddr_in sa;
    inet_pton(AF_INET, "127.0.0.1", &(sa.sin_addr));    // Calling API to fill in the data
    sa.sin_port = port;
    sa.sin_family = AF_INET;

    // Step 2a: This code to prevent server crashing when receiving data
    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));


    // Binding
    if (bind(sockfd, (struct sockaddr*) &sa, sizeof(sa) ) < 0) {
        perror("bind() failed");
        exit(1);
    }

    // Step 3: Setting up server to listen for joining clients
    // Backlog = , we want to accept 10 connections at max
    if (listen(sockfd, 10) < 0) {
        perror("listen(): failed");
        exit(1);
    }

    // Server socket is up and running, able to listen to upto 10 clients
    return sockfd;
}


//
// handle_client_request
//
// given a client, process the data just received from it
// the data is stored globally as data_from_client for easier access
//
static void *handle_client_request(int client) {
    // pthread_detach(pthread_self());
    // long arg = (long)c;
    // int client = (int)arg;
    char request[2048];
    int len = recv_http_request(client, request, sizeof(request), 0); // receiving data from client
    if (len == 0) {
        return NULL; // No request
    }
    assert(len > 0);  // Make sure len of data received is positive

    // Ping Request
    if ( !strncmp(request, ping_request, strlen(ping_request)) ) {
        handle_ping(client);
        close(client);
        return NULL;
    }
    // Echo Request
    else if ( !strncmp(request, echo_request, strlen(echo_request)) ) {
        handle_echo(client, request);
        close(client);
        return NULL;
    }
    // Write Request
    else if ( !strncmp(request, write_request, strlen(write_request)) ) {
        handle_write(client, request);
        close(client);
        return NULL;
    }
    // Read Request
    else if ( !strncmp(request, read_request, strlen(read_request)) ) {
        handle_read(client);
        close(client);
        return NULL;
    }
    // Stats Request
    else if ( !strncmp(request, stats_request, strlen(stats_request)) ) {
        handle_stats(client);
        close(client);
        return NULL;
    }
    // file request
    else if (!strncmp(request, "GET ", 4)) {
        // Assuming its a file request
        handle_file(client, request);
        // close(client);
        return NULL;
    }

    // Invalid Request
    send_error(client, err400_response);
    close(client);
    return NULL;
}

//
// handle_ping
//
// is a response to /ping command, sends a pong back to the client
//
static void handle_ping(int client) {
    char head[1024];
    int head_size = 0;
    char body[1024];
    int body_size = 0;


    // Creating Header
    head_size = strlen(ping_header); 
    memcpy(head, ping_header, head_size);

    // Creating Body
    body_size = strlen(ping_body);
    memcpy(body, ping_body, body_size);

    // Sending head and body separately
    send_request(client, head, head_size, body, body_size);

}

//
// handle_echo
//
// is a response to /echo command
// sends the body of the request back to the client
//
static void handle_echo(int client, char* request) {
    char head[1024];
    int head_size = 0;
    char body[1024];
    int body_size = 0;


    // Finding the end of the request
    char* end = strstr(request, "\r\n\r\n");
    if (end == NULL) {
        end = request+1024;
    }

    assert(end != NULL);
    *end = '\0';    // We are at the last line first "\r", we are changing it to be a NULL terminator

    // Finding the start of the request
    char* start = strstr(request, "\r\n");
    assert(start != NULL);
    start += 2;  // advancing it to the actual data from the header line

    // Creating body
    body_size = strlen(start);
    memcpy(body, start, body_size);

    // Creating header
    head_size = snprintf(head, 1024, ok200_response, body_size);

    // Sending head and body separately
    send_request(client, head, head_size, body, body_size);

}

//
// handle_write
//
// is a response to /write command
// Stores the data sent by the client
//
static void handle_write(int client, char* request) {

    static char* saveptr;
    // Checking and getting pointer to the Content Length header
    char* start = strstr(request, "\r\n\r\n");
    start += 4;  // Jump over newline characters
    assert(start != NULL);

    // Doing strtok_r() on client request to get to the string "Content Length: %d"
    char* token = strtok_r(request, "\r\n", &saveptr);
    assert(token != NULL);          // checking for first line
    token = strtok_r(NULL, "\r\n", &saveptr);   // Skipping first line

    int length = 0;
    while (token != NULL) {
        int res = sscanf(token, content_len_header, &length);
        if (res != 0) {
            break;  // found the content length header
        }

        token = strtok_r(NULL, "\r\n", &saveptr);
    }
    assert(length != 0);   // asserting found the header

    // Limiting longer data to 1024 bytes
    if (length > 1024) {
        length = 1024;
    }

    // Copying posted data to written_data, essentially saving it
    pthread_mutex_lock(&write_lock);
    written_size = length;
    memcpy(written, start, written_size);
    pthread_mutex_unlock(&write_lock);

    // Creating body to send later
    handle_read(client);

}

//
// handle_read
//
// is a response to /read command
// returns the data previously sent by the client
//
static void handle_read(int client) {
    char head[1024];
    int head_size = 0;
    char body[1024];
    int body_size = 0;

    // Creating body to send later
    pthread_mutex_lock(&write_lock);
    body_size = written_size;
    memcpy(body, written, body_size);
    pthread_mutex_unlock(&write_lock);

    // Creating head
    head_size = snprintf(head, sizeof(head), ok200_response, body_size);

    // Sending created data
    send_request(client, head, head_size, body, body_size);

}

//
// handle_stats
//
// is a response to /echo command
// returns the stats of the HTTP requests
//
static void handle_stats(int client) {
    char head[1024];
    int head_size = 0;
    char body[1024];
    int body_size = 0;

    pthread_mutex_lock(&stats_lock);
    // Creating a body
    body_size = snprintf(body, sizeof(body), stats_response_body,
        reqs, head_bytes, body_bytes, errs, err_bytes);
    pthread_mutex_unlock(&stats_lock);

    // Creating a head
    head_size = snprintf(head, sizeof(head), ok200_response, body_size);

    // Sending response
    send_request(client, head, head_size, body, body_size);

}

//
// handle_file
//
// is a response to GET /path command
// is a file request, returns the files contents
//
static void handle_file(int client, char* request) {
    char head[1024];
    int head_size = 0;
    char body[1024];
    int body_size = 0;

    // Figuring out the path to the file
    static char path[128];
    int found = sscanf(request, file_request, path);
    assert(found > 0);

    // Opening file
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        // File doesn't exist
        send_error(client, err404_response);
        close(client);
        return;
    }
    
    // Getting file size
    struct stat s;
    fstat(fd, &s);

    int file_size = s.st_size;

    // Creating header and sending it
    head_size = snprintf(head, sizeof(head), ok200_response, file_size);
    int sent = send_fully(client, head, head_size, 0);

    assert(sent == head_size);    // sent header in full
    pthread_mutex_lock(&stats_lock);
    head_bytes += head_size;
    pthread_mutex_unlock(&stats_lock);

    int file_read = 0;
    int file_sent = 0;
    int total_sent = 0;

    while (total_sent < file_size) {

        file_read = read(fd, body, sizeof(body));
        file_sent = 0;
        file_sent = send_fully(client, body, file_read, 0);

        // Short writes, in case full body isn't sent in one go
        while (file_sent != file_read) {
            file_sent += send_fully(client, body+file_sent, file_read-file_sent, 0);
        }

        assert(file_sent == file_read);  // we were able to send all data
    
        total_sent += file_sent;
        pthread_mutex_lock(&stats_lock);
        body_bytes += file_sent;
        pthread_mutex_unlock(&stats_lock);

    }

    pthread_mutex_lock(&stats_lock);
    reqs += 1;
    pthread_mutex_lock(&stats_lock);
    close(fd);  // Closing file descriptor
    close(client);

}


//
// send_request
//
// given a client, sends data to that client
//
static void send_request(int client, char* head, int head_size, char* body, int body_size) {

    int header_sent = send_fully(client, head, head_size, 0);
    int body_sent = 0;

    // Short writes, in case full body isn't sent in one go
    while (body_sent != body_size) {
        body_sent += send_fully(client, body+body_sent, body_size-body_sent, 0);
    }
    //printf("%s\n", body);
    assert(header_sent == head_size);
    assert(body_sent == body_size);

    pthread_mutex_lock(&stats_lock);
    reqs += 1;
    head_bytes += head_size;
    body_bytes += body_size;
    pthread_mutex_unlock(&stats_lock);

}

//
// send_error
//
// given a client, sends error response to that client
//
static void send_error(int client, const char* error) {

    int len = strlen(error);
    int sent = send_fully(client, error, len, 0);

    assert(sent == len);
    pthread_mutex_lock(&stats_lock);
    errs += 1;
    err_bytes += len;
    pthread_mutex_unlock(&stats_lock);

}

// ASSIGNMENT 4 ends here.
///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
// ASSIGNMENT 5 implmentation from here.

int create_server_socket(int port, int threads) {

    int server_socket = prepare_socket(port);

    // Initialize any global variable here
    if (pthread_mutex_init(&write_lock, NULL) != 0) {
        perror("Error creating mutex");
        exit(1);
    }
    if (pthread_mutex_init(&stats_lock, NULL) != 0) {
        perror("Error creating mutex");
        exit(1);
    }

    clients = malloc(threads*sizeof(int));
    ts = malloc(threads*sizeof(pthread_t));
    n_threads = threads;
    front = 0;
    rear = 0;

    if (pthread_mutex_init(&mutex, NULL) != 0) {
        perror("Error creating mutex");
        exit(1);
    }
    if (sem_init(&slots, 0, n_threads) != 0) {
        perror("Error creating semophore");
        exit(1);
    }
    if (sem_init(&items, 0, 0) != 0) {
        perror("Error creating semophore");
        exit(1);
    }

    // Tests 9 and 10: Create threads here
    for (int i = 0; i < n_threads; i++) {
        int ret = pthread_create(&ts[i], NULL, consumer, NULL);
        if (ret != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    return server_socket;

}

static void * consumer(void * arg) {
    while(1) {

        // wait for available client
        sem_wait(&items);

        // Grab the mutex lock to gaurantee mutual excluion
        pthread_mutex_lock(&mutex);

        // gets the available client
        int sockfd = clients[front];

        // update front
        front = (front + 1) % n_threads;

        pthread_mutex_unlock(&mutex);

        // available slot
        sem_post(&slots);

        handle_client_request(sockfd);

    }

    assert(0);
}

void accept_client(int server_socket) {
    //Producer
    static struct sockaddr_in client;
    static socklen_t client_size;

    memset(&client, 0, sizeof(client));
    memset(&client_size, 0, sizeof(client_size));

    int client_socket = accept(server_socket, (struct sockaddr *)&client, &client_size);
    if(client_socket < 0) {
        perror("Error on accept");
        exit(1);
    }

    // wait for available slot
    sem_wait(&slots);

    pthread_mutex_lock(&mutex);

    //save new client on shared buffer
    clients[rear] = client_socket;

    // update rear pointer
    rear = (rear + 1) % n_threads;

    pthread_mutex_unlock(&mutex);

    // announce new client available
    sem_post(&items);

    // pthread_t thread;
    // if (pthread_create(&thread, NULL, handle_client_request, (void*)(long)client_socket) != 0) {
    //     perror("pthread_create");
    //     exit(1);
    // }

    pthread_mutex_destroy(&write_lock);
    pthread_mutex_destroy(&stats_lock);

}