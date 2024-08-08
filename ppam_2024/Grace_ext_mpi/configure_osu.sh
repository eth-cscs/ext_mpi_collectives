#!/bin/bash

# osu-micro-benchmarks-7.4

export CC='/capstor/scratch/cscs/ajocksch/install_mpich_todi/bin/mpicc -L/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib -L/opt/cray/xpmem/default/lib64 -lext_mpi_collectives -ldl -lxpmem'
export CXX='/capstor/scratch/cscs/ajocksch/install_mpich_todi/bin/mpicxx -L/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib -L/opt/cray/xpmem/default/lib64 -lext_mpi_collectives -ldl -lxpmem'
export FC=/capstor/scratch/cscs/ajocksch/install_mpich_todi/bin/mpiftn
#export LDFLAGS=-L/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib
#export LIBS=-lext_mpi_collectives
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu_ext_mpi_todi

make -j 32 install
