cmake_minimum_required(VERSION 3.25)

project(asf_enumvar
  VERSION 0.1.0
  DESCRIPTION "Rust-enum-like variant driven by C++26 reflection (single header)"
  LANGUAGES CXX)

# ---- header-only interface library ----
add_library(asf_enumvar INTERFACE)
add_library(asf_enumvar::asf_enumvar ALIAS asf_enumvar)

target_include_directories(asf_enumvar INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(asf_enumvar INTERFACE cxx_std_26)

# Static reflection (P2996): GCC 16+ needs -freflection and the
# experimental standard library for std::meta.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR
      "asf_enumvar needs GCC 16+ for C++26 static reflection (-freflection)")
  endif()
  target_compile_options(asf_enumvar INTERFACE -freflection)
  target_link_libraries(asf_enumvar INTERFACE stdc++exp)
else()
  message(WARNING
    "asf_enumvar is only tested with GCC 16+ (-freflection); "
    "other compilers are untested.")
endif()

# ---- tests ----
option(ASF_ENUMVAR_BUILD_TESTS "Build asf_enumvar tests" ${PROJECT_IS_TOP_LEVEL})
if(ASF_ENUMVAR_BUILD_TESTS)
  enable_testing()
  foreach(t IN ITEMS test_var test_traits)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE asf_enumvar::asf_enumvar)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
    if(MINGW)
      # run without the MinGW runtime DLLs on PATH
      target_link_options(${t} PRIVATE -static)
    endif()
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

# ---- examples ----
# Examples are narrative demos meant for reading: built (so they never
# rot) and smoke-run in CI, but NOT registered with ctest. Correctness
# checks (assertions, edge cases, static_assert batteries) live in
# tests/ above.
option(ASF_ENUMVAR_BUILD_EXAMPLES "Build asf_enumvar examples" ${PROJECT_IS_TOP_LEVEL})
if(ASF_ENUMVAR_BUILD_EXAMPLES)
  foreach(ex IN ITEMS shapes connection)
    add_executable(example_${ex} examples/${ex}.cpp)
    target_link_libraries(example_${ex} PRIVATE asf_enumvar::asf_enumvar)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(example_${ex} PRIVATE -Wall -Wextra)
    endif()
    if(MINGW)
      target_link_options(example_${ex} PRIVATE -static)
    endif()
  endforeach()
endif()

# ---- install ----
include(GNUInstallDirs)
install(FILES include/enumvar.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS asf_enumvar EXPORT asf_enumvar-targets)
install(EXPORT asf_enumvar-targets
  NAMESPACE asf_enumvar::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asf_enumvar)
