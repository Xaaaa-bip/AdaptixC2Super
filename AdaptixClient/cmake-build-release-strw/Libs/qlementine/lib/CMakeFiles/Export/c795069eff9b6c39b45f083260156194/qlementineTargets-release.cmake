#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "qlementine::qlementine" for configuration "Release"
set_property(TARGET qlementine::qlementine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(qlementine::qlementine PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libqlementine.a"
  )

list(APPEND _cmake_import_check_targets qlementine::qlementine )
list(APPEND _cmake_import_check_files_for_qlementine::qlementine "${_IMPORT_PREFIX}/lib/libqlementine.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
