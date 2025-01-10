#!/bin/bash

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=288 --account=csstaff --constraint=gpu --partition=normal ./osu_reduce -m 8:67108864 -T mpi_float -f | tee santis_reduce_mpich_288_$i.txt
done
