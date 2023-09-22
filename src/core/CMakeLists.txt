set(SOURCES_CORE
    allreduce.c
    allreduce_recursive.c
    allreduce_recursive_shrink.c
    allreduce_short.c
    allreduce_short_single.c
    allreduce_single.c
    alltoall.c
    buffer_offset.c
    byte_code.c
    CMakeLists.txt
    cost_estimation.c
    cost_simple_recursive.c
    cost_simulation.c
    count_instructions.c
    dummy.c
    move_first_memcpy.c
    no_first_barrier.c
    no_offset.c
    no_socket_barriers.c
    optimise_buffers2.c
    optimise_buffers.c
    optimise_multi_socket.c
    parallel_memcpy.c
    ports_groups.c
    prime_factors.c
    rank_permutation.c
    raw_code.c
    raw_code_merge.c
    raw_code_tasks_node.c
    raw_code_tasks_node_master.c
    read_write.c
    recursive_factors.c
    reduce_copyin.c
    reduce_copyout.c
    reduce_scatter_single_node.c
    swap_copyin.c
    use_recvbuf.c
    use_sendbuf_recvbuf.c
    waitany.c
)

add_library(core_obj OBJECT ${SOURCES_CORE})
target_compile_definitions(core_obj PRIVATE M_MAP)

# latency_bandwidth.tmp is generated in ${PROJECT_BINARY_DIR}/generated
target_include_directories(
  core_obj PRIVATE ${PROJECT_SOURCE_DIR}/include/core ${PROJECT_SOURCE_DIR}/include/mpi
                   ${PROJECT_BINARY_DIR}/generated
)
target_link_libraries(core_obj PUBLIC m MPI::MPI_C)
