#!/bin/bash

#for tasks in 127 128
#do
#./average.sh AMD_MPICH/$tasks/pilatus_mpich_$tasks_* > AMD_mpich_blocking_$tasks.txt
#./average.sh AMD_MPICH/$tasks/pilatus_mpich_persistent_$tasks_* > AMD_mpich_persistent_$tasks.txt
#./average.sh AMD_OpenMPI/$tasks/pilatus_openmpi_$tasks_* > AMD_openmpi_blocking_$tasks.txt
#./average.sh AMD_OpenMPI/$tasks/pilatus_openmpi_persistent_$tasks_* > AMD_openmpi_persistent_$tasks.txt
#
#./average.sh AMD_ext_mpi/$tasks/pilatus_ext_mpi_$tasks_1_* > AMD_ext_mpi_$tasks_1.txt
#./average.sh AMD_ext_mpi/$tasks/pilatus_ext_mpi_$tasks_2_* > AMD_ext_mpi_$tasks_2.txt
#./average.sh AMD_ext_mpi/$tasks/pilatus_ext_mpi_$tasks_3_* > AMD_ext_mpi_$tasks_3.txt
#./average.sh AMD_ext_mpi/$tasks/pilatus_ext_mpi_$tasks_4_* > AMD_ext_mpi_$tasks_4.txt
#
#./minimum.sh AMD_ext_mpi_$tasks_1.txt AMD_ext_mpi_$tasks_2.txt > AMD_ext_mpi_$tasks.txt
#done

for tasks in 71 72 283 288 288_l
do
./average.sh Grace_MPICH/$tasks/todi_mpich_$tasks_* > Grace_mpich_blocking_$tasks.txt
./average.sh Grace_MPICH/$tasks/todi_mpich_persistent_$tasks_* > Grace_mpich_persistent_$tasks.txt
./average.sh Grace_OpenMPI/$tasks/todi_openmpi_$tasks_* > Grace_openmpi_blocking_$tasks.txt
./average.sh Grace_OpenMPI/$tasks/todi_openmpi_persistent_$tasks_* > Grace_openmpi_persistent_$tasks.txt

./average.sh Grace_ext_mpi/$tasks/todi_ext_mpi_$tasks_1_* > Grace_ext_mpi_$tasks_1.txt
./average.sh Grace_ext_mpi/$tasks/todi_ext_mpi_$tasks_2_* > Grace_ext_mpi_$tasks_2.txt
./average.sh Grace_ext_mpi/$tasks/todi_ext_mpi_$tasks_3_* > Grace_ext_mpi_$tasks_3.txt
./average.sh Grace_ext_mpi/$tasks/todi_ext_mpi_$tasks_4_* > Grace_ext_mpi_$tasks_4.txt

./minimum.sh Grace_ext_mpi_$tasks_1.txt Grace_ext_mpi_$tasks_2.txt > Grace_ext_mpi_$tasks.txt
done
