add_test(NAME test_numpy
  COMMAND ${PYTHON_EXECUTABLE} test_numpy.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

set_tests_properties(test_numpy PROPERTIES
  ENVIRONMENT PYTHONPATH=${PYLIB_BUILD_DIR}:$ENV{PYTHONPATH})