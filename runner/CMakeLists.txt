cmake_minimum_required(VERSION 3.20)
project(ProjectBladeRecomp CXX C)

# Opciones del runtime de PS2Recomp (dependencia externa en third_party/, GPL-3.0)
set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
# FMV (sceMpeg): con FFmpeg del sistema si pkg-config lo encuentra; sin el, GetPicture espera frames que nunca llegan
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(BLADE_FFMPEG_PROBE QUIET libavcodec libavformat libavutil libswresample libswscale)
endif()
if(BLADE_FFMPEG_PROBE_FOUND)
  set(PS2X_ENABLE_FFMPEG ON CACHE BOOL "" FORCE)
else()
  set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "" FORCE)
endif()
set(PS2X_ENABLE_SCCACHE OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "" FORCE)

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PS2R "${ROOT}/third_party/PS2Recomp")
add_subdirectory("${PS2R}" ps2recomp EXCLUDE_FROM_ALL)

# El raiz de PS2Recomp aplica esto solo a sus subdirectorios: repetirlo para el codigo generado
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|ARM64")
  add_compile_definitions(USE_SSE2NEON)
  include(FetchContent)
  FetchContent_GetProperties(sse2neon SOURCE_DIR SSE2NEON_DIR)
  include_directories("${SSE2NEON_DIR}")
endif()

set(GEN "${ROOT}/work")

# Programa principal
file(GLOB MAIN_SRC CONFIGURE_DEPENDS "${GEN}/output2/*.cpp")
add_executable(blade_recomp
  "${PS2R}/ps2xRuntime/src/main.cpp"
  ${MAIN_SRC}
  "${CMAKE_CURRENT_SOURCE_DIR}/blade_overrides.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/blade_devtools.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/blade_input.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/blade_audio.cpp")
target_include_directories(blade_recomp PRIVATE "${GEN}/output2")
target_link_libraries(blade_recomp PRIVATE ps2_runtime)
