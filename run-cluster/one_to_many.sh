#!/bin/bash

RPK_PATH="../vbuild/go/linux/bin/rpk"
DIR_PATH="./test"
SCRIPTS_DIR_PATH="./scripts"

INPUT_TOPIC="input"
DATA_DIR="./cluster/redpanda-node-0/data/kafka"
OUTPUT_TOPIC="output_"

producer="./kafka_2.13-3.0.0/bin/kafka-producer-perf-test.sh"

MESSAGES_COUNT=20000000
MESSAGES_SIZE=520

SCRIPTS_COUNT=10

rm -rf $SCRIPTS_DIR_PATH
mkdir $SCRIPTS_DIR_PATH

$RPK_PATH topic create ${INPUT_TOPIC} -p 32 -r 3

for i in {1..100}
do
    rm -rf $DIR_PATH
    mkdir $DIR_PATH

    $RPK_PATH wasm generate $DIR_PATH

    template=`cat ./template_code.js`

    new_code=${template//input/${INPUT_TOPIC}}
    new_code=${new_code//output/"${OUTPUT_TOPIC}${i}"}

    echo "$new_code" > $DIR_PATH/src/main.js

    cd $DIR_PATH
    npm install
    npm run build
    cd ..

    cp $DIR_PATH/dist/main.js $SCRIPTS_DIR_PATH/script_${i}.js

    # Deploy
    $RPK_PATH wasm deploy --name "${INPUT_TOPIC}_${i}" $SCRIPTS_DIR_PATH/script_${i}.js
done

$producer --topic "${INPUT_TOPIC}" --payload-file message.txt --producer-props asks=-1 bootstrap.servers=127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 --throughput -1 --num-records ${MESSAGES_COUNT}

for i in {1..100}
do
    flag=0
    input_result=`${RPK_PATH} topic describe ${INPUT_TOPIC} -p |  awk '{ print $7 }'`
    for j in {1..10}
    do
        name="${INPUT_TOPIC}."'$output'"_${i}"'$'
        result=`${RPK_PATH} topic describe ${name} -p |  awk '{ print $7 }'`
        result=${result//UNKNOWN_TOPIC_OR_PARTITION/0}

        if [ "$result" == "$input_result" ]
        then
            flag=1
            break
        fi
        sleep 1
    done

    if [ "$flag" == 0 ]
    then
        echo "FAILFAILFAILFAILFAILFAIL"
        exit 1
    fi

    rm -rf rpk_log.txt
    $RPK_PATH topic consume ${INPUT_TOPIC}.\$${OUTPUT_TOPIC}${i}\$ &> rpk_log.txt &
    sleep 2
    pids=`ps aux | grep rpk | awk '{print $2}'`
    sudo kill -9 $pids
    count=`grep "qwertyuiopasdfghjklzxcvbnm" rpk_log.txt -c`
    if ! [ "$count" == "0" ]
    then
        echo "FAILFAILFAILFAILFAILFAIL"
    fi
done
