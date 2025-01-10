#!/bin/bash

export EXT_MPI_VERBOSE=1
export EXT_MPI_COPYIN='10;-64 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal ./executable.sh ./osu_reduce_persistent -m 8:67108864 -T mpi_float -f | tee pilatus_reduce_ext_mpi_128_a_$i.txt
done

export EXT_MPI_COPYIN='10;1 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal ./executable.sh ./osu_reduce_persistent -m 8:67108864 -T mpi_float -f | tee pilatus_reduce_ext_mpi_128_b_$i.txt
done
