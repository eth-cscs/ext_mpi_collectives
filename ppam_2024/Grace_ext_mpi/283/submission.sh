#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=283
#SBATCH --account=csstaff
#SBATCH --constraint=gpu
#SBATCH --partition=normal

export EXT_MPI_VERBOSE=1
export EXT_MPI_COPYIN='5;-64 -9 -8 -4 4 8 9'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=283 --account=csstaff --constraint=gpu --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi_todi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee todi_ext_mpi_283_1_$i.txt
done

export EXT_MPI_COPYIN='5;1 -9 -8 -4 4 8 9'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=283 --account=csstaff --constraint=gpu --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi_todi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee todi_ext_mpi_283_2_$i.txt
done

export EXT_MPI_COPYIN='5;1 -3 -3 -4 -2 -2 -2 2 2 2 4 3 3'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=283 --account=csstaff --constraint=gpu --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi_todi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee todi_ext_mpi_283_3_$i.txt
done

export EXT_MPI_COPYIN='5;1 -3 -3 -2 -2 -2 -2 -2 2 2 2 2 2 3 3'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank --mpi=pmi2 --nodes=1 --ntasks-per-node=283 --account=csstaff --constraint=gpu --partition=normal /capstor/scratch/cscs/ajocksch/install_osu_ext_mpi_todi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee todi_ext_mpi_283_4_$i.txt
done
