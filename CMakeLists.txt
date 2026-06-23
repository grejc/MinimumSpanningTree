cmake_minimum_required(VERSION 3.10)
project(MinSpanningTree C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_options(-Wall -Wextra -O3)

# Find MPI
find_package(MPI REQUIRED)

# Executable for Sequential version
add_executable(JoseCarlos_RGM42426_AGMseq JoseCarlos_RGM42426_AGMseq.c)

# Executable for Parallel version (MPI)
add_executable(JoseCarlos_RGM42426_AGMpar JoseCarlos_RGM42426_AGMpar.c)
target_link_libraries(JoseCarlos_RGM42426_AGMpar PRIVATE MPI::MPI_C)
