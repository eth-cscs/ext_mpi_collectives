#!/bin/bash

cat $1 | awk 'END{print "file_input_max_raw="NR";"}'
echo "file_input_raw = (FileData *) malloc(file_input_max_raw*sizeof(*file_input_raw));"
cat $1 | awk '{print "file_input_raw["NR-1"].nnodes="$1";file_input_raw["NR-1"].nports="$2";file_input_raw["NR-1"].parallel="$3";file_input_raw["NR-1"].msize="$4";file_input_raw["NR-1"].deltaT="$5";"}'
