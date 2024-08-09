#!/bin/bash

# salloc --reservation=todi --nodes=1 --ntasks-per-node=288 --account=csstaff --constraint=gpu --partition=normal
# create hostfile

for i in $(seq 1 10);
do
  /capstor/scratch/cscs/ajocksch/install_openmpi_todi/bin/mpirun --hostfile hosts.txt -n 288 --bind-to CORE /capstor/scratch/cscs/ajocksch/install_osu_openmpi_todi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee todi_openmpi_persistent_288_$i.txt
done
