cmake_minimum_required(VERSION 3.16)
#project(Execution-BOF C)

set(CMAKE_C_STANDARD 99)

# Include directories (main _include is at root level)
include_directories(${CMAKE_SOURCE_DIR}/_include)

# Source files
set(EXECUTE_ASSEMBLY_SOURCES execute-assembly/inlineExecute-Assembly.c)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -w -Wno-incompatible-pointer-types")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# Object file targets
add_library(execute-assembly OBJECT ${EXECUTE_ASSEMBLY_SOURCES})

# Subdirectory (commented out - directory doesn't have CMakeLists.txt)
# add_subdirectory(No-Consolation)

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin)

# Custom command to copy object file to _bin
add_custom_target(copy_execute-assembly ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:execute-assembly> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/execute-assembly.x64.o
    DEPENDS execute-assembly
    COMMENT "Copying execute-assembly object file"
)
