CC = mpicc
CFLAGS = -g -O2 -Iinclude -DDEBUG -DVERBOSE
#CFLAGS = -g -O2 -Iinclude -DMMAP

OBJ = obj
LIBNAME = ext_mpi_collectives

all: lib/libext_mpi_collectives.a bin/benchmark
.PHONY: all

lib/libext_mpi_collectives.a: $(OBJ)/ext_mpi_alltoall_native.o $(OBJ)/ext_mpi_alltoall.o $(OBJ)/ext_mpi_alltoall_native_f.o $(OBJ)/ext_mpi_alltoall_f.o
	mkdir -p lib
	ar -r lib/lib$(LIBNAME).a $(OBJ)/*.o

$(OBJ)/ext_mpi_alltoall_native.o: include/ext_mpi_alltoall_native.h src/ext_mpi_alltoall_native.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/ext_mpi_alltoall_native.c -o $(OBJ)/ext_mpi_alltoall_native.o

$(OBJ)/ext_mpi_alltoall.o: include/ext_mpi_alltoall_native.h include/ext_mpi_alltoall.h src/ext_mpi_alltoall.c latency_bandwidth.tmp
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -I. -c src/ext_mpi_alltoall.c -o $(OBJ)/ext_mpi_alltoall.o

$(OBJ)/ext_mpi_alltoall_native_f.o: include/ext_mpi_alltoall_native.h src/ext_mpi_alltoall_native_f.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/ext_mpi_alltoall_native_f.c -o $(OBJ)/ext_mpi_alltoall_native_f.o

$(OBJ)/ext_mpi_alltoall_f.o: include/ext_mpi_alltoall.h src/ext_mpi_alltoall_f.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c src/ext_mpi_alltoall_f.c -o $(OBJ)/ext_mpi_alltoall_f.o

latency_bandwidth.tmp: minclude.sh latency_bandwidth/ext_mpi_bm.txt
	./minclude.sh latency_bandwidth/ext_mpi_bm.txt > latency_bandwidth.tmp

bin/benchmark: src/benchmark.c
	mkdir -p bin
	$(CC) $(CFLAGS) src/benchmark.c -o bin/benchmark

clean:
	rm $(OBJ)/*.o lib/lib$(LIBNAME).a bin/benchmark latency_bandwidth.tmp
