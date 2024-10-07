#!/bin/bash
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=128
#SBATCH --account=csstaff
#SBATCH --constraint=mc
#SBATCH --time=02:00:00

export FI_MR_CACHE_MONITOR=memhooks
export EXT_MPI_VERBOSE=1
export EXT_MPI_NUM_SOCKETS_PER_NODE=2
export EXT_MPI_COPYIN='6;1 -16 -4 -2 2 4 16'
export EXT_MPI_NUM_PORTS='4(-1 -1 1 1)'

for i in $(seq 1 10);
do
  srun --cpu_bind=rank /capstor/scratch/cscs/ajocksch/install_osu_hpe_mpi_ext_mpi/libexec/osu-micro-benchmarks/mpi/persistent/osu_allreduce_persistent -m 8:67108864 -T mpi_float | tee eiger_hpe_mpi_ext_mpi_128_4nodes_2_$i.txt
done
