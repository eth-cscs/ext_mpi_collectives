#!/bin/bash

# osu-micro-benchmarks-7.4

export CC=cc
export CXX=CC
export FC=ftn
export LDFLAGS=-L/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib
export LIBS=-lext_mpi_collectives
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu_hpe_mpi_ext_mpi

make -j 32 install
