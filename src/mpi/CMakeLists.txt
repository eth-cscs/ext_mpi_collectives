set(SOURCES_MPI
shmem.c
messages_shared_memory.c
hash_table.c
forward_interpreter.c
ext_mpi_native.c
ext_mpi_interface.c
ext_mpi.c
clean_barriers.c
backward_interpreter.c
)

# set(libname mpi_obj)
add_library(mpi_obj OBJECT ${SOURCES_MPI})
target_compile_definitions(mpi_obj PRIVATE M_MAP)
target_include_directories(mpi_obj PRIVATE 
    ${PROJECT_SOURCE_DIR}/include/core
    ${PROJECT_SOURCE_DIR}/include/mpi)
