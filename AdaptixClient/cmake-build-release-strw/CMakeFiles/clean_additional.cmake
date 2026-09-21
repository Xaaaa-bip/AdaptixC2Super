# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "AdaptixClient_autogen"
  "CMakeFiles\\AdaptixClient_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\AdaptixClient_autogen.dir\\ParseCache.txt"
  "Libs\\kddockwidgets\\CMakeFiles\\kddockwidgets_autogen.dir\\AutogenUsed.txt"
  "Libs\\kddockwidgets\\CMakeFiles\\kddockwidgets_autogen.dir\\ParseCache.txt"
  "Libs\\kddockwidgets\\kddockwidgets_autogen"
  "Libs\\qlementine\\lib\\CMakeFiles\\qlementine_autogen.dir\\AutogenUsed.txt"
  "Libs\\qlementine\\lib\\CMakeFiles\\qlementine_autogen.dir\\ParseCache.txt"
  "Libs\\qlementine\\lib\\qlementine_autogen"
  )
endif()
