#!/bin/bash

# osu-micro-benchmarks-7.4

export CC=/capstor/scratch/cscs/ajocksch/install_mpich_todi/bin/mpicc
export CXX=/capstor/scratch/cscs/ajocksch/install_mpich_todi/bin/mpicxx
export FC=/capstor/scratch/cscs/ajocksch/install_mpich_todi/bin/mpiftn
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu_mpich_todi

make -j 32 install
