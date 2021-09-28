#!/bin/bash

. ./config.sh

RPK_PATH="../vbuild/go/linux/bin/rpk"
GENERATE_DIR_PATH="./test"
SCRIPTS_DIR_PATH="./scripts"

PRODUCER_DIR="./producer_log"
CONSUMER_DIR="./consumer_log"

producer="./kafka_2.12-3.0.0/bin/kafka-producer-perf-test.sh"
consumer="./kafka_2.12-3.0.0/bin/kafka-consumer-perf-test.sh"

rm -rf $SCRIPTS_DIR_PATH
mkdir $SCRIPTS_DIR_PATH

for (( i=0; i < ${COPROC_COUNT}; i++ ))
do
    rm -rf $GENERATE_DIR_PATH
    mkdir $GENERATE_DIR_PATH

    $RPK_PATH wasm generate $GENERATE_DIR_PATH

    template=`cat template_code.js`

    new_code=${template//input/one_to_one_${i}}

    echo "$new_code" > $GENERATE_DIR_PATH/src/main.js

    cd $GENERATE_DIR_PATH
    npm install
    npm run build
    cd ..

    cp $GENERATE_DIR_PATH/dist/main.js $SCRIPTS_DIR_PATH/script_${i}.js

    # Deploy
    $RPK_PATH topic create one_to_one_${i} -p 32 -r 3
    $RPK_PATH wasm deploy --name "one_to_one_${i}" $SCRIPTS_DIR_PATH/script_${i}.js
done

rm -rf ${PRODUCER_DIR}
mkdir ${PRODUCER_DIR}

for (( i=0; i < ${COPROC_COUNT}; i++ ))
do
    $producer --topic "one_to_one_${i}" --record-size ${RECORD_SIZE} --producer-props asks=-1   bootstrap.servers=127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 --throughput ${THROUGHPUT} --num-records $MESSAGES_COUNT &> ${PRODUCER_DIR}/producer_${i}.txt &
done

rm -rf ${CONSUMER_DIR}
mkdir ${CONSUMER_DIR}
for (( i=0; i < ${COPROC_COUNT}; i++ ))
do
    $consumer --topic "one_to_one_${i}" --bootstrap-server 127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 --messages ${MESSAGES_COUNT} &> ${CONSUMER_DIR}/consumer_${i}.txt &
done


