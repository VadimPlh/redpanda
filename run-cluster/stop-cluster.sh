#!/usr/bin/env bash

pids=`ps aux | grep redpanda | awk '{print $2}'`
sudo kill -9 $pids
