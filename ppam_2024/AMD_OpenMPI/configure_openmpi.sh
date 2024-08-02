#!/bin/bash

export CC=cc
export CXX=CC
export FC=ftn
./configure --prefix=/capstor/scratch/cscs/ajocksch/install_openmpi

make -j 32 install
