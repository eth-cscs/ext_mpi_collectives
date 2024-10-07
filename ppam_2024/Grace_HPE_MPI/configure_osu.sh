#!/bin/bash

# osu-micro-benchmarks-7.4

export CC=mpicc
export CXX=mpicxx
export FC=mpif90
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu_grace_hpe_mpi

make -j 32 install
