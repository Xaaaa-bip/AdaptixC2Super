cmake_minimum_required(VERSION 3.16)
#project(AD-BOF C)

set(CMAKE_C_STANDARD 99)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/_include
#    ${CMAKE_CURRENT_SOURCE_DIR}/Kerbeus-BOF/_include
#    ${CMAKE_CURRENT_SOURCE_DIR}/SQL-BOF/common
#    ${CMAKE_CURRENT_SOURCE_DIR}/LDAP-BOF/_include
#    ${CMAKE_CURRENT_SOURCE_DIR}/LDAP-BOF/src/common
)

# Source files
set(LDAPSEARCH_SOURCES ldapsearch/ldapsearch.c)
set(ADWSSEARCH_SOURCES adwssearch/adws_search.c)
set(READLAPS_SOURCES readlaps/readlaps.c)
set(BADTAKEOVER_SOURCES badtakeover/BadTakeover.c)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -masm=intel -DBOF")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# Object file targets
add_library(ldapsearch OBJECT ${LDAPSEARCH_SOURCES})
add_library(adws_search OBJECT ${ADWSSEARCH_SOURCES})
add_library(readlaps OBJECT ${READLAPS_SOURCES})

# BadTakeover with special flags
add_library(badtakeover OBJECT ${BADTAKEOVER_SOURCES})
target_compile_options(badtakeover PRIVATE
    -fno-asynchronous-unwind-tables
    -nostdlib
    -fno-ident
    -fpack-struct=8
    -falign-functions=1
    -ffunction-sections
    -falign-jumps=1
    -Wall
)

# Subdirectories
# add_subdirectory(ADCS-BOF)
add_subdirectory(Kerbeus-BOF)
add_subdirectory(SQL-BOF)
add_subdirectory(LDAP-BOF)

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin)

# Custom targets to copy object files to _bin (using custom_target instead of custom_command for OBJECT libraries)
add_custom_target(copy_ldapsearch ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:ldapsearch> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/ldapsearch.x64.o
    DEPENDS ldapsearch
    COMMENT "Copying ldapsearch object file"
)

add_custom_target(copy_adws_search ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:adws_search> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/adws_search.x64.o
    DEPENDS adws_search
    COMMENT "Copying adws_search object file"
)

add_custom_target(copy_readlaps ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:readlaps> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/readlaps.x64.o
    DEPENDS readlaps
    COMMENT "Copying readlaps object file"
)

add_custom_target(copy_badtakeover ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:badtakeover> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/badtakeover.x64.o
    DEPENDS badtakeover
    COMMENT "Copying badtakeover object file"
)
