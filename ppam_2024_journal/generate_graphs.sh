#!/bin/bash

./average.sh AMD_MPICH/pilatus_reduce_mpich_128_*.txt > AMD_MPICH_reduce.txt
./average.sh AMD_MPICH/pilatus_reduce_mpich_persistent_128*.txt > AMD_MPICH_reduce_persistent.txt
./average.sh AMD_MPICH/pilatus_bcast_mpich_128_*.txt > AMD_MPICH_bcast.txt
./average.sh AMD_MPICH/pilatus_bcast_mpich_persistent_128*.txt > AMD_MPICH_bcast_persistent.txt

./average.sh AMD_OpenMPI/pilatus_reduce_openmpi_128_*.txt > AMD_OpenMPI_reduce_openmpi.txt
./average.sh AMD_OpenMPI/pilatus_reduce_openmpi_persistent_128*.txt > AMD_OpenMPI_reduce_openmpi_persistent.txt
./average.sh AMD_OpenMPI/pilatus_bcast_openmpi_128_*.txt > AMD_OpenMPI_bcast_openmpi.txt
./average.sh AMD_OpenMPI/pilatus_bcast_openmpi_persistent_128*.txt > AMD_OpenMPI_bcast_openmpi_persistent.txt

for i in a b
do
  ./average.sh AMD_ext_mpi/pilatus_reduce_ext_mpi_128_"$i"_*.txt > AMD_ext_mpi_reduce_"$i".txt
done

for i in a b c d
do
  ./average.sh AMD_ext_mpi/pilatus_bcast_ext_mpi_128_"$i"_*.txt > AMD_ext_mpi_bcast_"$i".txt
done

./minimum.sh AMD_ext_mpi_reduce_?.txt > AMD_ext_mpi_reduce.txt
./minimum.sh AMD_ext_mpi_bcast_?.txt > AMD_ext_mpi_bcast.txt

for tasks in 72 288
do
  ./average.sh Grace_MPICH/santis_reduce_mpich_"$tasks"_*.txt > Grace_MPICH_reduce_"$tasks".txt
  ./average.sh Grace_MPICH/santis_reduce_mpich_persistent_"$tasks"_*.txt > Grace_MPICH_reduce_persistent_"$tasks".txt
  ./average.sh Grace_MPICH/santis_bcast_mpich_"$tasks"_*.txt > Grace_MPICH_bcast_"$tasks".txt
  ./average.sh Grace_MPICH/santis_bcast_mpich_persistent_"$tasks"_*.txt > Grace_MPICH_bcast_persistent_"$tasks".txt

  ./average.sh Grace_OpenMPI/santis_reduce_openmpi_"$tasks"_*.txt > Grace_OpenMPI_reduce_"$tasks".txt
  ./average.sh Grace_OpenMPI/santis_reduce_openmpi_persistent_"$tasks"_*.txt > Grace_OpenMPI_reduce_persistent_"$tasks".txt
  ./average.sh Grace_OpenMPI/santis_bcast_openmpi_"$tasks"_*.txt > Grace_OpenMPI_bcast_"$tasks".txt
  ./average.sh Grace_OpenMPI/santis_bcast_openmpi_persistent_"$tasks"_*.txt > Grace_OpenMPI_bcast_persistent_"$tasks".txt

  for i in a b c d
  do
    ./average.sh Grace_ext_mpi/santis_reduce_ext_mpi_"$tasks"_"$i"_*.txt > Grace_ext_mpi_reduce_"$tasks"_"$i".txt
  done

  for i in a b c d e f g h
  do
    ./average.sh Grace_ext_mpi/santis_bcast_ext_mpi_"$tasks"_"$i"_*.txt > Grace_ext_mpi_bcast_"$tasks"_"$i".txt
  done

  ./minimum.sh Grace_ext_mpi_reduce_"$tasks"_*.txt > Grace_ext_mpi_reduce_"$tasks".txt
  ./minimum.sh Grace_ext_mpi_bcast_"$tasks"_*.txt > Grace_ext_mpi_bcast_"$tasks".txt

done
