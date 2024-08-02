#!/bin/bash

nlines=`cat $1 | sed '/^[0-9]/!d' | wc -l`
for line in `seq 1 $nlines`
do
  rm temp.txt
  for file in $@
  do
    cat $file | sed '/^[0-9]/!d' | sed "$line q;d" >> temp.txt
  done
  cat temp.txt | awk -F " " '{if (NR == 1) {min[0]=1e10; min[1]=1e10;}; min[0]=(min[0]<$1)?min[0]:$1; min[1]=(min[1]<$2)?min[1]:$2;} END {print min[0],min[1]}'
done
