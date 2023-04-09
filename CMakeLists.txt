cmake_minimum_required(VERSION 3.21)

project(libext_mpi_collectives C)

set(CMAKE_C_FLAGS_RELEASE "-w -g -O2 -DNDEBUG")
# set(CMAKE_CUDA_FLAGS_RELEASE "-O3 -DNDEBUG")

option(BUILD_TESTING "build tests" OFF)
option(GPU_DIRECT "Enable CUDA-aware MPI communication" OFF)

find_package(MPI REQUIRED)

# include(CTest)
include(CheckLanguage)

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

add_subdirectory(src)
# if (BUILD_TESTING)
#     include(setup_GTest)
# endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif(BUILD_TESTING)

# {{{ TODO: check_language(HIP)

# check_language(CUDA)
# if(CMAKE_CUDA_COMPILER)
#     enable_language(CUDA)
#     find_package(CUDAToolkit)
#     set(CMAKE_CUDA_STANDARD 17)
# endif()

# option(SPH_EXA_WITH_H5PART "Enable HDF5 IO using the H5Part library" ON)
# if (SPH_EXA_WITH_H5PART)
#     set(HDF5_PREFER_PARALLEL true)
#     find_package(HDF5)
#     if (HDF5_FOUND)
#         add_subdirectory(./extern/h5part)
#     else()
#         message(STATUS "No HDF5 support")
#         set(SPH_EXA_WITH_H5PART OFF)
#     endif ()
# endif ()
# }}}
