#!/bin/bash

#SBATCH --nodes=1
#SBATCH --ntasks-per-node=72
#SBATCH --account=csstaff
#SBATCH --constraint=gpu
#SBATCH --partition=normal

export LD_PRELOAD=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib/libext_mpi_collectives.so

export EXT_MPI_VERBOSE=1
export EXT_MPI_COPYIN='7;-64 -9 -8 8 9'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_a_$i.txt
done

export EXT_MPI_COPYIN='7;1 -9 -8 8 9'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_b_$i.txt
done

export EXT_MPI_COPYIN='7;-64 -72 72'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_c_$i.txt
done

export EXT_MPI_COPYIN='7;1 -72 72'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_d_$i.txt
done

export EXT_MPI_COPYIN='7;-256 -9 -8 8 9'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_e_$i.txt
done

export EXT_MPI_COPYIN='7;-1024 -9 -8 8 9'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_f_$i.txt
done

export EXT_MPI_COPYIN='7;-256 -72 72'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_g_$i.txt
done

export EXT_MPI_COPYIN='7;-1024 -72 72'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=72 --account=csstaff --constraint=gpu --partition=normal ./osu_bcast_persistent -m 8:67108864 -T mpi_float -f | tee santis_bcast_ext_mpi_72_h_$i.txt
done
