cmake_minimum_required(VERSION 3.16)
#project(LateralMovement-BOF C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

# Include directories (main _include is at root level)
include_directories(${CMAKE_SOURCE_DIR}/_include)

# Source files
set(PSEXEC_SOURCES psexec/psexec.c)
set(SCSHELL_SOURCES scshell/scshell.c)
set(TOKEN_MAKE_SOURCES token_make/token_make.c)
set(TOKEN_STEAL_SOURCES token_steal/token_steal.c)
set(WINRM_SOURCES winrm-client/winrm.cpp)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -w -Wno-int-conversion -Wno-incompatible-pointer-types")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -w")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

# Object file targets
add_library(psexec OBJECT ${PSEXEC_SOURCES})
add_library(scshell OBJECT ${SCSHELL_SOURCES})
add_library(token_make OBJECT ${TOKEN_MAKE_SOURCES})
add_library(token_steal OBJECT ${TOKEN_STEAL_SOURCES})

add_library(winrm OBJECT ${WINRM_SOURCES})
target_compile_options(winrm PRIVATE
    -masm=intel
    -nostdlib
    -Wno-nonnull
    -fno-asynchronous-unwind-tables
    -fno-exceptions
    -fno-rtti
)

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin)

# Custom commands to copy object files to _bin
add_custom_target(copy_psexec ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:psexec> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/psexec.x64.o
    DEPENDS psexec
    COMMENT "Copying psexec object file"
)

add_custom_target(copy_scshell ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:scshell> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/scshell.x64.o
    DEPENDS scshell
    COMMENT "Copying scshell object file"
)

add_custom_target(copy_token_make ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:token_make> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/token_make.x64.o
    DEPENDS token_make
    COMMENT "Copying token_make object file"
)

add_custom_target(copy_token_steal ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:token_steal> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/token_steal.x64.o
    DEPENDS token_steal
    COMMENT "Copying token_steal object file"
)

add_custom_target(copy_winrm ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:winrm> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/winrm.x64.o
    DEPENDS winrm
    COMMENT "Copying winrm object file"
)
