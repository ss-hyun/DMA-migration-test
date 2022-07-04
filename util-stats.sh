#!/bin/bash

DIR=cpu-util

echo '%cpu' > $DIR/ps_util.csv

for i in $(seq 0 6)
do
	echo 'time, %usr, %sys, %iowait, %irq, %soft, %idle' > $DIR/core$i.csv
done

while :
do
	./record.sh &
	sleep 1
done

