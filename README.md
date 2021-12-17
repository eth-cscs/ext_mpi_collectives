# ext_mpi_collectives

## What it is

The **ext_mpi_collectives** library provides persistent collective communication operations according to the MPI 4.0 standard. It is build on top of MPI libraries which might support or might not support MPI 4.0. The faster execution of the collectives through the library is provided using the MPI profiler hook.

## Installation and usage

If necessary the MPI wrapper of the C compiler in the first line of the Makefile should be adapted. Then

```
make
```

is executed. If the existing MPI library supports the standard 4.0 it is just required to re-link the application with the library

```
mpicxx main.o -Lext_mpi_path/lib -lext_mpi_collectives -o executable.x
```

if not the header file `ext_mpi_path/include/mpi/ext_mpi_interface.h` needs to be included in the files calling the libaries and the application nedds to be recompiled, e.g.

```
mpicxx main.cc -Lext_mpi_path/lib -lext_mpi_collectives -o executable.x
```

## Benchmark

When compiling the library also a benchmark is generated which serves also als example for the libary's usage

```
mpirun -n 36 ./bin/benchmark.x
```

## GPU support

The library supports CUDA aware MPI using this feature from the communication primitives of the underlying library. For using it the alternative Makefile

```
make -f Makfile_gpu
```

is provided with the benchmark

```
mpirun -n 36 ./bin/benchmark_gpu.x
```

This feature is not highly optimised.

## Current limitations

Only the collective operations `MPI_Allreduce_init, MPI_Allgatherv_init, MPI_Reduce_scatter_init, MPI_Bcast_init, MPI_Reduce_init, MPI_Gatherv_init, MPI_Scatterv_init, MPI_Allgather_init, MPI_Reduce_scatter_block_init, MPI_Gather_init, MPI_Scatter_init` are provided. Datatypes need to be contiguous and communicators need to be intra communicators. For reduction operations only the datatypes float, double, int and long int with the operation MPI_SUM are supported.

## Tests

The components of the library which do not require MPI can be tested independently. Every of the follwoing executables takes the input of the previous one and further processes the data. The different steps represent a mini language and an assembler code which is optimised step by step.

```
./bin/get_input_allreduce.x | ./bin/test_allreduce.x | ./bin/test_raw_code_tasks_node.x | ./bin/test_reduce_copyin.x | ./bin/test_raw_code.x | ./bin/test_reduce_copyout.x | ./bin/test_buffer_offset.x | ./bin/test_no_offset.x | ./bin/test_optimise_buffers.x | ./bin/test_optim
ise_buffers2.x | ./bin/test_parallel_memcpy.x | ./bin/test_raw_code_merge.x
```

There is also a standalone test for automatic parameter detection of the **allreduce** algorithm

```
./bin/simulate.x
```

## Literature

A. Jocksch, N. Ohana, E. Lanti, E. Koutsaniti, V. Karakasis, L. Villard: An optimisation of allreduce communication in message-passing systems. Parallel Computing, 107 (2021) 102812

A. Jocksch, N. Ohana, E. Lanti, V. Karakasis, L. Villard: Optimised allgatherv, reduce_scatter and allreduce communication in message-passing systems. poster at PASC 2021

A. Jocksch, N. Ohana, E. Lanti, V. Karakasis, L. Villard: Towards an optimal allreduce communication in message-passing systems. poster at EuroMPI/USA'20

A. Jocksch, M. Kraushaar, D. Daverio: Optimised all-to-all communication on multicore architectures applied to FFTs with pencil decomposition. Concurrency and Computation: Practice and Experience, Vol 31, No 16, e4964, 2019
