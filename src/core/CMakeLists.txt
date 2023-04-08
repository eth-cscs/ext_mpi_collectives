set(SOURCES_CORE
waitany.c
use_recvbuf.c
reduce_copyout.c
reduce_copyin.c
read_write.c
read_bench.c
raw_code_tasks_node_master.c
raw_code_tasks_node.c
raw_code_merge.c
raw_code.c
rank_permutation.c
prime_factors.c
ports_groups.c
parallel_memcpy.c
optimise_buffers.c
optimise_buffers2.c
no_socket_barriers.c
no_offset.c
no_first_barrier.c
move_first_memcpy.c
dummy.c
count_instructions.c
cost_simulation.c
cost_simple_recursive.c
cost_estimation.c
byte_code.c
buffer_offset.c
alltoall.c
allreduce_single.c
allreduce.c    
)

# set(exename sphexa)
# add_executable(${exename} sphexa.cpp)
add_library(core_obj OBJECT ${SOURCES_CORE})
target_compile_definitions(core_obj PRIVATE M_MAP)
target_include_directories(core_obj PRIVATE 
    # src/core/read_bench.c includes: node_size_threshold.tmp and latency_bandwidth.tmp
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include/core
    ${PROJECT_SOURCE_DIR}/include/mpi)

# {{{
# enableH5Part(${exename})
# enableInSituViz(${exename})
# enableGrackle(${exename})
# install(TARGETS ${exename} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# if (CMAKE_CUDA_COMPILER)
#     add_executable(${exename}-cuda sphexa.cpp)
#     target_include_directories(${exename}-cuda PRIVATE ${SPH_EXA_INCLUDE_DIRS})
#     target_include_directories(${exename}-cuda PUBLIC ${CMAKE_BINARY_DIR}/main/src)
#     target_compile_definitions(${exename}-cuda PRIVATE USE_CUDA)
#     target_link_libraries(${exename}-cuda PRIVATE cstone_gpu ryoanji sph_gpu observables_gpu OpenMP::OpenMP_CXX
#             ${MPI_CXX_LIBRARIES} CUDA::cudart)
#     enableH5Part(${exename}-cuda)
#     enableInSituViz(${exename}-cuda)
#     enableGpuDirect(${exename}-cuda)
#     enableGrackle(${exename}-cuda)
#     install(TARGETS ${exename}-cuda RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# 
# elseif (CMAKE_HIP_COMPILER)
#     add_executable(${exename}-hip sphexa.cpp)
#     target_include_directories(${exename}-hip PRIVATE ${SPH_EXA_INCLUDE_DIRS})
#     target_compile_definitions(${exename}-hip PRIVATE USE_CUDA THRUST_DEVICE_SYSTEM=THRUST_DEVICE_SYSTEM_HIP)
#     target_link_libraries(${exename}-hip PRIVATE cstone_gpu ryoanji sph_gpu observables_gpu OpenMP::OpenMP_CXX
#             ${MPI_CXX_LIBRARIES} hip::host)
#     set_target_properties(${exename}-hip PROPERTIES LINKER_LANGUAGE CXX)
#     enableH5Part(${exename}-hip)
#     enableInSituViz(${exename}-hip)
#     enableGpuDirect(${exename}-hip)
#     enableGrackle(${exename}-hip)
#     install(TARGETS ${exename}-hip RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# endif ()
# }}}
