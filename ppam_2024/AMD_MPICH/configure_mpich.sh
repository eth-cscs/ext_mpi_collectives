#!/bin/bash

# mpich-4.2.2

export CC=cc
export CXX=CC
export FC=ftn
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_mpich --disable-fortran --disable-cxx

make -j 32 install
