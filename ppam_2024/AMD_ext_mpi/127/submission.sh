#!/bin/bash

export EXT_MPI_VERBOSE=1
export EXT_MPI_COPYIN='5;-64 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=127 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_127_1_$i.txt
done

export EXT_MPI_COPYIN='5;1 -16 -4 -2 2 4 16'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=127 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_127_2_$i.txt
done

export EXT_MPI_COPYIN='5;1 -4 -4 -4 -2 2 4 4 4'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=127 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_127_3_$i.txt
done

export EXT_MPI_COPYIN='5;1 -2 -2 -2 -2 -2 -2 -2 2 2 2 2 2 2 2'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=127 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_ext_mpi_127_4_$i.txt
done
