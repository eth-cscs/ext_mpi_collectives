# ext_mpi_collectives

## What it is

The **ext_mpi_collectives** library provides persistent collective communication operations according to the MPI 4.0 standard. It is built on top of existing MPI libraries. If the underlying library is MPI 4.0 enabled the MPI profiler hook is applied for linking. The fast execution of the collectives through the library is provided by the following features:

 - Communication with flexible number of ports per node depending on the message size
 - Benchmark at installation time of the library for latency and bandwidth measurements
 - Cyclic shift algorithm for flexible number of ports for *allgatherv*, *reduce_scatter* and *allreduce*
 - Rank reordering for variable message sizes of *allgatherv* and *reduce_scatter*
 - Extension of cyclic shift algorithm with prefix sum for *allreduce*

## Installation and usage

Clone the repository:

```
git clone https://github.com/eth-cscs/ext_mpi_collectives.git
cd ext_mpi_collectives
```

If necessary, adapt the MPI wrapper of the C compiler in the first line of the Makefile. Then just run:

```
make
```

If the existing MPI library supports the standard 4.0 it is just required to re-link the application with the library

```
mpicxx main.o -Lext_mpi_path/lib -lext_mpi_collectives -o executable.x
```

if not the header file `ext_mpi_path/include/mpi/ext_mpi_interface.h` needs to be included in the files calling the libraries and the application needs to be recompiled, e.g.

```
mpicxx main.cc -Lext_mpi_path/lib -lext_mpi_collectives -o executable.x
```

## Benchmark

When compiling the library also a benchmark is generated which serves also as example for the library\'s usage

```
mpirun -n 36 ./bin/benchmark.x
```

## GPU support

The library supports CUDA aware MPI using this feature from the communication primitives of the underlying library. For using it the alternative Makefile

```
make -f Makefile_gpu
```

is provided with the benchmark

```
mpirun -n 36 ./bin/benchmark_gpu.x
```

This feature is not highly optimised.

## Current limitations

Only the collective operations `MPI_Allreduce_init`, `MPI_Allgatherv_init`, `MPI_Reduce_scatter_init`, `MPI_Bcast_init`, `MPI_Reduce_init`, `MPI_Gatherv_init`, `MPI_Scatterv_init`, `MPI_Allgather_init`, `MPI_Reduce_scatter_block_init`, `MPI_Gather_init`, `MPI_Scatter_init` are provided. Datatypes need to be contiguous and communicators need to be intra communicators. For reduction operations only the datatypes `float`, `double`, `int` and `long int` with the operation `MPI_SUM` are supported.

## Tests

The components of the library which do not require MPI can be tested independently. Every of the following executables takes the input of the previous one and further processes the data. The different steps represent a mini language and an assembler code which is optimised step by step.

```
./bin_tests/get_input_allreduce.x | ./bin_tests/test_allreduce.x | ./bin_tests/test_raw_code_tasks_node.x | ./bin_tests/test_reduce_copyin.x | ./bin_tests/test_raw_code.x | ./bin_tests/test_reduce_copyout.x | ./bin_tests/test_buffer_offset.x | ./bin_tests/test_no_offset.x | ./bin_tests/test_optimise_buffers.x | ./bin_tests/test_optimise_buffers2.x | ./bin_tests/test_parallel_memcpy.x | ./bin_tests/test_raw_code_merge.x
```

```
./bin_tests/get_input_allreduce.x | ./bin_tests/test_allreduce_recursive.x | ./bin_tests/test_raw_code_tasks_node.x | ./bin_tests/test_reduce_copyin.x | ./bin_tests/test_raw_code.x | ./bin_tests/test_reduce_copyout.x | ./bin_tests/test_buffer_offset.x | ./bin_tests/test_no_offset.x | ./bin_tests/test_optimise_buffers.x | ./bin_tests/test_optimise_buffers2.x | ./bin_tests/test_parallel_memcpy.x | ./bin_tests/test_raw_code_merge.x
```

There is also a standalone test for automatic parameter detection of the *allreduce* algorithm

```
./bin/simulate.x
```

## CMake

- Required before running cmake:

```
./minclude.sh latency_bandwidth/ext_mpi_bm.txt > latency_bandwidth.tmp
cat latency_bandwidth/ext_mpi_nst.txt > node_size_threshold.tmp
```

- Build libext_mpi_collectives.a with CMake:

```
cmake -B build -S ext_mpi_collectives.git \
-DBUILD_TESTING=ON

cmake --build build -j
# cmake --build build -t help
# cmake --build build -t test_waitany.x -v
cmake --install build --prefix myinstalldir
# -- Installing: ./myinstalldir/lib/libext_mpi_collectives.a
```

### CMake + CUDA

- Build with:

```
cmake -B build+cuda -S ext_mpi_collectives.git \
    -DBUILD_TESTING=ON -DCMAKE_CUDA_ARCHITECTURES=80
cmake --build build+cuda -j
cmake --install build+cuda --prefix myinstalldir
```

## Environment variables and arguments of MPI_Info

Environment variables:

|    variable name                 |  meaning                                                 |  default                  |
| -------------------------------- | -------------------------------------------------------- | ------------------------- |
|    EXT_MPI_VERBOSE               |  print parameters                                        |  0                        |
|    EXT_MPI_DEBUG                 |  internal check of values communicated in initalisation  |  1                        |
|    EXT_MPI_NUM_SOCKETS_PER_NODE  |  number of sockets per node                              |  1                        |
|    EXT_MPI_NOT_RECURSIVE         |  only cyclic shift algorithms not recursive ones         |  0                        |
|    EXT_MPI_ALTERNATING           |  alternating buffer                                      |  depends on message size  |
|    EXT_MPI_NUM_TASKS_PER_SOCKET  |  number of tasks per socket                              |  as many as possible      |
|    EXT_MPI_NUM_PORTS             |  parameters for internode communication algorithms       |  measurement              |
|    EXT_MPI_COPYIN                |  parameters for copy in reduction algorithm              |  measurement              |

the same parameters can be passed with the MPI_Info argument where the variable names are in lower case

## Literature

[A. Jocksch, N. Ohana, E. Lanti, E. Koutsaniti, V. Karakasis, L. Villard: An optimisation of allreduce communication in message-passing systems. Parallel Computing, 107 (2021) 102812](https://doi.org/10.1016/j.parco.2021.102812)

A. Jocksch, N. Ohana, E. Lanti, V. Karakasis, L. Villard: Optimised allgatherv, reduce_scatter and allreduce communication in message-passing systems. poster at PASC 2021

A. Jocksch, N. Ohana, E. Lanti, V. Karakasis, L. Villard: Towards an optimal allreduce communication in message-passing systems. poster at EuroMPI/USA'20

A. Jocksch, M. Kraushaar, D. Daverio: Optimised all-to-all communication on multicore architectures applied to FFTs with pencil decomposition. Concurrency and Computation: Practice and Experience, Vol 31, No 16, e4964, 2019
