cmake_minimum_required(VERSION 3.16)
#project(Creds-BOF C)

set(CMAKE_C_STANDARD 99)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/_include
    ${CMAKE_CURRENT_SOURCE_DIR}/_include
#    ${CMAKE_CURRENT_SOURCE_DIR}/UnderlayCopy
)

# Source files
set(ASKCREDS_SOURCES askcreds/askcreds.c)
set(AUTOLOGON_SOURCES autologon/autologon.c)
set(CREDMAN_SOURCES credman/credentialmanager.c)
set(HASHDUMP_SOURCES hashdump/hashdump.c)
set(COOKIE_MONSTER_SOURCES cookie-monster/cookie-monster-bof.c)
set(GET_NETNTLM_SOURCES get-netntlm/get-netntlm.c)
set(UNDERLAYCOPY_SOURCES UnderlayCopy/entry.c)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -DBOF -w -Wno-int-conversion -Wno-incompatible-pointer-types")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# Object file targets
add_library(askcreds OBJECT ${ASKCREDS_SOURCES})
add_library(autologon OBJECT ${AUTOLOGON_SOURCES})
add_library(credman OBJECT ${CREDMAN_SOURCES})
add_library(hashdump OBJECT ${HASHDUMP_SOURCES})
add_library(cookie-monster OBJECT ${COOKIE_MONSTER_SOURCES})
add_library(get-netntlm OBJECT ${GET_NETNTLM_SOURCES})
add_library(underlaycopy OBJECT ${UNDERLAYCOPY_SOURCES})
target_compile_options(underlaycopy PRIVATE -masm=intel)

# Subdirectory (commented out - directory doesn't have CMakeLists.txt)
# add_subdirectory(nanodump)

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin)

# Custom commands to copy object files to _bin
add_custom_target(copy_askcreds ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:askcreds> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/askcreds.x64.o
    DEPENDS askcreds
    COMMENT "Copying askcreds object file"
)

add_custom_target(copy_autologon ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:autologon> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/autologon.x64.o
    DEPENDS autologon
    COMMENT "Copying autologon object file"
)

add_custom_target(copy_credman ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:credman> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/credman.x64.o
    DEPENDS credman
    COMMENT "Copying credman object file"
)

add_custom_target(copy_hashdump ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:hashdump> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/hashdump.x64.o
    DEPENDS hashdump
    COMMENT "Copying hashdump object file"
)

add_custom_target(copy_cookie-monster ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:cookie-monster> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/cookie-monster-bof.x64.o
    DEPENDS cookie-monster
    COMMENT "Copying cookie-monster object file"
)

add_custom_target(copy_get-netntlm ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:get-netntlm> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/get-netntlm.x64.o
    DEPENDS get-netntlm
    COMMENT "Copying get-netntlm object file"
)

add_custom_target(copy_underlaycopy ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:underlaycopy> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/underlaycopy.x64.o
    DEPENDS underlaycopy
    COMMENT "Copying underlaycopy object file"
)
