#!/bin/bash

nlines=`cat $1 | sed '/^[0-9]/!d' | wc -l`
for line in `seq 1 $nlines`
do
  rm temp.txt
  for file in $@
  do
    cat $file | sed '/^[0-9]/!d' | sed "$line q;d" >> temp.txt
  done
  cat temp.txt | awk -F " " '{sum[0]+=$1; sum[1]+=$2; sum[2]+=$3; sum[3]+=$4; sum[4]+=$5;} END {print sum[0]/NR,sum[1]/NR,sum[2]/NR,sum[3]/NR,sum[4]/NR}'
done
