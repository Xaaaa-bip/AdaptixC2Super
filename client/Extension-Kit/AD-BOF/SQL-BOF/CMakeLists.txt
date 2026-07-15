cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 99)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/_include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -w -Wno-incompatible-pointer-types -DBOF")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# SQL BOF targets
add_library(sql_1434udp OBJECT src/SQL/1434udp/entry.c)
add_library(sql_adsi OBJECT src/SQL/adsi/entry.c)
add_library(sql_agentcmd OBJECT src/SQL/agentcmd/entry.c)
add_library(sql_agentstatus OBJECT src/SQL/agentstatus/entry.c)
add_library(sql_checkrpc OBJECT src/SQL/checkrpc/entry.c)
add_library(sql_clr OBJECT src/SQL/clr/entry.c)
add_library(sql_columns OBJECT src/SQL/columns/entry.c)
add_library(sql_databases OBJECT src/SQL/databases/entry.c)
add_library(sql_togglemodule OBJECT src/SQL/togglemodule/entry.c)
add_library(sql_impersonate OBJECT src/SQL/impersonate/entry.c)
add_library(sql_info OBJECT src/SQL/info/entry.c)
add_library(sql_links OBJECT src/SQL/links/entry.c)
add_library(sql_olecmd OBJECT src/SQL/olecmd/entry.c)
add_library(sql_query OBJECT src/SQL/query/entry.c)
add_library(sql_rows OBJECT src/SQL/rows/entry.c)
add_library(sql_search OBJECT src/SQL/search/entry.c)
add_library(sql_smb OBJECT src/SQL/smb/entry.c)
add_library(sql_tables OBJECT src/SQL/tables/entry.c)
add_library(sql_users OBJECT src/SQL/users/entry.c)
add_library(sql_whoami OBJECT src/SQL/whoami/entry.c)
add_library(sql_xpcmd OBJECT src/SQL/xpcmd/entry.c)

# Create _bin directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/_bin/SQL)

# Macro for copy targets
macro(sql_copy_target target_name output_name)
    add_custom_target(copy_${target_name} ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:${target_name}> ${CMAKE_CURRENT_SOURCE_DIR}/_bin/SQL/${output_name}.x64.o
        DEPENDS ${target_name}
        COMMENT "Copying ${output_name} object file"
    )
endmacro()

# Copy targets
sql_copy_target(sql_1434udp 1434udp)
sql_copy_target(sql_adsi adsi)
sql_copy_target(sql_agentcmd agentcmd)
sql_copy_target(sql_agentstatus agentstatus)
sql_copy_target(sql_checkrpc checkrpc)
sql_copy_target(sql_clr clr)
sql_copy_target(sql_columns columns)
sql_copy_target(sql_databases databases)
sql_copy_target(sql_togglemodule togglemodule)
sql_copy_target(sql_impersonate impersonate)
sql_copy_target(sql_info info)
sql_copy_target(sql_links links)
sql_copy_target(sql_olecmd olecmd)
sql_copy_target(sql_query query)
sql_copy_target(sql_rows rows)
sql_copy_target(sql_search search)
sql_copy_target(sql_smb smb)
sql_copy_target(sql_tables tables)
sql_copy_target(sql_users users)
sql_copy_target(sql_whoami whoami)
sql_copy_target(sql_xpcmd xpcmd)
