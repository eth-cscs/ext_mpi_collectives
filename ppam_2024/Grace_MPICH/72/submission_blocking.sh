#!/bin/bash

for i in $(seq 1 10);
do
  srun --reservation=todi --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_mpich_todi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce -m 8:67108864 -T mpi_float | tee todi_mpich_72_$i.txt
done
