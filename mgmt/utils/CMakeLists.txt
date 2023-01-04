add_library(utils_lm SHARED
        MgmtMarshall.cc
        MgmtSocket.cc
        MgmtUtils.cc
        ExpandingArray.cc
        MgmtLocalCleanup.cc
)
add_library(utils_p SHARED
        MgmtMarshall.cc
        MgmtSocket.cc
        MgmtUtils.cc
        MgmtProcessCleanup.cc
)
include_directories(
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${YAML_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/proxy
        ${CMAKE_SOURCE_DIR}/proxy/http
        ${CMAKE_SOURCE_DIR}/proxy/http2
        ${CMAKE_SOURCE_DIR}/proxy/http3
        ${CMAKE_SOURCE_DIR}/proxy/logging
        ${CMAKE_SOURCE_DIR}/proxy/http/remap
        ${CMAKE_SOURCE_DIR}/proxy/hdrs
)

