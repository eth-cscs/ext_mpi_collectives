#!/bin/bash

./average.sh AMD_MPICH/128/pilatus_mpich_128_* > mpich_blocking.txt
./average.sh AMD_MPICH/128/pilatus_mpich_persistent_* > mpich_persistent.txt
./average.sh AMD_OpenMPI/128/pilatus_openmpi_128_* > openmpi_blocking.txt
./average.sh AMD_OpenMPI/128/pilatus_openmpi_persistent_* > openmpi_persistent.txt

./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_1_* > ext_mpi_1.txt
./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_2_* > ext_mpi_2.txt
./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_3_* > ext_mpi_3.txt
./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_4_* > ext_mpi_4.txt

./minimum.sh ext_mpi_1.txt ext_mpi_2.txt > ext_mpi.txt
