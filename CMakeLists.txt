cmake_minimum_required(VERSION 3.21)
project(CMakeProjectTemplate VERSION 1.0.0)

include(cmake/build_option.cmake)
include(cmake/compile_setting.cmake)

# add_subdirectory(src)

if(${PROJECT_NAME}_BUILD_TEST)
  add_subdirectory(test)
endif()

if(${PROJECT_NAME}_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()

include(cmake/install.cmake)
include(cmake/cpack.cmake)
