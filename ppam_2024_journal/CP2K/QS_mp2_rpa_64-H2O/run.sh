#!/bin/bash

#SBATCH -N 1
#SBATCH --ntasks-per-node=16
#SPATCH --cpus-per-task=16
#SBATCH --partition=debug
##SBATCH --exclude=nid005340

export EXT_MPI_VERBOSE=1
export MPICH_GPU_SUPPORT_ENABLED=1
#srun --reservation=icon -u -N 1 --ntasks-per-node=4 ./executable.sh ./todi_gpu.sh osu_allreduce -d cuda
#export EXT_MPI_BLOCKING=1
#srun --reservation=icon -u -N 1 --ntasks-per-node=4 ./executable.sh ./todi_gpu.sh osu_allreduce -d cuda
#exit
export OMP_NUM_THREADS=16
#export LD_LIBRARY_PATH=/user-environment/env/._cp2k/irqwupqeblnrl7xwwky7jkosffnxrxew/lib64:$LD_LIBRARY_PATH
#export LD_PRELOAD=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib/libext_mpi_collectives.so:/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib/libmpifort_my.so:/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib/libmpi_my.so
#srun -N 1 --ntasks-per-node=1 cp ext_mpi_*_blocking_*.txt ext_mpi_*_blocking_*.dat /dev/shm
srun -N 1 --ntasks-per-node=1 cp ext_mpi_*_blocking_*.txt /dev/shm
srun -u --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose ./mps_wrapper.sh ./executable.sh /user-environment/env/cp2k/bin/cp2k.psmp -i H2O-64-RI-dRPA-TZ.inp -o hhh.out
export EXT_MPI_BLOCKING=1
srun -u --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose ./mps_wrapper.sh ./executable.sh /user-environment/env/cp2k/bin/cp2k.psmp -i H2O-64-RI-dRPA-TZ.inp -o hhh.out
srun -u --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose ./mps_wrapper.sh ./executable.sh /user-environment/env/cp2k/bin/cp2k.psmp -i H2O-64-RI-dRPA-TZ.inp -o hhh.out
srun -N 1 --ntasks-per-node=1 cp /dev/shm/ext_mpi_*_blocking_*.dat .
