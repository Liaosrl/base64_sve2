cmake_minimum_required(VERSION 3.15)
project(base64_sve2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -- SVE2 bitperm detection --
include(CheckCSourceCompiles)
include(CheckCSourceRuns)

set(SVE2_FLAGS "-march=armv8-a+sve2+sve2-bitperm")
set(CMAKE_REQUIRED_FLAGS "${SVE2_FLAGS}")

# 1. base SVE2 + bitperm
check_c_source_compiles("
  #include <arm_sve.h>
  int main(void){svuint32_t x=svdup_u32(1);svbdep_u32(x,x);return 0;}
" SVE2_BITPERM_OK)

# 2. svtbl2_u8 compile check (2-register TBL, 64-entry)
check_c_source_compiles("
  #include <arm_sve.h>
  int main(void){svuint8x2_t t=svcreate2_u8(svdup_u8(0),svdup_u8(0));
    svtbl2_u8(t,svdup_u8(0));return 0;}
" SVE2_SVTBL2_COMPILE)

# 3. VL>=256 check (svtbl2 needs >=256-bit VL for 64-entry table)
set(_svtbl2_run_src "
  #include <arm_sve.h>
  int main(void){return svcntb() >= 32 ? 0 : 1;}
")
if(SVE2_BITPERM_OK)
  set(CMAKE_REQUIRED_FLAGS "${SVE2_FLAGS}")
  check_c_source_runs("${_svtbl2_run_src}" SVE2_VL256_OK)
  if(NOT DEFINED SVE2_VL256_OK AND CMAKE_CROSSCOMPILING)
    set(SVE2_VL256_OK ON CACHE BOOL "Assume VL>=256 for cross-compile target")
    message(STATUS "Cross-compiling: assuming VL>=256. Override with -DSVE2_VL256_OK=OFF")
  endif()
endif()

# -- compile flags --
if(NOT SVE2_BITPERM_OK)
  message(FATAL_ERROR "SVE2 bitperm required. Pass -DCMAKE_C_FLAGS=${SVE2_FLAGS}")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 4. VL>=512 check (single svtbl2 covers all 128 entries for decoder)
if(SVE2_SVTBL2_COMPILE)
  set(_svtbl2_128_src "
    #include <arm_sve.h>
    int main(void){return svcntb() >= 64 ? 0 : 1;}
  ")
  set(CMAKE_REQUIRED_FLAGS "${SVE2_FLAGS}")
  check_c_source_runs("${_svtbl2_128_src}" SVE2_VL512_OK)
endif()

# enable svtbl2 when compiler supports it AND VL>=256
if(NOT SVE2_VL256_OK)
  message(FATAL_ERROR "VL must be >=256 bit. Both svtbl2 and tbl+tbx paths require "
    "at least 32-byte vector registers to cover the full 64-entry B64 table. "
    "VL=128 would need a 4-table approach (not implemented).")
endif()

if(SVE2_SVTBL2_COMPILE AND SVE2_VL256_OK)
  if(SVE2_VL512_OK)
    add_compile_definitions(HAVE_SVTBL2 HAVE_SVTBL2_128)
    message(STATUS "svtbl2_u8: 128-entry (single-table, VL>=512)")
  else()
    add_compile_definitions(HAVE_SVTBL2)
    message(STATUS "svtbl2_u8: 64-entry (dual-table, VL>=256)")
  endif()
else()
  message(STATUS "svtbl2_u8: no -> nibble+tbl fallback")
endif()

# -- targets --
# Decoder lookup is automatic; this switch is only for benchmarking.
option(BASE64_SVE2_FORCE_DEC_NIBBLE
  "Force nibble-table decoder lookup even when svtbl2 is available" OFF)
if(BASE64_SVE2_FORCE_DEC_NIBBLE)
  add_compile_definitions(BASE64_SVE2_FORCE_DEC_NIBBLE)
  message(STATUS "decoder lookup: forced nibble table")
endif()

add_library(base64_sve2 STATIC base64_sve2.c)
target_include_directories(base64_sve2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(base64_sve2 PUBLIC ${SVE2_FLAGS})

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
