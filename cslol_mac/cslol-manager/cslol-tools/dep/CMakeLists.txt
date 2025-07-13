cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# ▸ Let FetchContent_Populate() work without complaints (CMake 4 change)
cmake_policy(SET CMP0169 OLD)
# ▸ We already removed every empty CONFIGURE_/BUILD_COMMAND, so CMP0174 NEW is fine
cmake_policy(SET CMP0174 NEW)

# ─────────────  xxHash  ─────────────
FetchContent_Declare(
  xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG        v0.8.2
)
FetchContent_MakeAvailable(xxhash)

add_library(xxhash STATIC ${xxhash_SOURCE_DIR}/xxhash.c)
target_include_directories(xxhash PUBLIC ${xxhash_SOURCE_DIR})
target_compile_definitions(xxhash
  INTERFACE -DXXH_STATIC_LINKING_ONLY
  PRIVATE   -DXXH_CPU_LITTLE_ENDIAN)

# ─────────────  zstd  ───────────────
FetchContent_Declare(
  zstd_src
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG        v1.5.6
)
FetchContent_MakeAvailable(zstd_src)      # adds target zstd::zstd_static

# zstd’s CMake exports **zstd::zstd_static** (newer releases),
# or zstd::libzstd_static (older).  Make a compat alias only if it exists.
if(TARGET zstd::zstd_static)
  add_library(zstd ALIAS zstd::zstd_static)
elseif(TARGET zstd::libzstd_static)
  add_library(zstd ALIAS zstd::libzstd_static)
endif()

# ─────────────  libdeflate  ──────────
FetchContent_Declare(
  libdeflate_src
  GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
  GIT_TAG        v1.20
)
FetchContent_MakeAvailable(libdeflate_src)    # exports libdeflate::libdeflate_static

if(NOT TARGET libdeflate AND NOT TARGET libdeflate_static)
  # real static target exists → wrap
  add_library(libdeflate ALIAS libdeflate::libdeflate_static)
elseif(NOT TARGET libdeflate)
  # exported target *is* the alias, make an INTERFACE shim instead
  add_library(libdeflate INTERFACE)
  target_link_libraries(libdeflate INTERFACE libdeflate::libdeflate_static)
endif()

# ─────────────  miniz  (manual download + one-file build) ─
FetchContent_Declare(
  miniz_src
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG        3.0.2
)
# We just want the source tree—*not* its ancient CMakeLists.
FetchContent_Populate(miniz_src)

# Check what files we have
file(GLOB MINIZ_SOURCES 
    ${miniz_src_SOURCE_DIR}/miniz.c
    ${miniz_src_SOURCE_DIR}/miniz_zip.c
    ${miniz_src_SOURCE_DIR}/miniz_tinfl.c
    ${miniz_src_SOURCE_DIR}/miniz_tdef.c
)

# If separate files exist, use them; otherwise just use miniz.c
if(EXISTS ${miniz_src_SOURCE_DIR}/miniz_zip.c)
    add_library(miniz STATIC 
        ${miniz_src_SOURCE_DIR}/miniz.c
        ${miniz_src_SOURCE_DIR}/miniz_zip.c
        ${miniz_src_SOURCE_DIR}/miniz_tinfl.c
        ${miniz_src_SOURCE_DIR}/miniz_tdef.c
    )
else()
    # For older versions that have everything in one file
    add_library(miniz STATIC ${miniz_src_SOURCE_DIR}/miniz.c)
    target_compile_definitions(miniz PRIVATE
        MINIZ_NO_ARCHIVE_APIS=0
        MINIZ_NO_ARCHIVE_WRITING_APIS=0
    )
endif()

# every consumer needs both dirs
target_include_directories(miniz PUBLIC
    ${miniz_src_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/miniz_export.h
           "#pragma once\n#define MINIZ_EXPORT\n")
# ─────────────  fmt  ─────────────────
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        10.2.1
)
FetchContent_MakeAvailable(fmt)           # exports fmt::fmt

# ─────────────  fmtlog (header-only) ──
FetchContent_Declare(
  fmtlog_src
  GIT_REPOSITORY https://github.com/MengRao/fmtlog.git
  GIT_TAG        v2.2.1
)
# only download / extract – do *not* call its CMakeLists (which re-adds fmt)
FetchContent_Populate(fmtlog_src)

add_library(fmtlog INTERFACE)
target_include_directories(fmtlog INTERFACE ${fmtlog_src_SOURCE_DIR})
target_link_libraries(fmtlog INTERFACE fmt::fmt)