#!/bin/bash

#SBATCH -N 4
#SBATCH --ntasks-per-node=16

for i in `seq 1 10`
do

EXT_MPI_COPYIN='6;1 -4 4' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun -N 1 --ntasks-per-node=4 ./todi_gpu.sh ./osu_allreduce -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_1node_4tasks_hpe_mpi_"$i".txt

done

for i in `seq 1 10`
do

EXT_MPI_COPYIN='6;1 -4 4' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun -N 4 --ntasks-per-node=4 ./todi_gpu.sh ./osu_allreduce -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_4nodes_4tasks_hpe_mpi_"$i".txt

done

for i in `seq 1 10`
do

EXT_MPI_COPYIN='6;1 -4 4' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun -N 1 --ntasks-per-node=4 ./todi_gpu.sh ./osu_allreduce_persistent -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_1node_4tasks_ext_mpi_"$i".txt

done

for i in `seq 1 10`
do

EXT_MPI_NUM_SOCKETS_PER_NODE=4 EXT_MPI_NUM_PORTS='4(-1 -1 1 1)' EXT_MPI_COPYIN='6;1 -4 4' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun -N 4 --ntasks-per-node=4 ./todi_gpu.sh ./osu_allreduce_persistent -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_4nodes_4tasks_ext_mpi_a_"$i".txt

done

for i in `seq 1 10`
do

EXT_MPI_NUM_SOCKETS_PER_NODE=4 EXT_MPI_NUM_PORTS='4(-1 -1 1 1)' EXT_MPI_COPYIN='7;1 -4 4' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun -N 4 --ntasks-per-node=4 ./todi_gpu.sh ./osu_allreduce_persistent -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_4nodes_4tasks_ext_mpi_b_"$i".txt

done

for i in `seq 1 10`
do

MPICH_GPU_SUPPORT_ENABLED=1 srun --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose -N 1 --ntasks-per-node=16 ./mps_wrapper.sh ./osu_allreduce -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_1node_16tasks_hpe_mpi_"$i".txt

done

for i in `seq 1 10`
do

EXT_MPI_COPYIN='6;1 -4 -4 4 4' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose -N 1 --ntasks-per-node=16 ./mps_wrapper.sh ./osu_allreduce_persistent -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_1node_16tasks_ext_mpi_a_"$i".txt

done

for i in `seq 1 10`
do

EXT_MPI_COPYIN='6;1 -16 16' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose -N 1 --ntasks-per-node=16 ./mps_wrapper.sh ./osu_allreduce_persistent -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_1node_16tasks_ext_mpi_b_"$i".txt

done

for i in `seq 1 10`
do

E_XT_MPI_NUM_SOCKETS_PER_NODE=4 EXT_MPI_NUM_PORTS='4(-3 3)' EXT_MPI_COPYIN='7;1 -16 16' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose -N 4 --ntasks-per-node=16 ./mps_wrapper.sh ./osu_allreduce -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_4nodes_16tasks_hpe_mpi_"$i".txt

done


for i in `seq 1 10`
do

E_XT_MPI_NUM_SOCKETS_PER_NODE=4 EXT_MPI_NUM_PORTS='4(-3 3)' EXT_MPI_COPYIN='7;1 -16 16' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose -N 4 --ntasks-per-node=16 ./mps_wrapper.sh ./osu_allreduce_persistent -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_4nodes_16tasks_ext_mpi_a_"$i".txt

done


for i in `seq 1 10`
do

EXT_MPI_NUM_SOCKETS_PER_NODE=4 EXT_MPI_NUM_PORTS='4(-1 -1 1 1)' EXT_MPI_COPYIN='6;1 -4 -4 4 4' EXT_MPI_VERBOSE=1 LD_LIBRARY_PATH=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib:$LD_LIBRARY_PATH MPICH_GPU_SUPPORT_ENABLED=1 srun --cpu-bind=mask_cpu:0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,0xffffffffffffffffff000000000000000000000000000000000000000000000000000000,verbose -N 4 --ntasks-per-node=16 ./mps_wrapper.sh ./osu_allreduce_persistent -d cuda -m 8:67108864 -T mpi_float -f | tee santis_gpu_4nodes_16tasks_ext_mpi_b_"$i".txt

done
