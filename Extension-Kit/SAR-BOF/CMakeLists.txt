cmake_minimum_required(VERSION 3.16)
#project(SAR-BOF C)

set(CMAKE_C_STANDARD 99)

# Include directories (main _include is at root level)
include_directories(${CMAKE_SOURCE_DIR}/_include)

# Source files
set(SMARTSCAN_SOURCES smartscan/portscan_simple.c)
set(TASKHOUND_SOURCES taskhound/taskhound.c)
set(QUSER_SOURCES quser/quser.c)
set(NBTSCAN_SOURCES nbtscan/nbtscan.c)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -DBOF")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# Object file targets
add_library(smartscan OBJECT ${SMARTSCAN_SOURCES})

add_library(taskhound OBJECT ${TASKHOUND_SOURCES})
target_compile_options(taskhound PRIVATE -mno-stack-arg-probe)

add_library(quser OBJECT ${QUSER_SOURCES})

add_library(nbtscan OBJECT ${NBTSCAN_SOURCES})

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin)

# Custom commands to copy object files to _bin
add_custom_target(copy_smartscan ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:smartscan> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/smartscan.x64.o
    DEPENDS smartscan
    COMMENT "Copying smartscan object file"
)

add_custom_target(copy_taskhound ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:taskhound> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/taskhound.x64.o
    DEPENDS taskhound
    COMMENT "Copying taskhound object file"
)

add_custom_target(copy_quser ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:quser> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/quser.x64.o
    DEPENDS quser
    COMMENT "Copying quser object file"
)

add_custom_target(copy_nbtscan ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:nbtscan> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/nbtscan.x64.o
        DEPENDS nbtscan
        COMMENT "Copying nbtscan object file"
)