ifeq ($(OMPI), 1)
CC = /scratch/snx2000/ajocksch/openmpi-4.1.0/bin/mpicc
OBJ = obj_ompi
BIN = bin_ompi
LIBNAME = ext_mpi_collectives_ompi
else
CC = mpicc
OBJ = obj
BIN = bin
LIBNAME = ext_mpi_collectives
endif

CFLAGS = -g -O2 -Wall -I. -Iinclude -Iinclude/core -Iinclude/mpi -DDEBUG -DV_ERBOSE -DM_MAP
#CFLAGS = -g -O2 -Iinclude

ifeq ($(GPU), 1)
CFLAGS += -DGPU_ENABLED -Iinclude/gpu
NVCC = nvcc
NVCCFLAGS = -g -G -O2 -Iinclude/gpu -Iinclude/mpi
endif

all: lib/libext_mpi_collectives.a $(BIN)/benchmark $(BIN)/benchmark_node $(BIN)/test_alltoall $(BIN)/test_allreduce $(BIN)/test_raw_code $(BIN)/test_raw_code_merge $(BIN)/test_buffer_offset $(BIN)/test_no_offset $(BIN)/test_raw_code_tasks_node $(BIN)/test_raw_code_tasks_node_master $(BIN)/test_reduce_copyin $(BIN)/test_reduce_copyout $(BIN)/test_optimise_buffers $(BIN)/test_optimise_buffers2 $(BIN)/test_parallel_memcpy $(BIN)/test_rank_permutation_forward $(BIN)/test_rank_permutation_backward $(BIN)/get_input_alltoall $(BIN)/get_input_allreduce $(BIN)/get_input_allgatherv $(BIN)/get_input_reduce_scatter $(BIN)/test_allreduce_groups $(BIN)/test_no_first_barrier $(BIN)/test_dummy
.PHONY: all

