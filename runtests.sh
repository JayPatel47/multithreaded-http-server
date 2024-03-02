#!/bin/bash

EXEC=a5
DUMMY=test
OUTPUT=output
TEST_DIR=tests
TIMEOUT=1s
DIFF_FILE=d

PASSING=0

RED='\033[41;37m'
GREEN='\033[42m'
RESET='\033[0m'

export ASAN_OPTIONS="detect_leaks=false"

make clean && make all

echo "" > results

START=$(date +%s%N | cut -b1-13 | sed s/N/000/g)
for T in $(ls $TEST_DIR | grep -E "$1" | sort)
do
    THREADS="$TEST_DIR/$T/threads.txt"
    PASS=1
    for F in $(ls $TEST_DIR/$T | grep ".sh$" | sort)
    do
        echo -n -e "Running ${GREEN}positive test${RESET} $T/$F"
        echo -n $'\t'

        TESTFILE="$TEST_DIR/$T/$F"
        EXPECTED=$(sed 's/.sh$/.expected/g' <<<"$TESTFILE")

        killall -9 $EXEC &> /dev/null

        # Start server in background
        bash -c "echo \$\$ > pid ; exec ./$EXEC `cat $THREADS`" &

        T1=$(date +%s%N | cut -b1-13 | sed s/N/000/g)
        TRIES=0
        # Wait until server accepts requests on port
        until nc -z 127.0.0.1 `cat port.txt` ;
        do
            echo "$TRIES"
            ((TRIES++))
            sleep 0.1
            if [[ "$TRIES" -gt 10 ]]; then
                killall -9 $EXEC &> /dev/null
                exit 0
            fi
        done
        PID=`cat pid`

        T1=$(date +%s%N | cut -b1-13 | sed s/N/000/g)
        ./$TESTFILE `cat port.txt` &> $OUTPUT
        RET=$?
        T2=$(date +%s%N | cut -b1-13 | sed s/N/000/g)
        TT=$((T2-T1))

        # Kill server
        kill -9 $PID &> /dev/null
        wait $PID &> /dev/null

        if [ $RET -eq 0 ]
        then
            echo -e -n "${GREEN} RUN OK in ${TT}ms ${RESET}\t"
        else
            echo -n -e "Running ${GREEN}positive test${RESET} $T/$F"
            echo -n $'\t'
            echo -e "${RED} RUN FAIL in ${TT}ms ${RESET}"
            cat $OUTPUT
            PASS=0
            continue
        fi

         # Remove possible Windows \r from output and expected files
         sed 's/\r$//' -i $OUTPUT   &> /dev/null
         if [ -f "$EXPECTED" ]; then
             sed 's/\r$//' -i $EXPECTED &> /dev/null
             diff --text -y $EXPECTED $OUTPUT &> $DIFF_FILE
             DIFF=$?
             rm $DUMMY $OUTPUT &> /dev/null

             if [ $DIFF -eq 0 ]
             then
                 echo -e " ${GREEN} DIFF OK ${RESET}\t"
             else
                 echo -e " ${RED} DIFF FAIL ${RESET}"
                 cat $DIFF_FILE
             fi
         else
             DIFF=0
         fi



        if [ $RET -ne 0 ] || [ $DIFF -ne 0 ]
        then
            PASS=0
            #continue 2
        fi
    done

    if [ $PASS -eq 0 ]
    then
            echo -e "${RED}                   TEST ${T} FAILING                  ${RESET}"
            echo -e "${RED}                   TEST ${T} FAILING                  ${RESET}"
            echo -e "${RED}                   TEST ${T} FAILING                  ${RESET}"
    else
            echo -e "${GREEN}                   TEST ${T} PASSING                ${RESET}"
            echo -e "${GREEN}                   TEST ${T} PASSING                ${RESET}"
            echo -e "${GREEN}                   TEST ${T} PASSING                ${RESET}"
            echo -e "${GREEN}                   TEST ${T} PASSING                ${RESET}" >> results
    fi

    PASSING=$(($PASSING+$PASS))
done
END=$(date +%s%N | cut -b1-13 | sed s/N/000/g)
echo "Finished in $((END-START))ms"

killall -9 $EXEC &> /dev/null

echo $PASSING
