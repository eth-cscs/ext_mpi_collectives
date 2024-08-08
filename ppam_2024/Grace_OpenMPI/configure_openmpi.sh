#!/bin/bash

./configure CC=gcc CXX=g++ CFLAGS='-I/user-environment/env/icon/include' LDFLAGS='-L/user-environment/env/icon/lib64' --prefix='/capstor/scratch/cscs/ajocksch/install_openmpi_todi'

make -j 32 install

