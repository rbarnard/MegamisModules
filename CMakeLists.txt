cmake_minimum_required(VERSION 3.10)

project(ClockSync)

# Specify C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_VERBOSE_MAKEFILE ON)

# Setup build flags based on plugin.mk
set(RACK_DIR ../..)

set(CMAKE_CXX_FLAGS -fPIC)
set(CMAKE_CXX_CREATE_SHARED_LIBRARY True)

include_directories(${RACK_DIR}/include)
include_directories(${RACK_DIR}/dep/include)

# TODO: Is there a canonical way to specify a list of dependencies for a custom target?
#set(SOURCE_FILES
#        src/plugin.cpp src/plugin.hpp)
add_library(plugin
        src/plugin.cpp src/plugin.hpp
        src/ClockSync.cpp
        )

#add_custom_target(
add_custom_command(
#        ${PROJECT_NAME}
        TARGET plugin
        COMMAND make
#        DEPENDS plugin
        BYPRODUCTS ${CMAKE_CURRENT_LIST_DIR}/plugin.dylib
        COMMENT "Creating plugin.dylib"
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)
