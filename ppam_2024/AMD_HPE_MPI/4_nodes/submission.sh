#!/bin/bash

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --nodes=4 --ntasks-per-node=128 --account=csstaff --constraint=mc --partition=debug /capstor/scratch/cscs/ajocksch/install_osu_hpe_mpi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce -m 8:67108864 -T mpi_float | tee eiger_hpe_mpi_128_4nodes_1_$i.txt
done
