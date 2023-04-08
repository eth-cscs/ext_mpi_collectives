add_subdirectory(core)
add_subdirectory(fortran)
add_subdirectory(mpi)

add_library(ext_mpi_collectives 
    $<TARGET_OBJECTS:core_obj>
    $<TARGET_OBJECTS:mpi_obj>
    $<TARGET_OBJECTS:fortran_obj>
)

install(TARGETS ext_mpi_collectives RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})

# if (BUILD_TESTING)
#     add_subdirectory(test)
#     add_subdirectory(test/mpi)
# endif (BUILD_TESTING)
