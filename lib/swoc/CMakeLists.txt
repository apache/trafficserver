cmake_minimum_required(VERSION 3.11)

project(Lib-SWOC CXX)
set(LIBSWOC_VERSION "1.5.11")
set(CMAKE_CXX_STANDARD 17)
cmake_policy(SET CMP0087 NEW)
# override "lib64" to be "lib" unless the user explicitly sets it.
set(CMAKE_INSTALL_LIBDIR
    "lib"
    CACHE STRING "directory for libraries"
)
include(GNUInstallDirs)
include(CMakeDependentOption)

cmake_dependent_option(
  LIBSWOC_INSTALL "Enable generation of libswoc install targets" ON "NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT"
  OFF
)

set(HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/ArenaWriter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/BufferWriter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/DiscreteRange.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/Errata.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/HashFNV.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/IPAddr.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/IPEndpoint.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/IPRange.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/IPSrv.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/IntrusiveDList.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/IntrusiveHashMap.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/Lexicon.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/MemArena.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/MemSpan.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/RBTree.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/Scalar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/TextView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/Vectray.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/bwf_base.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/bwf_ex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/bwf_fwd.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/bwf_ip.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/bwf_std.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/string_view_util.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/swoc_file.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/swoc_ip.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/swoc_ip_util.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/swoc_meta.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/swoc/swoc_version.h
)

# These are external but required.
set(EXTERNAL_HEADER_FILES include/swoc/ext/HashFNV.h)

set(CC_FILES
    src/bw_format.cc
    src/bw_ip_format.cc
    src/ArenaWriter.cc
    src/Errata.cc
    src/swoc_ip.cc
    src/MemArena.cc
    src/RBTree.cc
    src/swoc_file.cc
    src/TextView.cc
    src/string_view_util.cc
)

add_library(libswoc SHARED ${CC_FILES})
set_target_properties(libswoc PROPERTIES OUTPUT_NAME swoc-${LIBSWOC_VERSION} PUBLIC_HEADER "${HEADER_FILES}")

if(CMAKE_COMPILER_IS_GNUCXX)
  target_compile_options(
    libswoc
    PRIVATE -fPIC
            -Wall
            -Wextra
            -Werror
            -Wnon-virtual-dtor
            -Wpedantic
            -Wshadow
  )
endif()

add_library(libswoc-static STATIC ${CC_FILES})
set_target_properties(
  libswoc-static PROPERTIES OUTPUT_NAME swoc-static-${LIBSWOC_VERSION} PUBLIC_HEADER "${HEADER_FILES}"
)
if(CMAKE_COMPILER_IS_GNUCXX)
  target_compile_options(
    libswoc-static
    PRIVATE -fPIC
            -Wall
            -Wextra
            -Werror
            -Wnon-virtual-dtor
            -Wpedantic
            -Wshadow
  )
endif()

# Not quite sure how this works, but I think it generates one of two paths depending on the context.
# That is, the generator functions return non-empty strings only in the corresponding context.
target_include_directories(
  libswoc-static PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)

target_include_directories(
  libswoc PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)

# Alledgedly this makes the targets "importable from the build directory" but I see no evidence of that.
# AFAICT the file isn't created at all even with this enabled.
#export(TARGETS libswoc FILE libswoc-config.cmake)

set(CLANG_DIRS)

set_target_properties(
  libswoc-static PROPERTIES CLANG_FORMAT_DIRS "${PROJECT_SOURCE_DIR}/src;${PROJECT_SOURCE_DIR}/include"
)

install(TARGETS libswoc PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/swoc)

add_library(libswoc::libswoc ALIAS libswoc)

if(BUILD_TESTING)
add_subdirectory(unit_tests)
endif()
