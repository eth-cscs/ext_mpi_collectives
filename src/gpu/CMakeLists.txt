set(SOURCES_GPU cuda_core.cu cuda_gemv.c cuda_shmem.c
                # hip_core.cpp hip_shmem.c
)

add_library(gpu_obj OBJECT ${SOURCES_GPU})
set_target_properties(gpu_obj PROPERTIES CUDA_ARCHITECTURES "60;80")
target_compile_definitions(gpu_obj PRIVATE M_MAP GPU_ENABLED)
target_include_directories(
  gpu_obj PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include/gpu
                  ${PROJECT_SOURCE_DIR}/include/core ${PROJECT_SOURCE_DIR}/include/mpi
)
