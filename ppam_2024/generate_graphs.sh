#!/bin/bash

./average.sh AMD_HPE_MPI/4_nodes/eiger_hpe_mpi_128_4nodes_1*.txt > AMD_HPE_MPI_4nodes.txt
./average.sh Grace_HPE_MPI/4_nodes/todi_hpe_mpi_288_4nodes_1_*.txt > Grace_HPE_MPI_4nodes.txt

for i in `seq 1 4`
do
  ./average.sh AMD_HPE_MPI_ext_mpi/4_nodes/eiger_hpe_mpi_ext_mpi_128_4nodes_"$i"_* > AMD_HPE_MPI_ext_mpi_128_4nodes_$i.txt
done
for i in `seq 1 6`
do
  ./average.sh Grace_HPE_MPI_ext_mpi/4_nodes/todi_hpe_mpi_ext_mpi_288_4nodes_"$i"_* > Grace_HPE_MPI_ext_mpi_288_4nodes_$i.txt
done

./minimum.sh AMD_HPE_MPI_ext_mpi_128_4nodes_?.txt > AMD_HPE_MPI_ext_mpi_128_4nodes.txt
./minimum.sh Grace_HPE_MPI_ext_mpi_288_4nodes_?.txt > Grace_HPE_MPI_ext_mpi_288_4nodes.txt

for tasks in 127 128
do
  ./average.sh AMD_MPICH/$tasks/pilatus_mpich_"$tasks"_* > AMD_mpich_blocking_$tasks.txt
  ./average.sh AMD_MPICH/$tasks/pilatus_mpich_persistent_"$tasks"_* > AMD_mpich_persistent_$tasks.txt
  ./average.sh AMD_OpenMPI/$tasks/pilatus_openmpi_"$tasks"_* > AMD_openmpi_blocking_$tasks.txt
  ./average.sh AMD_OpenMPI/$tasks/pilatus_openmpi_persistent_"$tasks"_* > AMD_openmpi_persistent_$tasks.txt

  for i in `seq 1 4`
  do
    ./average.sh AMD_ext_mpi/$tasks/pilatus_ext_mpi_"$tasks"_"$i"_* > AMD_ext_mpi_"$tasks"_$i.txt
  done

  ./minimum.sh AMD_ext_mpi_"$tasks"_1.txt AMD_ext_mpi_"$tasks"_2.txt > AMD_ext_mpi_$tasks.txt
done

for tasks in 71 72 283 288 288_l
do
  ./average.sh Grace_MPICH/$tasks/todi_mpich_"$tasks"_* > Grace_mpich_blocking_$tasks.txt
  ./average.sh Grace_MPICH/$tasks/todi_mpich_persistent_"$tasks"_* > Grace_mpich_persistent_$tasks.txt
  ./average.sh Grace_OpenMPI/$tasks/todi_openmpi_"$tasks"_* > Grace_openmpi_blocking_$tasks.txt
  ./average.sh Grace_OpenMPI/$tasks/todi_openmpi_persistent_"$tasks"_* > Grace_openmpi_persistent_$tasks.txt

  for i in `seq 1 4`
  do
    ./average.sh Grace_ext_mpi/$tasks/todi_ext_mpi_"$tasks"_"$i"_* > Grace_ext_mpi_"$tasks"_$i.txt
  done

  ./minimum.sh Grace_ext_mpi_"$tasks"_1.txt Grace_ext_mpi_"$tasks"_2.txt > Grace_ext_mpi_$tasks.txt
done
