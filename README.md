# ext_mpi_collectives

please cite

A. Jocksch, N. Ohana, E. Lanti, E. Koutsaniti, V. Karakasis, L. Villard: An optimisation of allreduce communication in message-passing systems. Parallel Computing, accepted

A. Jocksch, N. Ohana, E. Lanti, V. Karakasis, L. Villard: Optimised allgatherv, reduce_scatter and allreduce communication in message-passing systems. poster at PASC 2021

A. Jocksch, N. Ohana, E. Lanti, V. Karakasis, L. Villard: Towards an optimal allreduce communication in message-passing systems. poster at EuroMPI/USA'20

A. Jocksch, M. Kraushaar, D. Daverio: Optimised all-to-all communication on multicore architectures applied to FFTs with pencil decomposition. Concurrency and Computation: Practice and Experience, Vol 31, No 16, e4964, 2019

TODO:

 -More datatypes and reduction operations, currenty only float, double, int and long int with MPI_SUM

tests:

./bin/get_input_allreduce.x | ./bin/test_allreduce.x | ./bin/test_raw_code_tasks_node.x | ./bin/test_reduce_copyin.x | ./bin/test_raw_code.x | ./bin/test_reduce_copyout.x | ./bin/test_buffer_offset.x | ./bin/test_no_offset.x | ./bin/test_optimise_buffers.x | ./bin/test_optimise_buffers2.x | ./bin/test_parallel_memcpy.x | ./bin/test_raw_code_merge.x

./bin/simulate.x

benchmark:

mpirun -n 36 ./bin/benchmark.x
