#!/bin/bash
UTIL=util-stats.sh
DIR=$2
LABEL=$1

if [ $# -ne 2 ]; then
	echo "script need 2 parameter, <result dir move to this dir> <result store directory name>"
	exit
fi
mkdir $DIR


echo 3 > /proc/sys/vm/drop_caches


for i in $(seq 0 15)
do
	cpufreq-set -c $i -d 2301000
done


LD_PRELOAD=/home/sshyun/dma-test/use-uffd/libhemem.so numactl -N0 -m0 -- ./mig-test


for i in $(seq 0 15)
do
	cpufreq-set -c $i -d 1000000
done

cp mig_up_time.csv $DIR/mig_up_time.csv
cp mig_down_time.csv $DIR/mig_down_time.csv
cp per_core_util.csv $DIR/per_core_util.csv

mv $DIR $LABEL/$DIR
