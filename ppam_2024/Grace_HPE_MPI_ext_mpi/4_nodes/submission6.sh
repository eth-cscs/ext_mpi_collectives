#!/bin/bash
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=288
#SBATCH --account=csstaff
#SBATCH --constraint=gpu
#SBATCH --time=03:00:00

export FI_MR_CACHE_MONITOR=memhooks
export EXT_MPI_VERBOSE=1
export EXT_MPI_NUM_SOCKETS_PER_NODE=4
export EXT_MPI_COPYIN='6;-64 -9 -8 -4 4 8 9'
export EXT_MPI_NUM_PORTS='4(1 1)'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank /capstor/scratch/cscs/ajocksch/install_osu_grace_hpe_mpi_ext_mpi/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee todi_hpe_mpi_ext_mpi_288_4nodes_6_$i.txt
done
