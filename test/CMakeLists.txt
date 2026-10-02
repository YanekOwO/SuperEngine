
project(RLVMTests)

# -------------------------------------------------------------------
# Find Googletest Library
# -------------------------------------------------------------------
find_package(GTest QUIET)

if (GTest_FOUND)
  message(STATUS "Found GoogleTest: ${GTest_VERSION}")
  include_directories(${GTest_INCLUDE_DIRS})
  set(GTEST_LIBRARIES GTest::gtest_main GTest::gmock_main)
else()
  message(STATUS "GoogleTest not found. Fetching using FetchContent.")
  include(FetchContent)

  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    # Optional: Specify a particular version or tag
    # URL https://github.com/google/googletest/archive/release-1.11.0.zip
  )

  # Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)
  set(GTEST_LIBRARIES gtest_main gmock_main)
endif()

include(GoogleTest)
include(CTest)

# -------------------------------------------------------------------
# Build Unittest
# -------------------------------------------------------------------
file(GLOB_RECURSE TEST_SRC_FILES "*.cpp")
add_executable(unittest ${TEST_SRC_FILES})
target_include_directories(unittest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(unittest PRIVATE
  rlvm_src
  ${GTEST_LIBRARIES}

  PkgConfig::SDL3
  ${OPENGL_LIBRARIES}
  ${GLEW_LIBRARIES}
)

add_test(
  NAME RLVM_UnitTests
  COMMAND ${CMAKE_BINARY_DIR}/unittest --gtest_also_run_disabled_tests --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results.xml
)

set_tests_properties(RLVM_UnitTests PROPERTIES
  FAIL_REGULAR_EXPRESSION "FAILED"
  TIMEOUT 30
)

add_custom_target(run_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS unittest
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)


# -------------------------------------------------------------------
# Prepare test files
# -------------------------------------------------------------------
add_custom_target(copy_gameexe ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${CMAKE_CURRENT_SOURCE_DIR}/Gameexe_data ${CMAKE_BINARY_DIR}/test/Gameexe_data
  DEPENDS ${DIRS_TO_COPY}
)
add_custom_target(copy_gameroot ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${CMAKE_CURRENT_SOURCE_DIR}/Gameroot ${CMAKE_BINARY_DIR}/test/Gameroot
  DEPENDS ${DIRS_TO_COPY}
)
add_dependencies(unittest copy_gameroot copy_gameexe)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_FOUND)
  message(WARNING "Python interpreter not found.")
endif()

function(check_python_modules modules result_var)
  if(NOT Python3_FOUND)
    set(${result_var} FALSE PARENT_SCOPE)
    return()
  endif()

  set(import_cmd "import sys")
  foreach(module IN LISTS modules)
    set(import_cmd "${import_cmd}\ntry:\n    import ${module}\nexcept ImportError:\n    sys.exit(1)")
  endforeach()

  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "${import_cmd}"
    RESULT_VARIABLE modules_result
    OUTPUT_QUIET
    ERROR_QUIET
  )

  if(modules_result EQUAL 0)
    set(${result_var} TRUE PARENT_SCOPE)
  else()
    set(${result_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

set(REQUIRED_PYTHON_MODULES "numpy" "soundfile" "argparse" "wave" "struct")
check_python_modules("${REQUIRED_PYTHON_MODULES}" PYTHON_MODULES_OK)

# Generate wav test files
set(SAMPLE_RATES 8000 16000 44100)
set(CHANNELS 1 2)
set(SAMPLE_WIDTHS 8 16 32)
set(WAV_FILES)
set(WAV_PARAMS)
set(WAV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test/Gameroot/WAV")
file(MAKE_DIRECTORY "${WAV_OUTPUT_DIR}")
set(SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/wav_generator.py")

set(INDEX 0)
foreach(SAMPLE_RATE ${SAMPLE_RATES})
  foreach(CHANNEL ${CHANNELS})
    foreach(SAMPLE_WIDTH ${SAMPLE_WIDTHS})
      set(FREQUENCY 440)
      set(DURATION 0.5)
      set(FILE_NAME "${WAV_OUTPUT_DIR}/test${INDEX}.wav")

      if(${PYTHON_MODULES_OK} AND Python3_FOUND)
	add_custom_command(
          OUTPUT ${FILE_NAME}
	  COMMAND ${Python3_EXECUTABLE} ${SCRIPT_FILE} ${FILE_NAME} --frequency=${FREQUENCY} --sample_rate=${SAMPLE_RATE} --channel=${CHANNEL} --sample_format=${SAMPLE_WIDTH} --duration=${DURATION}
	  DEPENDS ${SCRIPT_FILE}
	  COMMENT "Generating ${FILE_NAME}"
	)
	list(APPEND WAV_FILES "${FILE_NAME}")
      else()
	message(WARNING "Skipping target ${FILE_NAME}")
      endif()

      math(EXPR INDEX "${INDEX} + 1")
    endforeach()
  endforeach()
endforeach()
add_custom_target(generate_wav_files ALL DEPENDS ${WAV_FILES})
add_dependencies(unittest generate_wav_files)

# Generate ogg test files
set(OGG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test/Gameroot/OGG")
file(MAKE_DIRECTORY ${OGG_OUTPUT_DIR})
set(OGG_TARGET "${OGG_OUTPUT_DIR}/test.ogg")
set(SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ogg_generator.py")
if(${PYTHON_MODULES_OK} AND Python3_FOUND)
  add_custom_command(
    OUTPUT ${OGG_TARGET}
    COMMAND ${Python3_EXECUTABLE} ${SCRIPT_FILE} ${OGG_TARGET}
    DEPENDS ${SCRIPT_FILE}
    COMMENT "Generating ${OGG_TARGET}"
  )
  add_custom_target(generate_ogg_files ALL DEPENDS ${OGG_TARGET})
  add_dependencies(unittest generate_ogg_files)
else()
  message(WARNING "Skipping target ${OGG_TARGET}")
endif()
