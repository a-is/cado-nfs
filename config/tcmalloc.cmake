message(STATUS "Checking for tcmalloc")
set(CMAKE_FIND_LIBRARY_SUFFIXES .a .so .lib .so.1 .so.2 .so.3 .so.4)
find_library(TCMALLOC_LIB tcmalloc)
if (TCMALLOC_LIB)
  message(STATUS "Checking for tcmalloc -- found")
else()
  message(STATUS "Checking for tcmalloc -- not found")
endif()  
