#!/bin/bash

export EXT_MPI_VERBOSE=1
export EXT_MPI_COPYIN='6;-64 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  salloc --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_mpich/bin/mpirun -n 128 ./osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_128_1_$i.txt
done

export EXT_MPI_COPYIN='6;1 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  salloc --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_mpich/bin/mpirun -n 128 ./osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_128_2_$i.txt
done

export EXT_MPI_COPYIN='6;1 -4 -4 -4 -2 2 4 4 4'

for i in $(seq 1 10);
do
  salloc --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_mpich/bin/mpirun -n 128 ./osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_128_3_$i.txt
done

export EXT_MPI_COPYIN='6;1 -2 -2 -2 -2 -2 -2 -2 2 2 2 2 2 2 2'

for i in $(seq 1 10);
do
  salloc --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_mpich/bin/mpirun -n 128 ./osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_128_4_$i.txt
done
