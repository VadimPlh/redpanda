#!/bin/bash

RPK_PATH="../vbuild/go/linux/bin/rpk"
DIR_PATH="./test"
SCRIPTS_DIR_PATH="./scripts"

INPUT_TOPIC="input"
DATA_DIR="./cluster/redpanda-node-0/data/kafka"
OUTPUT_TOPIC="output_"

MESSAGES_COUNT=100000
MESSAGES_SIZE=26000

SCRIPTS_COUNT=10

rm -rf $SCRIPTS_DIR_PATH
mkdir $SCRIPTS_DIR_PATH

$RPK_PATH topic create ${INPUT_TOPIC}

for i in {1..5}
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

./rdkafka_perfomance -P -t "${INPUT_TOPIC}" -s ${MESSAGES_SIZE} -b 127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 -m "qwertyuiopasdfghjklzxcvbnm" -c ${MESSAGES_COUNT}

for i in {1..5}
do
    ./rdkafka_perfomance -C -t "${INPUT_TOPIC}.\$${OUTPUT_TOPIC}${i}\$"  -b 127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 -p 0_11 -c ${MESSAGES_COUNT}

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
