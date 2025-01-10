#!/bin/bash

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast -m 8:67108864 -T mpi_float -f | tee santis_bcast_mpich_72_$i.txt
done
