#!/bin/bash

# salloc --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal

for i in $(seq 1 10);
do
  /capstor/scratch/cscs/ajocksch/install_openmpi/bin/mpirun --hostfile my_hostfile -n 128 --bind-to CORE ./osu_bcast -m 8:67108864 -T mpi_float -f | tee pilatus_bcast_openmpi_128_$i.txt
done
