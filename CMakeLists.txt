cmake_minimum_required(VERSION 3.28)
project(HolaCmake)

set(CMAKE_CXX_STANDARD 20)

# Set Adaptagrams root directory
set(ADAPTAGRAMS_DIR "${CMAKE_SOURCE_DIR}/adaptagrams/cola")

# Add include directories (headers)
include_directories(
        ${ADAPTAGRAMS_DIR}
        ${ADAPTAGRAMS_DIR}/libdialect  # Ensure this is present
)

# Add source files
set(SOURCES
        main.cpp
        adaptagrams/cola/libdialect/hola.cpp
        adaptagrams/cola/libdialect/graphs.cpp
        adaptagrams/cola/libdialect/logging.cpp
        adaptagrams/cola/libdialect/routing.cpp
        adaptagrams/cola/libdialect/nodeconfig.cpp
        adaptagrams/cola/libdialect/ortho.cpp
        adaptagrams/cola/libdialect/faces.cpp
        adaptagrams/cola/libdialect/nodes.cpp
        adaptagrams/cola/libdialect/constraints.cpp
        adaptagrams/cola/libdialect/expansion.cpp
        adaptagrams/cola/libdialect/peeling.cpp
        adaptagrams/cola/libdialect/treeplacement.cpp
        adaptagrams/cola/libcola/compound_constraints.cpp
        adaptagrams/cola/libdialect/aca.cpp
        adaptagrams/cola/libdialect/quadaction.cpp
        adaptagrams/cola/libcola/cola.cpp
        adaptagrams/cola/libcola/colafd.cpp
        adaptagrams/cola/libdialect/mytreeplacement.h
)

# Create an executable
add_executable(HolaCmake ${SOURCES})
set(CMAKE_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/adaptagrams/cola")

# Link against Adaptagrams libraries
target_link_libraries(HolaCmake PRIVATE cola vpsc avoid topology dialect)
