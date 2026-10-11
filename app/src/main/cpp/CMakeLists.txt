# arm64 libgl2jni.so that runs the original 32-bit engine, packaged as an
# asset, on the dynarmic ARM32 JIT (third_party/dynarmic, vendored).
cmake_minimum_required(VERSION 3.18)
project(nin_shim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Always optimize: the emulator is the hot path even in debug APKs (Gradle
# passes CMAKE_BUILD_TYPE=Debug for debug variants, which means -O0).
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)

set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# dynarmic, A32 frontend only. Boost (header-only: icl, variant) comes from
# third_party/boost-headers.
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_ENABLE_CPU_FEATURE_DETECTION ON CACHE BOOL "" FORCE)
set(Boost_INCLUDE_DIR ${THIRD_PARTY}/boost-headers CACHE PATH "" FORCE)
set(BOOST_ROOT ${THIRD_PARTY}/boost-headers CACHE PATH "" FORCE)
set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY}/dynarmic dynarmic EXCLUDE_FROM_ALL)

# Shown instead of the engine's build date ("v. Sep  9 2017"); at most 11 characters.
set(NIN_VERSION_LABEL "" CACHE STRING "Version label shown by the engine (empty: keep its 2017 build date)")
if(NIN_VERSION_LABEL)
  set_property(SOURCE engine.cpp APPEND PROPERTY COMPILE_DEFINITIONS "NIN_VERSION_LABEL=\"${NIN_VERSION_LABEL}\"")
endif()

add_library(gl2jni SHARED
  boot_log.cpp guest.cpp engine.cpp engine_patch.cpp field_types.cpp memory_query.cpp backup.cpp diag_bypass.cpp jni_sync_bypass.cpp
  hot_dispatch.cpp bionic_hle.cpp gles_bridge.cpp jni_bridge.cpp cpu_dynarmic.cpp android_java.cpp ninlib_exports.cpp)
target_link_libraries(gl2jni PRIVATE dynarmic GLESv2 android log)
target_link_options(gl2jni PRIVATE -Wl,--exclude-libs,ALL -Wl,-z,max-page-size=16384)
