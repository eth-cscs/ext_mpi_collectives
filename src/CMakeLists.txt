add_subdirectory(core)
add_subdirectory(fortran)
add_subdirectory(mpi)

add_library(ext_mpi_collectives 
    $<TARGET_OBJECTS:core_obj>
    $<TARGET_OBJECTS:mpi_obj>
    $<TARGET_OBJECTS:fortran_obj>
)

set_target_properties(ext_mpi_collectives PROPERTIES PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/mpi/ext_mpi_interface.h")

install(TARGETS ext_mpi_collectives RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ext_mpi_collectives PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