ifeq ($(GPU), 1)
lib/libext_mpi_collectives.a: $(OBJ)/alltoall.o $(OBJ)/allreduce.o $(OBJ)/allreduce_bit.o $(OBJ)/raw_code.o $(OBJ)/raw_code_merge.o $(OBJ)/buffer_offset.o $(OBJ)/no_offset.o $(OBJ)/raw_code_tasks_node.o $(OBJ)/raw_code_tasks_node_master.o $(OBJ)/ext_mpi_native.o $(OBJ)/ext_mpi.o $(OBJ)/reduce_copyin.o $(OBJ)/reduce_copyout.o $(OBJ)/read.o $(OBJ)/optimise_buffers.o $(OBJ)/optimise_buffers2.o $(OBJ)/parallel_memcpy.o $(OBJ)/rank_permutation.o $(OBJ)/allreduce_groups.o $(OBJ)/clean_barriers.o $(OBJ)/prime_factors.o $(OBJ)/no_first_barrier.o $(OBJ)/dummy.o $(OBJ)/gpu_core.o $(OBJ)/gpu_shmem.o $(OBJ)/backward_interpreter.o $(OBJ)/forward_interpreter.o
else
lib/libext_mpi_collectives.a: $(OBJ)/alltoall.o $(OBJ)/allreduce.o $(OBJ)/allreduce_bit.o $(OBJ)/raw_code.o $(OBJ)/raw_code_merge.o $(OBJ)/buffer_offset.o $(OBJ)/no_offset.o $(OBJ)/raw_code_tasks_node.o $(OBJ)/raw_code_tasks_node_master.o $(OBJ)/ext_mpi_native.o $(OBJ)/ext_mpi.o $(OBJ)/reduce_copyin.o $(OBJ)/reduce_copyout.o $(OBJ)/read.o $(OBJ)/optimise_buffers.o $(OBJ)/optimise_buffers2.o $(OBJ)/parallel_memcpy.o $(OBJ)/rank_permutation.o $(OBJ)/allreduce_groups.o $(OBJ)/clean_barriers.o $(OBJ)/prime_factors.o $(OBJ)/no_first_barrier.o $(OBJ)/dummy.o $(OBJ)/backward_interpreter.o $(OBJ)/forward_interpreter.o
endif
	mkdir -p lib
	ar -r lib/lib$(LIBNAME).a $(OBJ)/*.o

$(OBJ)/ext_mpi.o: include/mpi/ext_mpi.h include/mpi/ext_mpi_native.h src/mpi/ext_mpi.c latency_bandwidth.tmp node_size_threshold.tmp
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -DVERBOSE -c src/mpi/ext_mpi.c -o $(OBJ)/ext_mpi.o

$(OBJ)/ext_mpi_native.o: include/mpi/ext_mpi_native.h src/mpi/ext_mpi_native.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/mpi/ext_mpi_native.c -o $(OBJ)/ext_mpi_native.o

$(OBJ)/clean_barriers.o: include/mpi/clean_barriers.h src/mpi/clean_barriers.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/mpi/clean_barriers.c -o $(OBJ)/clean_barriers.o

$(OBJ)/prime_factors.o: include/core/prime_factors.h src/core/prime_factors.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/prime_factors.c -o $(OBJ)/prime_factors.o

$(OBJ)/read.o: include/core/read.h src/core/read.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/read.c -o $(OBJ)/read.o

$(OBJ)/alltoall.o: include/core/alltoall.h src/core/alltoall.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/alltoall.c -o $(OBJ)/alltoall.o

$(OBJ)/allreduce.o: include/core/allreduce.h include/core/allreduce_bit.h src/core/allreduce.c include/core/read.h
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/allreduce.c -o $(OBJ)/allreduce.o

$(OBJ)/allreduce_bit.o: include/core/allreduce_bit.h src/core/allreduce_bit.c include/core/read.h
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/allreduce_bit.c -o $(OBJ)/allreduce_bit.o

$(OBJ)/raw_code.o: include/core/raw_code.h src/core/raw_code.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/raw_code.c -o $(OBJ)/raw_code.o

$(OBJ)/raw_code_merge.o: include/core/raw_code_merge.h src/core/raw_code_merge.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/raw_code_merge.c -o $(OBJ)/raw_code_merge.o

$(OBJ)/buffer_offset.o: include/core/buffer_offset.h src/core/buffer_offset.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/buffer_offset.c -o $(OBJ)/buffer_offset.o

$(OBJ)/no_offset.o: include/core/no_offset.h src/core/no_offset.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/no_offset.c -o $(OBJ)/no_offset.o

$(OBJ)/raw_code_tasks_node.o: include/core/raw_code_tasks_node.h src/core/raw_code_tasks_node.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/raw_code_tasks_node.c -o $(OBJ)/raw_code_tasks_node.o

$(OBJ)/raw_code_tasks_node_master.o: include/core/raw_code_tasks_node_master.h src/core/raw_code_tasks_node_master.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/raw_code_tasks_node_master.c -o $(OBJ)/raw_code_tasks_node_master.o

$(OBJ)/optimise_buffers.o: include/core/optimise_buffers.h src/core/optimise_buffers.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/optimise_buffers.c -o $(OBJ)/optimise_buffers.o

$(OBJ)/optimise_buffers2.o: include/core/optimise_buffers2.h src/core/optimise_buffers2.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/optimise_buffers2.c -o $(OBJ)/optimise_buffers2.o

$(OBJ)/parallel_memcpy.o: include/core/parallel_memcpy.h src/core/parallel_memcpy.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/parallel_memcpy.c -o $(OBJ)/parallel_memcpy.o

$(OBJ)/backward_interpreter.o: include/mpi/backward_interpreter.h src/mpi/backward_interpreter.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/mpi/backward_interpreter.c -o $(OBJ)/backward_interpreter.o

$(OBJ)/forward_interpreter.o: include/mpi/forward_interpreter.h src/mpi/forward_interpreter.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/mpi/forward_interpreter.c -o $(OBJ)/forward_interpreter.o

$(OBJ)/reduce_copyin.o: include/core/reduce_copyin.h src/core/reduce_copyin.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/reduce_copyin.c -o $(OBJ)/reduce_copyin.o

$(OBJ)/reduce_copyout.o: include/core/reduce_copyout.h src/core/reduce_copyout.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/reduce_copyout.c -o $(OBJ)/reduce_copyout.o

$(OBJ)/rank_permutation.o: include/core/rank_permutation.h src/core/rank_permutation.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/rank_permutation.c -o $(OBJ)/rank_permutation.o

$(OBJ)/allreduce_groups.o: include/core/allreduce_groups.h src/core/allreduce_groups.c $(OBJ)/allreduce.o $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/allreduce_groups.c -o $(OBJ)/allreduce_groups.o

$(OBJ)/no_first_barrier.o: include/core/no_first_barrier.h src/core/no_first_barrier.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/no_first_barrier.c -o $(OBJ)/no_first_barrier.o

$(OBJ)/dummy.o: include/core/dummy.h src/core/dummy.c $(OBJ)/read.o
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/core/dummy.c -o $(OBJ)/dummy.o

ifeq ($(GPU), 1)
$(OBJ)/gpu_shmem.o: include/gpu/gpu_shmem.h src/gpu/gpu_shmem.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/gpu/gpu_shmem.c -o $(OBJ)/gpu_shmem.o

$(OBJ)/gpu_core.o: include/gpu/gpu_core.h src/gpu/gpu_core.cu
	mkdir -p $(OBJ)
	$(NVCC) $(NVCCFLAGS) -c src/gpu/gpu_core.cu -o $(OBJ)/gpu_core.o
endif

latency_bandwidth.tmp: minclude.sh latency_bandwidth/ext_mpi_bm.txt
	./minclude.sh latency_bandwidth/ext_mpi_bm.txt > latency_bandwidth.tmp

node_size_threshold.tmp: latency_bandwidth/ext_mpi_nst.txt
	cat latency_bandwidth/ext_mpi_nst.txt > node_size_threshold.tmp

$(BIN)/benchmark: src/initial_benchmark/benchmark.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) src/initial_benchmark/benchmark.c -o $(BIN)/benchmark

$(BIN)/benchmark_node: src/initial_benchmark/benchmark_node.c lib/lib$(LIBNAME).a
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) src/initial_benchmark/benchmark_node.c lib/lib$(LIBNAME).a -o $(BIN)/benchmark_node

$(BIN)/test_alltoall: tests/test_alltoall.c $(OBJ)/alltoall.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/alltoall.o tests/test_alltoall.c -o $(BIN)/test_alltoall

$(BIN)/test_allreduce: tests/test_allreduce.c $(OBJ)/allreduce.o $(OBJ)/allreduce_bit.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/allreduce.o $(OBJ)/allreduce_bit.o $(OBJ)/read.o tests/test_allreduce.c -o $(BIN)/test_allreduce

$(BIN)/test_raw_code: tests/test_raw_code.c $(OBJ)/raw_code.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/raw_code.o $(OBJ)/read.o tests/test_raw_code.c -o $(BIN)/test_raw_code

$(BIN)/test_raw_code_merge: tests/test_raw_code_merge.c $(OBJ)/raw_code_merge.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/raw_code_merge.o $(OBJ)/read.o tests/test_raw_code_merge.c -o $(BIN)/test_raw_code_merge

$(BIN)/test_buffer_offset: tests/test_buffer_offset.c $(OBJ)/buffer_offset.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/buffer_offset.o $(OBJ)/read.o tests/test_buffer_offset.c -o $(BIN)/test_buffer_offset

$(BIN)/test_no_offset: tests/test_no_offset.c $(OBJ)/no_offset.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/no_offset.o $(OBJ)/read.o tests/test_no_offset.c -o $(BIN)/test_no_offset

$(BIN)/test_raw_code_tasks_node: tests/test_raw_code_tasks_node.c $(OBJ)/raw_code_tasks_node.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/raw_code_tasks_node.o $(OBJ)/read.o tests/test_raw_code_tasks_node.c -o $(BIN)/test_raw_code_tasks_node

$(BIN)/test_raw_code_tasks_node_master: tests/test_raw_code_tasks_node_master.c $(OBJ)/raw_code_tasks_node_master.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/raw_code_tasks_node_master.o $(OBJ)/read.o tests/test_raw_code_tasks_node_master.c -o $(BIN)/test_raw_code_tasks_node_master

$(BIN)/test_optimise_buffers: tests/test_optimise_buffers.c $(OBJ)/optimise_buffers.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/optimise_buffers.o $(OBJ)/read.o tests/test_optimise_buffers.c -o $(BIN)/test_optimise_buffers

$(BIN)/test_optimise_buffers2: tests/test_optimise_buffers2.c $(OBJ)/optimise_buffers2.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/optimise_buffers2.o $(OBJ)/read.o tests/test_optimise_buffers2.c -o $(BIN)/test_optimise_buffers2

$(BIN)/test_parallel_memcpy: tests/test_parallel_memcpy.c $(OBJ)/parallel_memcpy.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/parallel_memcpy.o $(OBJ)/read.o tests/test_parallel_memcpy.c -o $(BIN)/test_parallel_memcpy

$(BIN)/test_reduce_copyin: tests/test_reduce_copyin.c $(OBJ)/reduce_copyin.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/reduce_copyin.o $(OBJ)/read.o tests/test_reduce_copyin.c -o $(BIN)/test_reduce_copyin

$(BIN)/test_reduce_copyout: tests/test_reduce_copyout.c $(OBJ)/reduce_copyout.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/reduce_copyout.o $(OBJ)/read.o tests/test_reduce_copyout.c -o $(BIN)/test_reduce_copyout

$(BIN)/test_rank_permutation_forward: tests/test_rank_permutation_forward.c $(OBJ)/rank_permutation.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/rank_permutation.o $(OBJ)/read.o tests/test_rank_permutation_forward.c -o $(BIN)/test_rank_permutation_forward

$(BIN)/test_rank_permutation_backward: tests/test_rank_permutation_backward.c $(OBJ)/rank_permutation.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/rank_permutation.o $(OBJ)/read.o tests/test_rank_permutation_backward.c -o $(BIN)/test_rank_permutation_backward

$(BIN)/get_input_alltoall: tests/get_input_alltoall.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) tests/get_input_alltoall.c -o $(BIN)/get_input_alltoall

$(BIN)/get_input_allreduce: tests/get_input_allreduce.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) tests/get_input_allreduce.c -o $(BIN)/get_input_allreduce

$(BIN)/get_input_allgatherv: tests/get_input_allgatherv.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) tests/get_input_allgatherv.c -o $(BIN)/get_input_allgatherv

$(BIN)/get_input_reduce_scatter: tests/get_input_reduce_scatter.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) tests/get_input_reduce_scatter.c -o $(BIN)/get_input_reduce_scatter

$(BIN)/test_allreduce_groups: tests/test_allreduce_groups.c $(OBJ)/allreduce_groups.o $(OBJ)/allreduce.o $(OBJ)/allreduce_bit.o $(OBJ)/read.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/allreduce_groups.o $(OBJ)/allreduce.o $(OBJ)/allreduce_bit.o $(OBJ)/read.o tests/test_allreduce_groups.c -o $(BIN)/test_allreduce_groups

$(BIN)/test_no_first_barrier: tests/test_no_first_barrier.c $(OBJ)/no_first_barrier.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/no_first_barrier.o $(OBJ)/read.o tests/test_no_first_barrier.c -o $(BIN)/test_no_first_barrier

$(BIN)/test_dummy: tests/test_dummy.c $(OBJ)/dummy.o
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ)/dummy.o $(OBJ)/read.o tests/test_dummy.c -o $(BIN)/test_dummy

clean:
	rm $(OBJ)/*.o lib/lib$(LIBNAME).a $(BIN)/* latency_bandwidth.tmp node_size_threshold.tmp
