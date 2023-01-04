add_library(proxy STATIC
        private/SSLProxySession.cc
        CacheControl.cc
        ControlBase.cc
        ControlMatcher.cc
        IPAllow.cc
        ParentConsistentHash.cc
        ParentRoundRobin.cc
        ParentSelectionStrategy.cc
        ParentSelection.cc
        Plugin.cc
        PluginVC.cc
        ProtocolProbeSessionAccept.cc
        ProxySession.cc
        ProxyTransaction.cc
        ReverseProxy.cc
        StatPages.cc
        Transform.cc
)

set(PROXY_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/proxy
        ${CMAKE_SOURCE_DIR}/proxy/http
        ${CMAKE_SOURCE_DIR}/proxy/http2
        ${CMAKE_SOURCE_DIR}/proxy/http3
        ${CMAKE_SOURCE_DIR}/proxy/logging
        ${CMAKE_SOURCE_DIR}/proxy/http/remap
        ${CMAKE_SOURCE_DIR}/proxy/hdrs
)

target_include_directories(proxy PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${CMAKE_SOURCE_DIR}/lib/yamlcpp/include
        )

add_subdirectory(hdrs)
add_subdirectory(shared)
add_subdirectory(http)
add_subdirectory(http2)
add_subdirectory(logging)
