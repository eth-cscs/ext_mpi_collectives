#!/bin/bash

for i in `seq 1 10`
do

#srun -N 1 --ntasks-per-node=4 all_reduce_perf -t 1 -g 1 -b 8 -e 67108864 -f 2  | tee santis_gpu_1node_4tasks_nccl_"$i".txt
srun --mpi=pmi2 --environment=/capstor/scratch/cscs/manitart/ce_images/nccl_tests_env.toml -n4 -N1 all_reduce_perf -t 1 -g 1 -b 8 -e 67108864 -f 2 | tee santis_gpu_1node_4tasks_nccl_"$i".txt

done

for i in `seq 1 10`
do

#srun -N 4 --ntasks-per-node=4 all_reduce_perf -t 1 -g 1 -b 8 -e 67108864 -f 2  | tee santis_gpu_4nodes_4tasks_nccl_"$i".txt
srun --mpi=pmi2 --environment=/capstor/scratch/cscs/manitart/ce_images/nccl_tests_env.toml -n16 -N4 all_reduce_perf -t 1 -g 1 -b 8 -e 67108864 -f 2 | tee santis_gpu_4nodes_4tasks_nccl_"$i".txt

done
