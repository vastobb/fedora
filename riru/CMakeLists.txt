cmake_minimum_required(VERSION 3.22)

project(riru_levi_mod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Expect PRELOADER_ANDROID_ROOT to point at LiteLDev/preloader-android checkout.
# In Actions (build-riru.yml) this is fetched via FetchContent at 0.2.3.
# For local standalone builds, pass -DPRELOADER_ANDROID_ROOT=<path>.
set(PRELOADER_ANDROID_ROOT "" CACHE PATH "Path to preloader-android checkout")

if(PRELOADER_ANDROID_ROOT STREQUAL "" OR NOT EXISTS "${PRELOADER_ANDROID_ROOT}/include/pl/Mod.hpp")
  include(FetchContent)
  FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG 5d108da350ce3e7f13f51415f81e65344ecd5d14)
  FetchContent_MakeAvailable(preloader_android)
endif()

option(RIRU_USE_HOOKS "Enable true-StartScreen hook scaffold (needs verified offsets)" OFF)

if(NOT ANDROID)
  message(STATUS "riru: non-Android host configure only (no .so). Set ANDROID toolchain for real build.")
endif()

if(ANDROID)
  if(NOT TARGET preloader AND EXISTS "${PRELOADER_ANDROID_ROOT}/CMakeLists.txt")
    add_subdirectory("${PRELOADER_ANDROID_ROOT}" "${CMAKE_BINARY_DIR}/_preloader_android")
  endif()

  if(ANDROID_ABI)
    set(RIRU_OUT_ABI "${ANDROID_ABI}")
  else()
    set(RIRU_OUT_ABI "android")
  endif()
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${RIRU_OUT_ABI}")

  find_library(RIRU_LOG_LIB log)

  add_library(Riru SHARED src/RiruTest.cpp)
  set_target_properties(Riru PROPERTIES OUTPUT_NAME "Riru")

  target_include_directories(Riru PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
  if(PRELOADER_ANDROID_ROOT AND EXISTS "${PRELOADER_ANDROID_ROOT}/include")
    target_include_directories(Riru PRIVATE "${PRELOADER_ANDROID_ROOT}/include")
  endif()

  target_compile_options(Riru PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic -fvisibility=hidden -fvisibility-inlines-hidden>)

  if(RIRU_USE_HOOKS)
    target_compile_definitions(Riru PRIVATE RIRU_USE_HOOKS=1)
  else()
    target_compile_definitions(Riru PRIVATE RIRU_USE_HOOKS=0)
  endif()

  if(RIRU_LOG_LIB)
    target_link_libraries(Riru PRIVATE "${RIRU_LOG_LIB}")
  endif()

  if(TARGET preloader)
    target_link_libraries(Riru PRIVATE preloader)
  else()
    # Allow configure without preloader target (Actions always has it).
    target_link_options(Riru PRIVATE -Wl,--unresolved-symbols=ignore-all)
  endif()
endif()
