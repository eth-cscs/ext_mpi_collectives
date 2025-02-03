#!/bin/bash

export LD_PRELOAD=/capstor/scratch/cscs/ajocksch/ext_mpi_collectives.git/lib/libext_mpi_collectives.so
"$@"
