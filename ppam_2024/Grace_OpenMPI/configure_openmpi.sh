#!/bin/bash

uenv image repo # if you never used uenv before
uenv image find
uenv image pull prgenv-gnu/24.7
uenv start --view=modules prgenv-gnu/24.7

export PATH=$PATH:/user-environment/linux-sles15-neoverse_v2/gcc-13.2.0/automake-1.16.5-6vkf2u3eefhpgcg3c3kltikemtqnlvb7/bin/automake

./configure CC=gcc CXX=g++ CFLAGS='-I/user-environment/env/icon/include' LDFLAGS='-L/user-environment/env/icon/lib64' --prefix='/capstor/scratch/cscs/ajocksch/install_openmpi_todi'

make -j 32 install

