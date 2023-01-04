add_library(inkhostdb STATIC
    HostDB.cc
    Inline.cc
    RefCountCache.cc
    HostFile.cc
    HostDBInfo.cc
)
target_include_directories(inkhostdb PRIVATE
        ${CMAKE_SOURCE_DIR}/iocore/eventsystem
        ${CMAKE_SOURCE_DIR}/iocore/dns
        ${CMAKE_SOURCE_DIR}/iocore/aio
        ${CMAKE_SOURCE_DIR}/iocore/net
        ${CMAKE_SOURCE_DIR}/iocore/cache
        ${CMAKE_SOURCE_DIR}/iocore/hostdb
        ${CMAKE_SOURCE_DIR}/proxy
        ${CMAKE_SOURCE_DIR}/proxy/http
        ${CMAKE_SOURCE_DIR}/proxy/hdrs
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
)
