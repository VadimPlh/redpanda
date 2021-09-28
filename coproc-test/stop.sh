#!/bin/bash

pids=`ps aux | grep redpanda | awk '{print $2}'`
sudo kill -9 $pids
pids=`ps aux | grep perf-test | awk '{print $2}'`
sudo kill -9 $pids
