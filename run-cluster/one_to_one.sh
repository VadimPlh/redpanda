#!/bin/bash

RPK_PATH="../vbuild/go/linux/bin/rpk"
DIR_PATH="./test"
SCRIPTS_DIR_PATH="./scripts"
DATA_DIR="./cluster/redpanda-node-0/data/kafka/one_to_one"
producer="./kafka_2.13-2.8.0/bin/kafka-producer-perf-test.sh"

SCRIPTS_COUNT=100

MESSAGES_COUNT=2000000000
MESSAGES_SIZE=520

rm -rf $SCRIPTS_DIR_PATH
mkdir $SCRIPTS_DIR_PATH

for i in {1..100}
do
    rm -rf $DIR_PATH
    mkdir $DIR_PATH

    $RPK_PATH wasm generate $DIR_PATH

    template=`cat template_code.js`

    new_code=${template//input/one_to_one_${i}}

    echo "$new_code" > $DIR_PATH/src/main.js

    cd $DIR_PATH
    npm install
    npm run build
    cd ..

    cp $DIR_PATH/dist/main.js $SCRIPTS_DIR_PATH/script_${i}.js

    # Deploy
    $RPK_PATH topic create one_to_one_${i} -p 32 -r 3
    $RPK_PATH wasm deploy --name "one_to_one_${i}" $SCRIPTS_DIR_PATH/script_${i}.js
done

for i in {1..100}
do

    $producer --topic "one_to_one_${i}" --payload-file message.txt --producer-props asks=-1   bootstrap.servers=127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 --throughput -1 --num-records $MESSAGES_COUNT

    flag=0
    final_size=`du -s ${DATA_DIR}_${i} | awk '{ print $1 }'`
    for j in {1..10}
    do
        name="${DATA_DIR}_${i}."'$output$'
        result_size=`du -s ${name} | awk '{ print $1 }'`
        if [ "$final_size" == "$result_size" ]
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
    $RPK_PATH topic consume one_to_one_${i}.\$output\$ &> rpk_log.txt &
    sleep 3
    pids=`ps aux | grep rpk | awk '{print $2}'`
    sudo kill -9 $pids
    count=`grep "qwertyuiopasdfghjklzxcvbnm" rpk_log.txt -c`
    if ! [ "$count" == "0" ]
    then
        echo "FAILFAILFAILFAILFAILFAIL"
    fi
done




