# libarchive's reading of .7z, .rar (RAR 4 and 5) and .tar(.gz/.xz) for the
# game's import on Android (src/import_archive.cc; .zip is read there
# itself). Only these readers are built, configured by archive_config.h
# (libarchive's own CMake probes zlib and liblzma by linking them, which the
# ones built here can't be yet).

include(FetchContent)

FetchContent_Declare(libarchive
    GIT_REPOSITORY "https://github.com/libarchive/libarchive"
    GIT_TAG "v3.8.1"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    # Sources only: no add_subdirectory of its CMakeLists.txt.
    SOURCE_SUBDIR "-"
)

FetchContent_MakeAvailable(libarchive)

set(LIBARCHIVE_DIR "${libarchive_SOURCE_DIR}/libarchive")

add_library(archive_read STATIC
    "${LIBARCHIVE_DIR}/archive_acl.c"
    "${LIBARCHIVE_DIR}/archive_blake2s_ref.c"
    "${LIBARCHIVE_DIR}/archive_blake2sp_ref.c"
    "${LIBARCHIVE_DIR}/archive_check_magic.c"
    "${LIBARCHIVE_DIR}/archive_cryptor.c"
    "${LIBARCHIVE_DIR}/archive_entry.c"
    "${LIBARCHIVE_DIR}/archive_entry_sparse.c"
    "${LIBARCHIVE_DIR}/archive_entry_xattr.c"
    "${LIBARCHIVE_DIR}/archive_hmac.c"
    "${LIBARCHIVE_DIR}/archive_options.c"
    "${LIBARCHIVE_DIR}/archive_pack_dev.c"
    "${LIBARCHIVE_DIR}/archive_ppmd7.c"
    "${LIBARCHIVE_DIR}/archive_random.c"
    "${LIBARCHIVE_DIR}/archive_rb.c"
    "${LIBARCHIVE_DIR}/archive_read.c"
    "${LIBARCHIVE_DIR}/archive_read_add_passphrase.c"
    "${LIBARCHIVE_DIR}/archive_read_set_options.c"
    "${LIBARCHIVE_DIR}/archive_read_support_filter_gzip.c"
    "${LIBARCHIVE_DIR}/archive_read_support_filter_xz.c"
    "${LIBARCHIVE_DIR}/archive_read_support_format_7zip.c"
    "${LIBARCHIVE_DIR}/archive_read_support_format_rar.c"
    "${LIBARCHIVE_DIR}/archive_read_support_format_rar5.c"
    "${LIBARCHIVE_DIR}/archive_read_support_format_tar.c"
    "${LIBARCHIVE_DIR}/archive_string.c"
    "${LIBARCHIVE_DIR}/archive_string_sprintf.c"
    "${LIBARCHIVE_DIR}/archive_time.c"
    "${LIBARCHIVE_DIR}/archive_util.c"
    "${LIBARCHIVE_DIR}/archive_virtual.c"
)

target_compile_definitions(archive_read
    PRIVATE "PLATFORM_CONFIG_H=\"${CMAKE_CURRENT_SOURCE_DIR}/archive_config.h\""
    PUBLIC LIBARCHIVE_STATIC
)
target_include_directories(archive_read PUBLIC "${LIBARCHIVE_DIR}")
if(ANDROID)
    # Its 64-bit file functions on 32-bit Android (archive.h includes it
    # there while building it).
    target_include_directories(archive_read PRIVATE "${libarchive_SOURCE_DIR}/contrib/android/include")
    target_sources(archive_read PRIVATE "android/localcharset.c")
    target_include_directories(archive_read PRIVATE "android")
endif()
target_include_directories(archive_read PRIVATE ${ZLIB_INCLUDE_DIRS})
target_link_libraries(archive_read PUBLIC ${ZLIB_LIBRARIES} ${LIBLZMA_LIBRARIES})
set_target_properties(archive_read PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
)
# Its own code, not ours to fix.
target_compile_options(archive_read PRIVATE -w)
