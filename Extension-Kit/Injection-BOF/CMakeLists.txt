cmake_minimum_required(VERSION 3.16)
#project(Injection-BOF C)

set(CMAKE_C_STANDARD 99)

# Include directories (main _include is at root level)
include_directories(${CMAKE_SOURCE_DIR}/_include)

# Source files
set(INJECT_CFG_SOURCES inject_cfg/inject_cfg.c)
set(INJECT_SEC_SOURCES inject_sec/inject_sec.c)
set(INJECT_POOLPARTY_SOURCES inject_poolparty/inject_poolparty.c)
set(INJECT_32TO64_SOURCES inject_32to64/inject_32to64.c)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# Object file targets
add_library(inject_cfg OBJECT ${INJECT_CFG_SOURCES})
target_compile_options(inject_cfg PRIVATE -masm=intel -Wall)

add_library(inject_sec OBJECT ${INJECT_SEC_SOURCES})
target_compile_options(inject_sec PRIVATE -w -Wno-int-conversion -Wno-incompatible-pointer-types)
target_compile_definitions(inject_sec PRIVATE BOF)

add_library(inject_poolparty OBJECT ${INJECT_POOLPARTY_SOURCES})
target_compile_options(inject_poolparty PRIVATE -w -Wno-int-conversion -Wno-incompatible-pointer-types)
target_compile_definitions(inject_poolparty PRIVATE BOF)

# x86 target
add_library(inject_32to64 OBJECT ${INJECT_32TO64_SOURCES})
target_compile_options(inject_32to64 PRIVATE -w -Wno-int-conversion -Wno-incompatible-pointer-types)
target_compile_definitions(inject_32to64 PRIVATE BOF)

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin)

# Custom commands to copy object files to _bin
add_custom_target(copy_inject_cfg ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:inject_cfg> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/inject_cfg.x64.o
    DEPENDS inject_cfg
    COMMENT "Copying inject_cfg object file"
)

add_custom_target(copy_inject_sec ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:inject_sec> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/inject_sec.x64.o
    DEPENDS inject_sec
    COMMENT "Copying inject_sec object file"
)

add_custom_target(copy_inject_poolparty ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:inject_poolparty> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/inject_poolparty.x64.o
    DEPENDS inject_poolparty
    COMMENT "Copying inject_poolparty object file"
)

add_custom_target(copy_inject_32to64 ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:inject_32to64> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/inject_32to64.x86.o
    DEPENDS inject_32to64
    COMMENT "Copying inject_32to64 object file"
)
