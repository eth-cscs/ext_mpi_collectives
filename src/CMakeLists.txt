add_subdirectory(core)
add_subdirectory(fortran)
add_subdirectory(mpi)
add_subdirectory(noopt)

if(EXT_MPI_GPU_DIRECT AND CMAKE_CUDA_COMPILER)
  message("-- CUDA ON ${CMAKE_CUDA_COMPILER}")
  # add_subdirectory(gpu)
  add_library(
    ext_mpi_collectives
    $<TARGET_OBJECTS:core_obj> $<TARGET_OBJECTS:mpi_obj> $<TARGET_OBJECTS:fortran_obj>
    $<${EXT_MPI_GPU_SUPPORT}:$<TARGET_OBJECTS:gpu_obj>
  )
  # target_link_libraries(ext_mpi_collectives CUDA::cudart)
else()
  add_library(
    ext_mpi_collectives $<TARGET_OBJECTS:core_obj> $<TARGET_OBJECTS:mpi_obj>
                        $<TARGET_OBJECTS:fortran_obj> $<TARGET_OBJECTS:noopt_obj>
  )
endif()

set_target_properties(
  ext_mpi_collectives PROPERTIES PUBLIC_HEADER
                                 "${PROJECT_SOURCE_DIR}/include/mpi/ext_mpi_interface.h"
)

install(TARGETS ext_mpi_collectives RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ext_mpi_collectives PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
