#!/bin/bash

rm -rf cscope.files cscope.files

find . \( -name '*.c' -o -name '*.cpp' -o -name '*.cc' -o -name '*.s' -o -name '*.S' -o -name '*.h' \) -print > cscope.files
cscope -i cscope.files
