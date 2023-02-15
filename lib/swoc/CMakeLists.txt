cmake_minimum_required(VERSION 3.11)

project(Lib-SWOC CXX)
set(LIBSWOC_VERSION "1.4.2")
set(CMAKE_CXX_STANDARD 17)
cmake_policy(SET CMP0087 NEW)
# override "lib64" to be "lib" unless the user explicitly sets it.
set(CMAKE_INSTALL_LIBDIR "lib" CACHE STRING "directory for libraries" FORCE)
include(GNUInstallDirs)

cmake_dependent_option(LIBSWOC_INSTALL
        "Enable generation of libswoc install targets" ON
        "NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT" OFF)

set(HEADER_FILES
        include/swoc/swoc_version.h
        include/swoc/ArenaWriter.h
        include/swoc/BufferWriter.h
        include/swoc/bwf_base.h
        include/swoc/bwf_ex.h
        include/swoc/bwf_ip.h
        include/swoc/bwf_std.h
        include/swoc/DiscreteRange.h
        include/swoc/Errata.h
        include/swoc/IntrusiveDList.h
        include/swoc/IntrusiveHashMap.h
        include/swoc/swoc_ip.h
        include/swoc/IPEndpoint.h
        include/swoc/IPAddr.h
        include/swoc/IPSrv.h
        include/swoc/IPRange.h
        include/swoc/Lexicon.h
        include/swoc/MemArena.h
        include/swoc/MemSpan.h
        include/swoc/Scalar.h
        include/swoc/TextView.h
        include/swoc/swoc_file.h
        include/swoc/swoc_meta.h
        include/swoc/string_view.h
        include/swoc/Vectray.h
        )

# These are external but required.
set(EXTERNAL_HEADER_FILES
        include/swoc/ext/HashFNV.h
        )

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
set_target_properties(libswoc PROPERTIES OUTPUT_NAME swoc-${LIBSWOC_VERSION})
if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(libswoc PRIVATE -fPIC -Wall -Wextra -Werror -Wnon-virtual-dtor -Wpedantic)
endif()

add_library(libswoc-static STATIC ${CC_FILES})
set_target_properties(libswoc-static PROPERTIES OUTPUT_NAME swoc-static-${LIBSWOC_VERSION})
if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(libswoc-static PRIVATE -fPIC -Wall -Wextra -Werror -Wnon-virtual-dtor -Wpedantic)
endif()

# Not quite sure how this works, but I think it generates one of two paths depending on the context.
# That is, the generator functions return non-empty strings only in the corresponding context.
target_include_directories(libswoc-static
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        )

target_include_directories(libswoc
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        )

if (LIBSWOC_INSTALL)
    # These install target variables are created by GNUInstallDirs.
    install(TARGETS libswoc-static
            EXPORT libswoc-static-config
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # seems to have no effect.
            )
    install(TARGETS libswoc
            EXPORT libswoc-config
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # seems to have no effect.
            )
    install(DIRECTORY include/swoc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    set(link-target libswoc.so)
    set(link-src $<TARGET_FILE_NAME:libswoc>)
    install(CODE
            "execute_process(COMMAND bash -c \"echo Linking ${link-src} to ${link-target};cd ${CMAKE_INSTALL_FULL_LIBDIR} && (rm -f ${link-target};ln -s ${link-src} ${link-target})
        \")"
            )

    install(EXPORT libswoc-static-config
            NAMESPACE libswoc::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libswoc
            )

    install(EXPORT libswoc-config
            NAMESPACE libswoc::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libswoc
            )

    #    set(PKG_CONFIG_FILE libswoc.pc)
    #    set(PKG_STATIC_CONFIG_FILE libswoc-static.pc)
    #    configure_file("libswoc.pc.cmake" ${PKG_CONFIG_FILE} @ONLY)
    #
    # configure_file("libswoc.pc.cmake" libswoc.pc @ONLY)
    # configure_file("libswoc-static.pc.cmake" libswoc-static.pc @ONLY)
    # install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libswoc.pc ${CMAKE_CURRENT_BINARY_DIR}/libswoc-static.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
    #    install(FILES ${PKG_STATIC_CONFIG_FILE} DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

# Alledgedly this makes the targets "importable from the build directory" but I see no evidence of that.
# AFAICT the file isn't created at all even with this enabled.
#export(TARGETS libswoc FILE libswoc-config.cmake)

set(CLANG_DIRS )

set_target_properties(libswoc-static PROPERTIES CLANG_FORMAT_DIRS "${PROJECT_SOURCE_DIR}/src;${PROJECT_SOURCE_DIR}/include")