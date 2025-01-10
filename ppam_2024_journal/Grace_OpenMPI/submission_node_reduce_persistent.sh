#!/bin/bash

# salloc --nodes=1 --ntasks-per-node=288 --account=csstaff --constraint=gpu --partition=normal

for i in $(seq 1 10);
do
  /capstor/scratch/cscs/ajocksch/install_openmpi_todi/bin/mpirun --hostfile my_hostfile -n 288 --bind-to CORE ./osu_reduce_persistent -m 8:67108864 -T mpi_float -f | tee santis_reduce_openmpi_persistent_288_$i.txt
done
