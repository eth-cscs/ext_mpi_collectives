CC = mpicc
OBJ = obj
BIN = bin
LIBNAME = ext_mpi_collectives
INCLUDE = -I. -Iinclude/core -Iinclude/mpi -I/usr/include -I/opt/cray/xpmem/default/include

DEPDIR := .deps
directories := $(shell (mkdir -p $(DEPDIR); mkdir -p $(DEPDIR)/core; mkdir -p $(DEPDIR)/mpi; mkdir -p $(DEPDIR)/fortran; mkdir -p $(DEPDIR)/initial_benchmark; mkdir -p $(DEPDIR)/noopt; mkdir -p $(OBJ); mkdir -p $(OBJ)/core; mkdir -p $(OBJ)/mpi; mkdir -p $(OBJ)/fortran; mkdir -p $(OBJ)/initial_benchmark; mkdir -p $(OBJ)/noopt; mkdir -p $(BIN); mkdir -p lib; mkdir bin_tests))

CFLAGS = -g -O2 -Wall -fPIC $(INCLUDE) -DDEBUG -DM_MAP -DXPMEM

SOURCES = $(wildcard src/core/*.c src/mpi/*.c src/fortran/*.c src/noopt/*.c src/initial_benchmark/*.c)
OBJECTS = $(subst src,$(OBJ),$(SOURCES:.c=.o))
TESTS = $(wildcard tests/*.c)
TESTSBIN = $(subst tests,bin_tests,$(TESTS:.c=.x))
INITBENCHMARKS = $(wildcard src/initial_benchmark/*.c)
INITBENCHMARKSBIN = $(subst src/initial_benchmark,bin,$(INITBENCHMARKS:.c=.x))
LIBS = -Llib -l$(LIBNAME) -lrt -lm -L/usr/lib64 -lxpmem -ldl

.PHONY: all clean
all: lib/lib$(LIBNAME).a lib/lib$(LIBNAME).so $(TESTSBIN) $(INITBENCHMARKSBIN)

lib/lib$(LIBNAME).a: $(OBJECTS)
	ar -r lib/lib$(LIBNAME).a $(OBJ)/core/*.o $(OBJ)/mpi/*.o $(OBJ)/fortran/*.o $(OBJ)/noopt/*.o

lib/lib$(LIBNAME).so: $(OBJECTS)
	$(CC) -shared $(LIBS) $(OBJ)/core/*.o $(OBJ)/mpi/*.o $(OBJ)/fortran/*.o $(OBJ)/noopt/*.o -o lib/lib$(LIBNAME).so

DEPFLAGS = -MMD -MT $(OBJ)/$*.o -MP -MF $(DEPDIR)/$*.d
DEPENDENCIES = $(subst src,$(DEPDIR),$(SOURCES:.c=.d))

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) $(TARGET_ARCH) -c

latency_bandwidth.tmp: minclude.sh latency_bandwidth/ext_mpi_bm.txt
	./minclude.sh latency_bandwidth/ext_mpi_bm.txt > latency_bandwidth.tmp

.deps/%.d: src/%.c latency_bandwidth.tmp
	$(COMPILE.c) $< -E > /dev/null

bin_tests/%.x: tests/%.c
	$(CC) $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) tests/$*.c $(LIBS)

bin/%.x: src/initial_benchmark/%.c
	$(CC) $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) src/initial_benchmark/$*.c $(LIBS)

DEPFILES := $(patsubst %.c,%.d,$(subst src,$(DEPDIR),$(SOURCES)))
$(DEPFILES):

include $(DEPFILES)

obj/noopt/%.o: .deps/noopt/%.d
	$(CC) -c $(CFLAGS) -O0 $(TARGET_ARCH) $(OUTPUT_OPTION) src/noopt/$*.c

obj/%.o: .deps/%.d
	$(CC) -c $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) src/$*.c

clean:
	rm $(OBJ)/core/*.o $(OBJ)/mpi/*.o $(OBJ)/gpu/*.o $(OBJ)/fortran/*.o $(OBJ)/noopt/*.o $(OBJ)/initial_benchmark/*.o lib/lib$(LIBNAME).a lib/lib$(LIBNAME).so $(BIN)/* bin_tests/* latency_bandwidth.tmp
