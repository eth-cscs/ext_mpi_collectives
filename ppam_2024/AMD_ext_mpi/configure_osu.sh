#!/bin/bash

# osu-micro-benchmarks-7.4

export CC=/capstor/scratch/cscs/ajocksch/install_mpich/bin/mpicc
export CXX=/capstor/scratch/cscs/ajocksch/install_mpich/bin/mpicxx
export FC=/capstor/scratch/cscs/ajocksch/install_mpich/bin/mpiftn
export LDFLAGS=-L/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib
export LIBS=-lext_mpi_collectives
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu_ext_mpi

make -j 32 install
