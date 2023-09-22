cmake_minimum_required(VERSION 3.21)

project(
  libext_mpi_collectives
  VERSION 0.1
  DESCRIPTION "Optimized MPI Collectives"
  LANGUAGES C
)

# ------------------------------------------------------------------------------
# Language support (CUDA/Fortran)
# ------------------------------------------------------------------------------
include(CheckLanguage)

# ------------------------------------------------------------------------------
# Policies
# ------------------------------------------------------------------------------
function(set_policy POLICY_NAME POLICY_VALUE)
  if(POLICY ${POLICY_NAME})
    cmake_policy(SET ${POLICY_NAME} ${POLICY_VALUE})
  endif()
endfunction()

set_policy(CMP0028 NEW) # Double colon in target name means ALIAS or IMPORTED target
set_policy(CMP0060 NEW) # Link libraries by full path even in implicit directories
set_policy(CMP0074 NEW) # find_package uses XXX_ROOT vars using PackageName
set_policy(CMP0144 NEW) # find_package uses XXX_ROOT vars using PACKAGENAME Uppercase

# ------------------------------------------------------------------------------
# CMake find modules
# ------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------------------
# Setup Output directories
# ------------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/bin
    CACHE PATH "Single Directory for all Executables."
)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/lib
    CACHE PATH "Single Directory for all Libraries"
)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/lib
    CACHE PATH "Single Directory for all static libraries."
)

# ------------------------------------------------------------------------------
# Default build type
# ------------------------------------------------------------------------------
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE
      "${default_build_type}"
      CACHE STRING "Choose the type of build." FORCE
  )
  set_property(
    CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo"
  )
endif()

# ------------------------------------------------------------------------------
# CTest
# ------------------------------------------------------------------------------
include(CTest)
enable_testing()

# ------------------------------------------------------------------------------
# MPI
# ------------------------------------------------------------------------------
find_package(MPI REQUIRED)

# ------------------------------------------------------------------------------
# GPU targets
# ------------------------------------------------------------------------------
option(EXT_MPI_GPU_SUPPORT "Enable GPU support" OFF)
if(EXT_MPI_GPU_SUPPORT)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit)
    option(EXT_MPI_GPU_DIRECT "Enable CUDA-aware MPI communication" OFF)
  endif()
endif()

# ------------------------------------------------------------------------------
# GENERATED files : Setup default parameters from benchmark
# ------------------------------------------------------------------------------
# cat latency_bandwidth/ext_mpi_nst.txt > node_size_threshold.tmp ./minclude.sh
# latency_bandwidth/ext_mpi_bm.txt > latency_bandwidth.tmp

file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/generated)

add_custom_command(
  OUTPUT ${PROJECT_BINARY_DIR}/generated/node_size_threshold.tmp
  COMMAND cat ${PROJECT_SOURCE_DIR}/latency_bandwidth/ext_mpi_nst.txt >
          ${PROJECT_BINARY_DIR}/generated/node_size_threshold.tmp
  DEPENDS ${PROJECT_SOURCE_DIR}/latency_bandwidth/ext_mpi_nst.txt
  COMMENT "running generator for node_size_threshold.tmp"
)

add_custom_command(
  OUTPUT ${PROJECT_BINARY_DIR}/generated/latency_bandwidth.tmp
  COMMAND ${PROJECT_SOURCE_DIR}/minclude.sh ${PROJECT_SOURCE_DIR}/latency_bandwidth/ext_mpi_bm.txt >
          ${PROJECT_BINARY_DIR}/generated/latency_bandwidth.tmp
  DEPENDS ${PROJECT_SOURCE_DIR}/latency_bandwidth/ext_mpi_bm.txt
  DEPENDS ${PROJECT_SOURCE_DIR}/minclude.sh
  COMMENT "running generator for latency_bandwidth.tmp"
)
add_custom_target(
  run_generator ALL
  DEPENDS ${PROJECT_BINARY_DIR}/generated/node_size_threshold.tmp
  DEPENDS ${PROJECT_BINARY_DIR}/generated/latency_bandwidth.tmp
)

# ------------------------------------------------------------------------------
# Main targets
# ------------------------------------------------------------------------------
add_subdirectory(src)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
  add_subdirectory(tests)
endif(BUILD_TESTING)
