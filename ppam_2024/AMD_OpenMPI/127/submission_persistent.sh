#!/bin/bash

# salloc --nodes=1 --ntasks-per-node=127 --account=csstaff --constraint=mc --partition=normal
# create hostfile

for i in $(seq 1 10);
do
  /capstor/scratch/cscs/ajocksch/install_openmpi/bin/mpirun --hostfile hosts.txt -n 127 --bind-to CORE /capstor/scratch/cscs/ajocksch/install_osu_openmpi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_openmpi_persistent_127_$i.txt
done
