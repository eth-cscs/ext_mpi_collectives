#!/bin/bash

export EXT_MPI_VERBOSE=1
export EXT_MPI_COPYIN='6;-64 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal ./osu_bcast -m 8:67108864 -T mpi_float -f | tee pilatus_bcast_mpich_128_$i.txt
done
