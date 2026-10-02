cmake_minimum_required(VERSION 3.18)

project(RLVM VERSION 0.14.7 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
option(RLVM_BUILD_TESTS "Build unit test program" OFF)
option(RLVM_FULLSTATIC "Build executables that are linked statically" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(RLVM_FULLSTATIC)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_LINK_SEARCH_START_STATIC ON)
  set(CMAKE_LINK_SEARCH_END_STATIC ON)

  message(WARNING "Building static targets. This is currently untested and not guaranteed to work.")
endif()

find_package(PkgConfig REQUIRED)
if(RLVM_FULLSTATIC)
  list(APPEND PKG_CONFIG_EXECUTABLE --static)
endif()

enable_testing()

# -------------------------------------------------------------------
# Find TBB Library
# -------------------------------------------------------------------
find_package(TBB REQUIRED)
if(TBB_FOUND)
  message(STATUS "TBB found: ${TBB_VERSION}")
  include_directories(${TBB_INCLUDE_DIRS})
else()
  message(ERROR "TBB not found.")
endif()

# -------------------------------------------------------------------
# Find OpenGL Library
# -------------------------------------------------------------------
find_package(OpenGL REQUIRED)
if (OpenGL_FOUND)
  message(STATUS "OpenGL found: ${OpenGL_VERSION}")
  include_directories(${OpenGL_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "OpenGL not found. Please install OpenGL.")
endif()

# -------------------------------------------------------------------
# Find GLEW Library
# -------------------------------------------------------------------
find_package(GLEW REQUIRED)
if (GLEW_FOUND)
  message(STATUS "GLEW found: ${GLEW_VERSION}")
  include_directories(${GLEW_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "GLEW not found. Please install GLEW.")
endif()

# -------------------------------------------------------------------
# Find SDL3 Libraries
# -------------------------------------------------------------------
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3>=3.2.0)
pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf>=3.2.0)

# -------------------------------------------------------------------
# Find Theora decoder library
# -------------------------------------------------------------------
pkg_check_modules(THEORADEC REQUIRED IMPORTED_TARGET theoradec)

# -------------------------------------------------------------------
# Find FFmpeg encoder libraries
# -------------------------------------------------------------------
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat
  libavcodec
  libavutil
  libswscale
)

# -------------------------------------------------------------------
# Find Boost Libraries
# -------------------------------------------------------------------
set(BOOST_COMPONENTS program_options serialization iostreams filesystem log)
find_package(Boost REQUIRED COMPONENTS ${BOOST_COMPONENTS})

if (Boost_FOUND)
  message(STATUS "Boost found: ${Boost_VERSION}")
  include_directories(${Boost_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Boost libraries not found. Please install Boost.")
endif()

# -------------------------------------------------------------------
# Find Freetype2 Library
# -------------------------------------------------------------------
find_package(Freetype REQUIRED)

if (Freetype_FOUND)
  message(STATUS "Freetype2 found: ${FREETYPE_VERSION_STRING}")
  include_directories(${FREETYPE_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Freetype2 not found. Please install Freetype2 development libraries.")
endif()


# -------------------------------------------------------------------
# Find zlib Library
# -------------------------------------------------------------------
find_package(ZLIB REQUIRED)

if (ZLIB_FOUND)
  message(STATUS "zlib found: ${ZLIB_VERSION}")
  include_directories(${ZLIB_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "zlib not found. Please install zlib development libraries.")
endif()

# -------------------------------------------------------------------
# Find JPEG Library
# -------------------------------------------------------------------
find_package(JPEG REQUIRED)

if (JPEG_FOUND)
  message(STATUS "libjpeg found")
  include_directories(${JPEG_INCLUDE_DIR})
else()
  message(FATAL_ERROR "libjpeg not found. Please install libjpeg (or libjpeg-turbo) development libraries.")
endif()

# -------------------------------------------------------------------
# Find Vorbis Library
# -------------------------------------------------------------------
find_package(VorbisFile REQUIRED)

if (VorbisFile_FOUND)
  message(STATUS "VorbisFile found")
  include_directories(${VorbisFile_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "VorbisFile not found. Please install VorbisFile development libraries.")
endif()

# -------------------------------------------------------------------
# Add Subdirectories to Build
# -------------------------------------------------------------------
include_directories(vendor)
add_subdirectory(vendor)
add_subdirectory(src)
if(RLVM_BUILD_TESTS)
  add_subdirectory(test)
endif()
