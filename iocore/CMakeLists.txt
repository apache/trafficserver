add_subdirectory(eventsystem)
add_subdirectory(net)
add_subdirectory(aio)
add_subdirectory(dns)
add_subdirectory(hostdb)
add_subdirectory(utils)
add_subdirectory(cache)

set(IOCORE_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/iocore/eventsystem
        ${CMAKE_SOURCE_DIR}/iocore/dns
        ${CMAKE_SOURCE_DIR}/iocore/aio
        ${CMAKE_SOURCE_DIR}/iocore/net
        ${CMAKE_SOURCE_DIR}/iocore/cache
        ${CMAKE_SOURCE_DIR}/iocore/hostdb
        ${CMAKE_SOURCE_DIR}/iocore/utils PARENT_SCOPE)
