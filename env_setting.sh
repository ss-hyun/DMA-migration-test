#!/bin/bash

ndctl create-namespace -f -e namespace0.0 --mode=devdax --align 2M
#ndctl create-namespace -f -e namespace1.0 --mode=devdax --align 2M

echo 1000000 > /proc/sys/vm/max_map_count
