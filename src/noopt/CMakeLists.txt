set(SOURCES_noopt read_bench.c)

add_library(noopt_obj OBJECT ${SOURCES_noopt})
target_compile_definitions(noopt_obj PRIVATE M_MAP)

# latency_bandwidth.tmp is generated in ${PROJECT_BINARY_DIR}/generated
target_include_directories(
  noopt_obj PRIVATE ${PROJECT_SOURCE_DIR}/include/core ${PROJECT_SOURCE_DIR}/include/mpi
                    ${PROJECT_BINARY_DIR}/generated
)
