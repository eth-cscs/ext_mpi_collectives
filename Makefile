CC = mpicc
OBJ = obj
BIN = bin
LIBNAME = ext_mpi_collectives
INCLUDE = -I. -Iinclude/core -Iinclude/mpi

CFLAGS = -g -O2 -Wall $(INCLUDE) -DDEBUG -DV_ERBOSE -DM_MAP

SOURCES = $(wildcard src/core/*.c src/mpi/*.c)
OBJECTS = $(subst src,$(OBJ),$(SOURCES:.c=.o))
TESTS = $(wildcard tests/*.c)
TESTSBIN = $(subst tests,bin,$(TESTS:.c=.x))

all: lib/libext_mpi_collectives.a $(TESTSBIN)
.PHONY: all

lib/libext_mpi_collectives.a: $(OBJECTS)
	mkdir -p lib
	ar -r lib/lib$(LIBNAME).a $(OBJ)/core/*.o $(OBJ)/mpi/*.o

DEPDIR := .deps
DEPFLAGS = -MMD -MT $(OBJ)/$*.o -MP -MF $(DEPDIR)/$*.d
DEPENDENCIES = $(subst src,$(DEPDIR),$(SOURCES:.c=.d))

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) $(TARGET_ARCH) -c

latency_bandwidth.tmp: minclude.sh latency_bandwidth/ext_mpi_bm.txt
	./minclude.sh latency_bandwidth/ext_mpi_bm.txt > latency_bandwidth.tmp

node_size_threshold.tmp: latency_bandwidth/ext_mpi_nst.txt
	cat latency_bandwidth/ext_mpi_nst.txt > node_size_threshold.tmp

.deps/%.d: src/%.c node_size_threshold.tmp latency_bandwidth.tmp
	$(COMPILE.c) $< -E > /dev/null

bin/%.x: tests/%.c
	$(CC) $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) tests/$*.c -Llib -l$(LIBNAME)

DEPFILES := $(patsubst %.c,%.d,$(subst src,$(DEPDIR),$(SOURCES)))
$(DEPFILES):

include $(DEPFILES)

obj/%.o: .deps/%.d
	$(CC) -c $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) src/$*.c

clean:
	rm $(OBJ)/core/*.o $(OBJ)/mpi/*.o lib/lib$(LIBNAME).a $(BIN)/* latency_bandwidth.tmp node_size_threshold.tmp
