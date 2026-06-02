# ext_mpi_collectives

## What it is

The **ext_mpi_collectives** library provides persistent collective communication operations according to the MPI 4.0 standard. It is built on top of existing MPI libraries. If the underlying library is MPI 4.0 enabled the MPI profiler hook is applied for linking. The fast execution of the collectives through the library is provided by the following features:

 - Communication with flexible number of ports per node depending on the message size
 - Benchmark at installation time of the library for latency and bandwidth measurements
 - Cyclic shift algorithm for flexible number of ports for *allgatherv*, *reduce_scatter* and *allreduce*
 - Rank reordering for variable message sizes of *allgatherv* and *reduce_scatter*
 - Extension of cyclic shift algorithm with prefix sum for *allreduce*

Furthermore blocking collective communication operations are supported experimentally, which are based on the persistent communication. Several different algorithms are set up and adapted for the specific message length of every blocking call.

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

In the current version of the libary the benchmark at installation time is deactivated. It is necessary to specify the factors / radixes of the algorithms manually, e.g.,

```
export EXT_MPI_COPYIN='6;-64 -9 -8 -4 4 8 9'
export EXT_MPI_COPYIN='6;1 -9 -8 -4 4 8 9'
export EXT_MPI_COPYIN='6;1 -3 -3 -4 -2 -2 -2 2 2 2 4 3 3'
export EXT_MPI_COPYIN='6;1 -3 -3 -2 -2 -2 -2 -2 2 2 2 2 2 3 3'
export EXT_MPI_COPYIN='6;1 -72 -4 4 72'
export EXT_MPI_COPYIN='6;1 -288 288'
```

are a few options for allreduce on a 4 sockets Grace-Hoppe node. The '6' before the semicolon indicates that the recursive exchage algorithm will be used. The first number after the semicolon ('-64' or '1') is if negative the size of the fractions of the data vector in bytes, if positive it should be 1 and the fractions of the data vector are chosen as equal as possible. The following negative numbers are the factors / radixes of the reduce_scatter part and the final positive numbers the factors / rdixes of the allgather part. The optimal set of parameters is message size dependent.

For multiple nodes the variable

```
export EXT_MPI_NUM_PORTS='4(3)'
export EXT_MPI_NUM_PORTS='4(-1 -1 1 1)'
export EXT_MPI_NUM_PORTS='4(-1 1 1)'
export EXT_MPI_NUM_PORTS='4(1 1)'
```

needs to be set. The number before the parenthesis is the number of nodes. In the parenthesis the negative numbers are the factors / radixes minus one for the reduce_scatter part and the positive numbers are the factors / radixes minus one for the allgather part.

For blocking communication parameter files with the factors / radixes are required. For 288 processes on a four socket Grace-Hopper node possible parameters are:

```
cat ext_mpi_allreduce_blocking_1_288.txt
8 1 1(-1) 9;-64_-9_-8_-4_4_8_9
1024 1 1(-1) 9;1_-9_-8_-4_4_8_9
4096 1 1(-1) 6;1_-9_-8_-4_4_8_9
524288 1 1(-1) 6;1_-72_-4_4_72
8 1 1(-1) 9;-64_-9_-8_-4_4_8_9
1024 1 1(-1) 9;1_-9_-8_-4_4_8_9
4096 1 1(-1) 6;1_-9_-8_-4_4_8_9
524288 1 1(-1) 6;1_-72_-4_4_72
```

The first row is the lower limit of the message size from which the algorithm is taken, where the first four lines and second four lines indicate the parameters for CPU pointers and GPU pointers, respectively. The next number is the number of logical sockets per node. The '1(-1)' indicate one node without inter-node communication and correspond to the EXT_MPI_NUM_PORTS variable. The following numbers indicate the factors / radixes for intra-node communication and correspond to the EXT_MPI_COPYIN variable.

## GPU support

The library supports GPU aware MPI using this feature from the communication primitives of the underlying library. For using it the alternative Makefiles

```
make -f Makefile_cuda
make -f Makefile_hip
```

are provided.

## Current limitations

Only the collective operation `MPI_Allreduce` / `MPI_Allreduce_init` is well tested. Other supported collective operations `MPI_Allgatherv_init`, `MPI_Reduce_scatter_init`, `MPI_Bcast_init`, `MPI_Reduce_init`, `MPI_Gatherv_init`, `MPI_Scatterv_init`, `MPI_Allgather_init`, `MPI_Reduce_scatter_block_init`, `MPI_Gather_init`, `MPI_Scatter_init` are experimental. Datatypes need to be contiguous and communicators need to be intra communicators. For reduction operations only the datatypes `float`, `double`, `int` and `long int` with the operation `MPI_SUM` are supported.

## Tests

The components of the library which do not require MPI can be tested independently. Every of the following executables takes the input of the previous one and further processes the data. The different steps represent a mini language and an assembler code which is optimised step by step.

```
./bin_tests/get_input_allreduce.x | ./bin_tests/test_allreduce.x | ./bin_tests/test_raw_code_tasks_node.x | ./bin_tests/test_reduce_copyin.x | ./bin_tests/test_raw_code.x | ./bin_tests/test_reduce_copyout.x | ./bin_tests/test_buffer_offset.x | ./bin_tests/test_no_offset.x | ./bin_tests/test_optimise_buffers.x | ./bin_tests/test_optimise_buffers2.x | ./bin_tests/test_parallel_memcpy.x | ./bin_tests/test_raw_code_merge.x
```

```
./bin_tests/get_input_allreduce.x | ./bin_tests/test_allreduce_recursive.x | ./bin_tests/test_raw_code_tasks_node.x | ./bin_tests/test_reduce_copyin.x | ./bin_tests/test_raw_code.x | ./bin_tests/test_reduce_copyout.x | ./bin_tests/test_buffer_offset.x | ./bin_tests/test_no_offset.x | ./bin_tests/test_optimise_buffers.x | ./bin_tests/test_optimise_buffers2.x | ./bin_tests/test_parallel_memcpy.x | ./bin_tests/test_raw_code_merge.x
```

## CMake (currently not supported)

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
|    EXT_MPI_NUM_PORTS             |  parameters for internode communication algorithms       |  to be set                |
|    EXT_MPI_COPYIN                |  parameters for copy in (and out) reduction algorithm    |  to be set                |
|    EXT_MPI_BLOCKING              |  activate blocking collective support                    |  0                        |

the same parameters can be passed with the MPI_Info argument where the variable names are in lower case

## On Slingshot:
export FI_MR_CACHE_MONITOR=memhooks

## Third party software included
https://github.com/greensky00/avltree.git

## Literature

[A. Jocksch, C. N. Avans, R. Shipley, A. Skjellum (2026). A compilation-based approach to performant reduction and redistribution collective communication algorithms. The International Journal of High Performance Computing Applications, 40(2), 219-239.](https://doi.org/10.1177/10943420251363423)

[A. Jocksch, N. Ohana, E. Lanti, E. Koutsaniti, V. Karakasis, L. Villard: An optimisation of allreduce communication in message-passing systems. Parallel Computing, 107 (2021) 102812](https://doi.org/10.1016/j.parco.2021.102812)

A. Jocksch, M. Kraushaar, D. Daverio: Optimised all-to-all communication on multicore architectures applied to FFTs with pencil decomposition. Concurrency and Computation: Practice and Experience, Vol 31, No 16, e4964, 2019
