#!/bin/bash

pids=`ps aux | grep redpanda | awk '{print $2}'`
sudo kill -9 $pids
pids=`ps aux | grep perf-test | awk '{print $2}'`
sudo kill -9 $pids
pids=`ps aux | grep kafka | awk '{print $2}'`
sudo kill -9 $pids

. ./config.sh

RPK_PATH="../vbuild/go/linux/bin/rpk"

for (( i=0; i < ${COPROC_COUNT}; i++ ))
do
    $RPK_PATH wasm remove  "one_to_one_${i}"  --brokers ${BROKERS}
    $RPK_PATH topic delete one_to_one_${i}  --brokers ${BROKERS}
done
