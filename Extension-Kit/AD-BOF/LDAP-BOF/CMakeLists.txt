cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 99)

# Include directories
include_directories(
#    ${CMAKE_SOURCE_DIR}/_include
    ${CMAKE_CURRENT_SOURCE_DIR}/_include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -masm=intel -fno-stack-protector -mno-stack-arg-probe -DBOF")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

# GET operations
add_library(ldap_get_users OBJECT src/get/get-users.c)
add_library(ldap_get_computers OBJECT src/get/get-computers.c)
add_library(ldap_get_groups OBJECT src/get/get-groups.c)
add_library(ldap_get_usergroups OBJECT src/get/get-usergroups.c)
add_library(ldap_get_groupmembers OBJECT src/get/get-groupmembers.c)
add_library(ldap_get_object OBJECT src/get/get-object.c)
add_library(ldap_get_domaininfo OBJECT src/get/get-domaininfo.c)
add_library(ldap_get_maq OBJECT src/get/get-maq.c)
add_library(ldap_get_writable OBJECT src/get/get-writable.c)
add_library(ldap_get_delegation OBJECT src/get/get-delegation.c)
add_library(ldap_get_uac OBJECT src/get/get-uac.c)
add_library(ldap_get_attribute OBJECT src/get/get-attribute.c)
add_library(ldap_get_spn OBJECT src/get/get-spn.c)
add_library(ldap_get_acl OBJECT src/get/get-acl.c)
add_library(ldap_get_rbcd OBJECT src/get/get-rbcd.c)

# ADD operations
add_library(ldap_add_user OBJECT src/add/add-user.c)
add_library(ldap_add_computer OBJECT src/add/add-computer.c)
add_library(ldap_add_group OBJECT src/add/add-group.c)
add_library(ldap_add_groupmember OBJECT src/add/add-groupmember.c)
add_library(ldap_add_ou OBJECT src/add/add-ou.c)
add_library(ldap_add_sidhistory OBJECT src/add/add-sidhistory.c)
add_library(ldap_add_spn OBJECT src/add/add-spn.c)
add_library(ldap_add_attribute OBJECT src/add/add-attribute.c)
add_library(ldap_add_uac OBJECT src/add/add-uac.c)
add_library(ldap_add_delegation OBJECT src/add/add-delegation.c)
add_library(ldap_add_rbcd OBJECT src/add/add-rbcd.c)
add_library(ldap_add_ace OBJECT src/add/add-ace.c)

# SET operations
add_library(ldap_set_password OBJECT src/set/set-password.c)
add_library(ldap_set_spn OBJECT src/set/set-spn.c)
add_library(ldap_set_delegation OBJECT src/set/set-delegation.c)
add_library(ldap_set_attribute OBJECT src/set/set-attribute.c)
add_library(ldap_set_uac OBJECT src/set/set-uac.c)
add_library(ldap_set_owner OBJECT src/set/set-owner.c)

# MOVE operations
add_library(ldap_move_object OBJECT src/move/move-object.c)

# REMOVE operations
add_library(ldap_remove_groupmember OBJECT src/remove/remove-groupmember.c)
add_library(ldap_remove_object OBJECT src/remove/remove-object.c)
add_library(ldap_remove_delegation OBJECT src/remove/remove-delegation.c)
add_library(ldap_remove_spn OBJECT src/remove/remove-spn.c)
add_library(ldap_remove_attribute OBJECT src/remove/remove-attribute.c)
add_library(ldap_remove_rbcd OBJECT src/remove/remove-rbcd.c)
add_library(ldap_remove_ace OBJECT src/remove/remove-ace.c)
add_library(ldap_remove_uac OBJECT src/remove/remove-uac.c)

# Create _bin directory
file(MAKE_DIRECTORY ../_bin/LDAP)

# Macro for copy targets
macro(ldap_copy_target target_name output_name)
    add_custom_target(copy_${target_name} ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:${target_name}> ../_bin/LDAP/${output_name}.x64.o
        DEPENDS ${target_name}
        COMMENT "Copying ${output_name} object file"
    )
endmacro()

# GET copy targets
ldap_copy_target(ldap_get_users get-users)
ldap_copy_target(ldap_get_computers get-computers)
ldap_copy_target(ldap_get_groups get-groups)
ldap_copy_target(ldap_get_usergroups get-usergroups)
ldap_copy_target(ldap_get_groupmembers get-groupmembers)
ldap_copy_target(ldap_get_object get-object)
ldap_copy_target(ldap_get_domaininfo get-domaininfo)
ldap_copy_target(ldap_get_maq get-maq)
ldap_copy_target(ldap_get_writable get-writable)
ldap_copy_target(ldap_get_delegation get-delegation)
ldap_copy_target(ldap_get_uac get-uac)
ldap_copy_target(ldap_get_attribute get-attribute)
ldap_copy_target(ldap_get_spn get-spn)
ldap_copy_target(ldap_get_acl get-acl)
ldap_copy_target(ldap_get_rbcd get-rbcd)

# ADD copy targets
ldap_copy_target(ldap_add_user add-user)
ldap_copy_target(ldap_add_computer add-computer)
ldap_copy_target(ldap_add_group add-group)
ldap_copy_target(ldap_add_groupmember add-groupmember)
ldap_copy_target(ldap_add_ou add-ou)
ldap_copy_target(ldap_add_sidhistory add-sidhistory)
ldap_copy_target(ldap_add_spn add-spn)
ldap_copy_target(ldap_add_attribute add-attribute)
ldap_copy_target(ldap_add_uac add-uac)
ldap_copy_target(ldap_add_delegation add-delegation)
ldap_copy_target(ldap_add_rbcd add-rbcd)
ldap_copy_target(ldap_add_ace add-ace)

# SET copy targets
ldap_copy_target(ldap_set_password set-password)
ldap_copy_target(ldap_set_spn set-spn)
ldap_copy_target(ldap_set_delegation set-delegation)
ldap_copy_target(ldap_set_attribute set-attribute)
ldap_copy_target(ldap_set_uac set-uac)
ldap_copy_target(ldap_set_owner set-owner)

# MOVE copy targets
ldap_copy_target(ldap_move_object move-object)

# REMOVE copy targets
ldap_copy_target(ldap_remove_groupmember remove-groupmember)
ldap_copy_target(ldap_remove_object remove-object)
ldap_copy_target(ldap_remove_delegation remove-delegation)
ldap_copy_target(ldap_remove_spn remove-spn)
ldap_copy_target(ldap_remove_attribute remove-attribute)
ldap_copy_target(ldap_remove_rbcd remove-rbcd)
ldap_copy_target(ldap_remove_ace remove-ace)
ldap_copy_target(ldap_remove_uac remove-uac)
