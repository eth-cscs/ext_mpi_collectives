#!/bin/bash

for i in `seq 1 10`
do

srun -N 1 --ntasks-per-node=4 all_reduce_perf -t 1 -g 1 -b 8 -e 67108864 -f 2  | tee santis_gpu_1node_4tasks_nccl_"$i".txt

done

for i in `seq 1 10`
do

srun -N 4 --ntasks-per-node=4 all_reduce_perf -t 1 -g 1 -b 8 -e 67108864 -f 2  | tee santis_gpu_4nodes_4tasks_nccl_"$i".txt

done
