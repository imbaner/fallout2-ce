# liblzma (decoding only): the LZMA/LZMA2 data of .7z archives and .tar.xz
# (libarchive, third_party/libarchive) for the game's import on Android.

set(XZ_ENCODERS "" CACHE STRING "" FORCE)
set(XZ_MATCH_FINDERS "" CACHE STRING "" FORCE)
set(XZ_MICROLZMA_ENCODER OFF CACHE BOOL "" FORCE)
set(XZ_MICROLZMA_DECODER OFF CACHE BOOL "" FORCE)
set(XZ_LZIP_DECODER OFF CACHE BOOL "" FORCE)
set(XZ_THREADS "no" CACHE STRING "" FORCE)
set(XZ_NLS OFF CACHE BOOL "" FORCE)
set(XZ_DOC OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_XZ OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_XZDEC OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_LZMADEC OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_SCRIPTS OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_SYMLINKS OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_SYMLINKS_LZMA OFF CACHE BOOL "" FORCE)
set(XZ_SANDBOX "no" CACHE STRING "" FORCE)

include(FetchContent)

FetchContent_Declare(xz
    GIT_REPOSITORY "https://github.com/tukaani-project/xz"
    GIT_TAG "v5.8.1"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(xz)

# Its own CMake gives lzma.h only to an installed copy.
target_include_directories(liblzma INTERFACE "$<BUILD_INTERFACE:${xz_SOURCE_DIR}/src/liblzma/api>")
target_compile_definitions(liblzma INTERFACE LZMA_API_STATIC)

set(LIBLZMA_LIBRARIES "liblzma" PARENT_SCOPE)
