#!/bin/bash

# osu-micro-benchmarks-7.4

export CC=/capstor/scratch/cscs/ajocksch/install_mpich/bin/mpicc
export CXX=/capstor/scratch/cscs/ajocksch/install_mpich/bin/mpicxx
export FC=/capstor/scratch/cscs/ajocksch/install_mpich/bin/mpiftn
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu

make -j 32 install
