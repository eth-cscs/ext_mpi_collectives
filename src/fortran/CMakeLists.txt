set(SOURCES_FORTRAN ext_mpi_f.c)

add_library(fortran_obj OBJECT ${SOURCES_FORTRAN})
target_compile_definitions(fortran_obj PRIVATE M_MAP)
target_include_directories(
  fortran_obj PRIVATE ${PROJECT_SOURCE_DIR}/include/core ${PROJECT_SOURCE_DIR}/include/mpi
)
