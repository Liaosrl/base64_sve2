cmake_minimum_required(VERSION 3.15)
project(base64_sve2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -- SVE2 bitperm detection --
include(CheckCSourceCompiles)

set(SVE2_FLAGS "-march=armv8-a+sve2+sve2-bitperm")
set(CMAKE_REQUIRED_FLAGS "${SVE2_FLAGS}")

# 1. base SVE2 + bitperm
check_c_source_compiles("
  #include <arm_sve.h>
  int main(void){svuint32_t x=svdup_u32(1);svbdep_u32(x,x);return 0;}
" SVE2_BITPERM_OK)

# 2. svtbl2_u8 compiler support
check_c_source_compiles("
  #include <arm_sve.h>
  int main(void){svuint8x2_t t=svcreate2_u8(svdup_u8(0),svdup_u8(0));
    svtbl2_u8(t,svdup_u8(0));return 0;}
" BASE64_SVE2_COMPILER_HAS_TBL2)

set(BASE64_SVE2_LOOKUP_MODE "AUTO" CACHE STRING
  "Lookup path: AUTO, TBL2_VL256, TBL2_VL_GE512, or FALLBACK")
set_property(CACHE BASE64_SVE2_LOOKUP_MODE PROPERTY STRINGS
  AUTO TBL2_VL256 TBL2_VL_GE512 FALLBACK)
string(TOUPPER "${BASE64_SVE2_LOOKUP_MODE}" _base64_sve2_lookup_mode)
set(_base64_sve2_lookup_modes
  AUTO TBL2_VL256 TBL2_VL_GE512 FALLBACK)
if(NOT _base64_sve2_lookup_mode IN_LIST _base64_sve2_lookup_modes)
  message(FATAL_ERROR "Invalid BASE64_SVE2_LOOKUP_MODE: "
    "${BASE64_SVE2_LOOKUP_MODE}")
endif()

# -- compile flags --
if(NOT SVE2_BITPERM_OK)
  message(FATAL_ERROR "SVE2 bitperm required. Pass -DCMAKE_C_FLAGS=${SVE2_FLAGS}")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 3. AUTO detects native VL; cross-builds default to the VL256 path.
if(_base64_sve2_lookup_mode STREQUAL "AUTO" AND CMAKE_CROSSCOMPILING)
  if(BASE64_SVE2_COMPILER_HAS_TBL2)
    set(_base64_sve2_lookup_mode TBL2_VL256)
  else()
    set(_base64_sve2_lookup_mode FALLBACK)
  endif()
  message(STATUS "Cross-compiling: AUTO selected ${_base64_sve2_lookup_mode}; "
    "override with -DBASE64_SVE2_LOOKUP_MODE=<mode>")
elseif(_base64_sve2_lookup_mode STREQUAL "AUTO")
  set(_base64_sve2_vl_probe_dir
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/base64_sve2_vl_probe")
  file(MAKE_DIRECTORY "${_base64_sve2_vl_probe_dir}")
  set(_base64_sve2_vl_probe "${_base64_sve2_vl_probe_dir}/probe.c")
  file(WRITE "${_base64_sve2_vl_probe}" "
    #include <arm_sve.h>
    #include <stdio.h>
    int main(void) {
      printf(\"%llu\", (unsigned long long)svcntb() * 8ull);
      return 0;
    }
  ")
  try_run(_base64_sve2_vl_run_result _base64_sve2_vl_compile_result
    "${_base64_sve2_vl_probe_dir}/build"
    "${_base64_sve2_vl_probe}"
    COMPILE_DEFINITIONS "${SVE2_FLAGS}"
    COMPILE_OUTPUT_VARIABLE _base64_sve2_vl_compile_output
    RUN_OUTPUT_VARIABLE _base64_sve2_vl_bits)
  if(NOT _base64_sve2_vl_compile_result)
    message(FATAL_ERROR "Failed to compile the SVE VL probe:\n"
      "${_base64_sve2_vl_compile_output}")
  endif()
  if(NOT "${_base64_sve2_vl_run_result}" STREQUAL "0")
    message(FATAL_ERROR "Failed to run the SVE VL probe: "
      "${_base64_sve2_vl_run_result}")
  endif()
  string(STRIP "${_base64_sve2_vl_bits}" _base64_sve2_vl_bits)
  message(STATUS "Detected SVE VL=${_base64_sve2_vl_bits} bits")
  if(NOT _base64_sve2_vl_bits MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Invalid SVE VL: '${_base64_sve2_vl_bits}'")
  endif()
  math(EXPR _base64_sve2_vl_remainder "${_base64_sve2_vl_bits} % 128")
  if(_base64_sve2_vl_bits LESS 256 OR
     _base64_sve2_vl_bits GREATER 2048 OR
     NOT _base64_sve2_vl_remainder EQUAL 0)
    message(FATAL_ERROR "Unsupported SVE VL: ${_base64_sve2_vl_bits} bit")
  endif()
  if(NOT BASE64_SVE2_COMPILER_HAS_TBL2)
    set(_base64_sve2_lookup_mode FALLBACK)
  elseif(_base64_sve2_vl_bits EQUAL 256)
    set(_base64_sve2_lookup_mode TBL2_VL256)
  elseif(_base64_sve2_vl_bits GREATER_EQUAL 512)
    set(_base64_sve2_lookup_mode TBL2_VL_GE512)
  else()
    set(_base64_sve2_lookup_mode FALLBACK)
  endif()
endif()

# Manual TBL2 selection still requires compiler support.
if(_base64_sve2_lookup_mode MATCHES "^TBL2_" AND
   NOT BASE64_SVE2_COMPILER_HAS_TBL2)
  message(FATAL_ERROR "${_base64_sve2_lookup_mode} requires svtbl2_u8 support")
endif()

set(_base64_sve2_tbl2_definition)
if(_base64_sve2_lookup_mode STREQUAL "TBL2_VL_GE512")
  set(_base64_sve2_tbl2_definition BASE64_SVE2_USE_TBL2_VL_GE512)
elseif(_base64_sve2_lookup_mode STREQUAL "TBL2_VL256")
  set(_base64_sve2_tbl2_definition BASE64_SVE2_USE_TBL2_VL256)
endif()
message(STATUS "lookup mode: ${_base64_sve2_lookup_mode}")

# -- targets --
add_library(base64_sve2 STATIC base64_sve2.c)
target_include_directories(base64_sve2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(base64_sve2 PUBLIC ${SVE2_FLAGS})
if(_base64_sve2_tbl2_definition)
  target_compile_definitions(base64_sve2 PRIVATE
    ${_base64_sve2_tbl2_definition})
endif()

option(BUILD_DEMO "Build correctness test" ON)
if(BUILD_DEMO)
  add_executable(demo_base64 demo_base64.c)
  target_link_libraries(demo_base64 PRIVATE base64_sve2)
endif()

option(BUILD_BENCH "Build performance benchmark" ON)
if(BUILD_BENCH)
  add_executable(bench_base64 bench_base64.c)
  target_link_libraries(bench_base64 PRIVATE base64_sve2)
endif()

install(TARGETS base64_sve2
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(FILES base64_sve2.h DESTINATION include)
install(TARGETS demo_base64 bench_base64
  RUNTIME DESTINATION bin OPTIONAL)
