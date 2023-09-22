set(SOURCES_MPI
    backward_interpreter.c
    clean_barriers.c
    cost_copyin_measurement.c
    ext_mpi.c
    ext_mpi_interface.c
    ext_mpi_native.c
    forward_interpreter.c
    hash_table_blocking.c
    hash_table.c
    messages_shared_memory.c
    padding_factor.c
    shmem.c
)

# set(libname mpi_obj)
add_library(mpi_obj OBJECT ${SOURCES_MPI})
target_compile_definitions(mpi_obj PRIVATE M_MAP)
target_include_directories(
  mpi_obj PRIVATE ${PROJECT_SOURCE_DIR}/include/core ${PROJECT_SOURCE_DIR}/include/mpi
)
