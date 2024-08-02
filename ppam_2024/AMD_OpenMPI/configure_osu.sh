#!/bin/bash

# osu-micro-benchmarks-7.4

export CC=/capstor/scratch/cscs/ajocksch/install_openmpi/bin/mpicc
export CXX=/capstor/scratch/cscs/ajocksch/install_openmpi/bin/mpicxx
export FC=/capstor/scratch/cscs/ajocksch/install_openmpi/bin/mpiftn
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu_openmpi

make -j 32 install
