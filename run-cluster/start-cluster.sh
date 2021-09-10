#!/bin/bash

COUNT=1000;
SIZE=5000;
IPs='127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094';

NODE_PATH='/home/fedora/data/redpanda/build/node/output/modules/rpc/service.js'

cd ./cluster


# Start redpanda cluster
for dir in *
do
    cd $dir
    sh prepare.sh
    cd ..
done

for dir in *
do
    cd $dir
    rm -rf log.txt
    touch log.txt
    sh start.sh &> log.txt  &
    count="0"
    while true
    do
        count=`grep "Successfully started Redpanda" log.txt -c`
        if [ "$count" == "1" ]
        then
            break;
        fi
    done
    rm -rf node_log.txt
    node $NODE_PATH redpanda.yaml &> node_log.txt &
    cd ..
done
