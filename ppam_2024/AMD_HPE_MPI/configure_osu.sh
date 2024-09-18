#!/bin/bash

# osu-micro-benchmarks-7.4

export CC=cc
export CXX=CC
export FC=ftn
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_osu_hpe_mpi

make -j 32 install
