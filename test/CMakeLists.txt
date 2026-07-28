include(CTest)

function(add_test_executable target_name)
  add_executable(${target_name} ${ARGN})
  set_target_properties(${target_name} PROPERTIES FOLDER "TestCases")
endfunction()

function(add_ctest_executable target_name)
  add_executable(${target_name} ${ARGN})
  add_test(NAME ${target_name} COMMAND ${target_name})
  set_target_properties(${target_name} PROPERTIES FOLDER "TestCases")
endfunction()

### Here is examples ###
# add_test_executable(my_test1 my_test1.cpp my_test1a.cpp code.cpp)
# add_ctest_executable(my_ctest2 my_ctest2.cpp my_test2a.cpp code.cpp)


