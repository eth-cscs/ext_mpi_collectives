#!/bin/bash

./average.sh AMD_MPICH/128/pilatus_mpich_128_* > AMD_mpich_blocking.txt
./average.sh AMD_MPICH/128/pilatus_mpich_persistent_* > AMD_mpich_persistent.txt
./average.sh AMD_OpenMPI/128/pilatus_openmpi_128_* > AMD_openmpi_blocking.txt
./average.sh AMD_OpenMPI/128/pilatus_openmpi_persistent_* > AMD_openmpi_persistent.txt

./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_1_* > AMD_ext_mpi_1.txt
./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_2_* > AMD_ext_mpi_2.txt
./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_3_* > AMD_ext_mpi_3.txt
./average.sh AMD_ext_mpi/128/pilatus_ext_mpi_128_4_* > AMD_ext_mpi_4.txt

./minimum.sh AMD_ext_mpi_1.txt AMD_ext_mpi_2.txt > AMD_ext_mpi.txt

./average.sh AMD_MPICH/127/pilatus_mpich_127_* > AMD_mpich_blocking_127.txt
./average.sh AMD_MPICH/127/pilatus_mpich_persistent_* > AMD_mpich_persistent_127.txt
./average.sh AMD_OpenMPI/127/pilatus_openmpi_127_* > AMD_openmpi_blocking_127.txt
./average.sh AMD_OpenMPI/127/pilatus_openmpi_persistent_* > AMD_openmpi_persistent_127.txt

./average.sh AMD_ext_mpi/127/pilatus_ext_mpi_127_1_* > AMD_ext_mpi_127_1.txt
./average.sh AMD_ext_mpi/127/pilatus_ext_mpi_127_2_* > AMD_ext_mpi_127_2.txt
./average.sh AMD_ext_mpi/127/pilatus_ext_mpi_127_3_* > AMD_ext_mpi_127_3.txt
./average.sh AMD_ext_mpi/127/pilatus_ext_mpi_127_4_* > AMD_ext_mpi_127_4.txt

./minimum.sh AMD_ext_mpi_127_1.txt AMD_ext_mpi_127_2.txt > AMD_ext_mpi_127.txt
