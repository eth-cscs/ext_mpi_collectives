#!/bin/bash

export EXT_MPI_VERBOSE=1
export EXT_MPI_COPYIN='6;-64 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  salloc --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_openmpi/bin/mpirun -n 128 --bind-to CORE ./osu_allreduce -m 8:67108864 -T mpi_float | tee pilatus_openmpi_128_$i.txt
done
