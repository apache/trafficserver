add_library(http2 STATIC
        HPACK.cc
        HTTP2.cc
        Http2Frame.cc
        Http2ClientSession.cc
        Http2CommonSession.cc
        Http2ConnectionState.cc
        Http2DebugNames.cc
        Http2FrequencyCounter.cc
        Http2Stream.cc
        Http2SessionAccept.cc
)
target_include_directories(http2 PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${YAMLCPP_INCLUDE_DIR}
)