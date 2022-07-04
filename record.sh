#!/bin/bash

DIR=cpu-util
EXEC=mig-test

core_util=`mpstat -P 0-6 1 1`
ps_util=`ps -eo pcpu,pid,user,args | grep ./$EXEC | grep -v grep`
ps_util=`echo $ps_util | cut -d ' ' -f1`

if [ -z "$ps_util" ]; then
	exit 0
fi

# echo $ps_util >> $DIR/ps_util.csv

i=0
j=0
core=0
for per_core_util in $core_util
do
	if [ $i -lt 20 ]; then 
		i=$((i+1))
		continue
	fi

	if [ $j -eq 0 ] || [ $j -eq 13 ]; then
		if [ $j -eq 13 ]; then 
			echo "" >> $DIR/core$core.csv
			j=0
			core=$((core+1))
		fi
		if [ $core -eq 16 ]; then
			break
		fi
		echo -n $per_core_util >> $DIR/core$core.csv
	elif [ $j -eq 3 ] || [ $j -eq 5 ] || [ $j -eq 6 ] || [ $j -eq 7 ] || [ $j -eq 8 ] || [ $j -eq 12 ]; then
		echo -n ", $per_core_util" >> $DIR/core$core.csv
	fi
	j=$((j+1))
done

