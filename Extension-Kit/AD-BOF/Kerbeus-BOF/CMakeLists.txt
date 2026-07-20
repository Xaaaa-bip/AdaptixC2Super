cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 99)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/_include
    ${CMAKE_CURRENT_SOURCE_DIR}/_include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -w -Wno-incompatible-pointer-types -DBOF")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# Source files
set(HASH_SOURCES hash/hash.c)
set(KLIST_SOURCES klist/klist.c)
set(DESCRIBE_SOURCES describe/describe.c)
set(TGTDELEG_SOURCES tgtdeleg/tgtdeleg.c)
set(PTT_SOURCES ptt/ptt.c)
set(PURGE_SOURCES purge/purge.c)
set(ASKTGT_SOURCES asktgt/asktgt.c)
set(ASKTGS_SOURCES asktgs/asktgs.c)
set(RENEW_SOURCES renew/renew.c)
set(CHANGEPW_SOURCES changepw/changepw.c)
set(ASREPROASTING_SOURCES asreproasting/asreproasting.c)
set(KERBEROASTING_SOURCES kerberoasting/kerberoasting.c)
set(S4U_SOURCES s4u/s4u.c)
set(CROSS_S4U_SOURCES s4u/cross_s4u.c)

# Object file targets
add_library(kerb_hash OBJECT ${HASH_SOURCES})
add_library(kerb_klist OBJECT ${KLIST_SOURCES})
add_library(kerb_triage OBJECT ${KLIST_SOURCES})
target_compile_definitions(kerb_triage PRIVATE TRIAGE)
add_library(kerb_dump OBJECT ${KLIST_SOURCES})
target_compile_definitions(kerb_dump PRIVATE DUMP)
add_library(kerb_describe OBJECT ${DESCRIBE_SOURCES})
add_library(kerb_tgtdeleg OBJECT ${TGTDELEG_SOURCES})
add_library(kerb_ptt OBJECT ${PTT_SOURCES})
add_library(kerb_purge OBJECT ${PURGE_SOURCES})
add_library(kerb_asktgt OBJECT ${ASKTGT_SOURCES})
add_library(kerb_asktgs OBJECT ${ASKTGS_SOURCES})
add_library(kerb_renew OBJECT ${RENEW_SOURCES})
add_library(kerb_changepw OBJECT ${CHANGEPW_SOURCES})
add_library(kerb_asreproasting OBJECT ${ASREPROASTING_SOURCES})
add_library(kerb_kerberoasting OBJECT ${KERBEROASTING_SOURCES})
add_library(kerb_s4u OBJECT ${S4U_SOURCES})
add_library(kerb_cross_s4u OBJECT ${CROSS_S4U_SOURCES})

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF)

# Custom targets to copy object files to _bin
add_custom_target(copy_kerb_hash ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_hash> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/hash.x64.o
    DEPENDS kerb_hash
    COMMENT "Copying hash object file"
)

add_custom_target(copy_kerb_klist ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_klist> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/klist.x64.o
    DEPENDS kerb_klist
    COMMENT "Copying klist object file"
)

add_custom_target(copy_kerb_triage ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_triage> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/triage.x64.o
    DEPENDS kerb_triage
    COMMENT "Copying triage object file"
)

add_custom_target(copy_kerb_dump ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_dump> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/dump.x64.o
    DEPENDS kerb_dump
    COMMENT "Copying dump object file"
)

add_custom_target(copy_kerb_describe ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_describe> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/describe.x64.o
    DEPENDS kerb_describe
    COMMENT "Copying describe object file"
)

add_custom_target(copy_kerb_tgtdeleg ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_tgtdeleg> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/tgtdeleg.x64.o
    DEPENDS kerb_tgtdeleg
    COMMENT "Copying tgtdeleg object file"
)

add_custom_target(copy_kerb_ptt ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_ptt> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/ptt.x64.o
    DEPENDS kerb_ptt
    COMMENT "Copying ptt object file"
)

add_custom_target(copy_kerb_purge ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_purge> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/purge.x64.o
    DEPENDS kerb_purge
    COMMENT "Copying purge object file"
)

add_custom_target(copy_kerb_asktgt ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_asktgt> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/asktgt.x64.o
    DEPENDS kerb_asktgt
    COMMENT "Copying asktgt object file"
)

add_custom_target(copy_kerb_asktgs ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_asktgs> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/asktgs.x64.o
    DEPENDS kerb_asktgs
    COMMENT "Copying asktgs object file"
)

add_custom_target(copy_kerb_renew ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_renew> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/renew.x64.o
    DEPENDS kerb_renew
    COMMENT "Copying renew object file"
)

add_custom_target(copy_kerb_changepw ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_changepw> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/changepw.x64.o
    DEPENDS kerb_changepw
    COMMENT "Copying changepw object file"
)

add_custom_target(copy_kerb_asreproasting ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_asreproasting> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/asreproasting.x64.o
    DEPENDS kerb_asreproasting
    COMMENT "Copying asreproasting object file"
)

add_custom_target(copy_kerb_kerberoasting ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_kerberoasting> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/kerberoasting.x64.o
    DEPENDS kerb_kerberoasting
    COMMENT "Copying kerberoasting object file"
)

add_custom_target(copy_kerb_s4u ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_s4u> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/s4u.x64.o
    DEPENDS kerb_s4u
    COMMENT "Copying s4u object file"
)

add_custom_target(copy_kerb_cross_s4u ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:kerb_cross_s4u> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/Kerbeus-BOF/cross_s4u.x64.o
    DEPENDS kerb_cross_s4u
    COMMENT "Copying cross_s4u object file"
)
