if(${PROJECT_NAME}_ENABLE_INSTALL)
#
# CMake 安装选项
#

### 安装头文件 ###
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/headers DESTINATION include)
# install(FILES ${CMAKE_SOURCE_DIR}/include/head.h DESTINATION include)
# install(FILES ${PROJECT_SOURCE_DIR}/include/xxx.hpp DESTINATION include)
# install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/xxx DESTINATION include FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
# install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/xxxx DESTINATION include FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

### 安装二进制目标文件 ###
# install(TARGETS lib1 DESTINATION lib)
# install(TARGETS program1 DESTINATION bin)

endif()
