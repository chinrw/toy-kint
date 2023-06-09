cmake_minimum_required(VERSION 3.14)
include(FetchContent)
include(ExternalProject)

project(CS526-KINT)

set(LT_LLVM_INSTALL_DIR "" CACHE PATH "LLVM installation directory")
set(LT_LLVM_INCLUDE_DIR "${LT_LLVM_INSTALL_DIR}/include/llvm")
if(NOT EXISTS "${LT_LLVM_INCLUDE_DIR}")
message(FATAL_ERROR
  " LT_LLVM_INSTALL_DIR (${LT_LLVM_INCLUDE_DIR}) is invalid.")
endif()

# 1.2 Check that the LLVMConfig.cmake file exists (the location depends on the
# OS)
set(LT_VALID_INSTALLATION FALSE)

# Ubuntu + Darwin
if(EXISTS "${LT_LLVM_INSTALL_DIR}/lib/cmake/llvm/LLVMConfig.cmake")
  set(LT_VALID_INSTALLATION TRUE)
endif()

# Fedora
if(EXISTS "${LT_LLVM_INSTALL_DIR}/lib64/cmake/llvm/LLVMConfig.cmake")
  set(LT_VALID_INSTALLATION TRUE)
endif()

if(NOT ${LT_VALID_INSTALLATION})
  message(FATAL_ERROR
    "LLVM installation directory, (${LT_LLVM_INSTALL_DIR}), is invalid. Couldn't
    find LLVMConfig.cmake.")
endif()


set(CMAKE_CXX_STANDARD 17)
message("generator is set to ${CMAKE_GENERATOR}")
list(APPEND CMAKE_PREFIX_PATH "${LT_LLVM_INSTALL_DIR}/lib/cmake/llvm/")

find_package(LLVM 16 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LT_LLVM_INSTALL_DIR}")

message(
  "LLVM STATUS:
  Definitions ${LLVM_DEFINITIONS}
  Includes    ${LLVM_INCLUDE_DIRS}
  Libraries   ${LLVM_LIBRARY_DIRS}
  Targets     ${LLVM_TARGETS_TO_BUILD}")

# Set the LLVM header and library paths
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(LLVM-Config)
include(HandleLLVMOptions)
include(AddLLVM)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      Debug
      CACHE STRING "Build type (default Debug):" FORCE)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall\
  -fdiagnostics-color=always")

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# Set the build directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

# ===============================================================================
# 1. ADD SUB-TARGETS Doing this at the end so that all definitions and
#   link/include paths are available for the sub-projects.
# ===============================================================================
add_subdirectory(HelloWorld)
add_subdirectory(kint)
