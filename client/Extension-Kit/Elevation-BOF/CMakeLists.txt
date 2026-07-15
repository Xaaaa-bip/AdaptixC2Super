cmake_minimum_required(VERSION 3.16)
#project(Elevation-BOF C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

# Include directories (main _include is at root level)
include_directories(${CMAKE_SOURCE_DIR}/_include)

# Source files
set(GETSYSTEM_TOKEN_SOURCES getsystem_token/getsystem_token.c)
set(UAC_REGSHELLCMD_SOURCES uac_regshellcmd/RegistryShellCommandBOF.c)
set(PRINTSPOOFER_SOURCES printspoofer/printspoofer.c)
set(UAC_SSPI_SOURCES uac_sspi/SspiUacBypassBOF.cpp)
set(DCOMPOTATO_SOURCES potato-dcom/DCOMPotato.cpp)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -w -Wno-int-conversion -Wno-incompatible-pointer-types")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -w")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

# Object file targets
add_library(getsystem_token OBJECT ${GETSYSTEM_TOKEN_SOURCES})
add_library(uac_regshellcmd OBJECT ${UAC_REGSHELLCMD_SOURCES})
add_library(printspoofer OBJECT ${PRINTSPOOFER_SOURCES})
target_compile_definitions(printspoofer PRIVATE BOF)

add_library(uac_sspi OBJECT ${UAC_SSPI_SOURCES})

add_library(DCOMPotato OBJECT ${DCOMPOTATO_SOURCES})
target_compile_options(DCOMPotato PRIVATE
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
add_custom_target(copy_getsystem_token ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:getsystem_token> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/getsystem_token.x64.o
    DEPENDS getsystem_token
    COMMENT "Copying getsystem_token object file"
)

add_custom_target(copy_uac_regshellcmd ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:uac_regshellcmd> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/uac_regshellcmd.x64.o
    DEPENDS uac_regshellcmd
    COMMENT "Copying uac_regshellcmd object file"
)

add_custom_target(copy_printspoofer ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:printspoofer> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/printspoofer.x64.o
    DEPENDS printspoofer
    COMMENT "Copying printspoofer object file"
)

add_custom_target(copy_uac_sspi ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:uac_sspi> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/uac_sspi.x64.o
    DEPENDS uac_sspi
    COMMENT "Copying uac_sspi object file"
)

add_custom_target(copy_DCOMPotato ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:DCOMPotato> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/DCOMPotato.x64.o
    DEPENDS DCOMPotato
    COMMENT "Copying DCOMPotato object file"
)
