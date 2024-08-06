#!/bin/bash

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=normal /capstor/scratch/cscs/ajocksch/install_osu/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee pilatus_mpich_persistent_128_$i.txt
done
