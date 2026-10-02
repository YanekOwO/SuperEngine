
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL
  "Suppress warnings from dependencies"
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/utf8cpp/include)

# -------------------------------------------------------------------
# pygame
# -------------------------------------------------------------------
add_library(pygame STATIC "pygame/alphablit.cc")
target_link_libraries(pygame PUBLIC PkgConfig::SDL3)

# -------------------------------------------------------------------
# GLM
# -------------------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/glm)
target_include_directories(glm PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/glm
)

# -------------------------------------------------------------------
# libsamplerate
# -------------------------------------------------------------------
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "Disable src examples" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable src tests" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libsamplerate)
target_include_directories(samplerate PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/libsamplerate/include
)

# -------------------------------------------------------------------
# utf8cpp
# -------------------------------------------------------------------
add_library(utf8cpp INTERFACE)
target_include_directories(utf8cpp INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/utf8cpp/source
)

# -------------------------------------------------------------------
# xclannad
# -------------------------------------------------------------------
file(GLOB XCLANNAD_SRC_FILES "xclannad/*.cc" "xclannad/*.cpp")
add_library(xclannad STATIC ${XCLANNAD_SRC_FILES})
target_link_libraries(xclannad PRIVATE JPEG::JPEG)
