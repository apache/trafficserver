add_library(http_remap STATIC
        AclFiltering.cc
        NextHopSelectionStrategy.cc
        NextHopConsistentHash.cc
        NextHopHealthStatus.cc
        NextHopRoundRobin.cc
        NextHopStrategyFactory.cc
        RemapConfig.cc
        RemapHitCount.cc
        RemapPluginInfo.cc
        PluginDso.cc
        PluginFactory.cc
        RemapPlugins.cc
        RemapProcessor.cc
        UrlMapping.cc
        UrlMappingPathIndex.cc
        UrlRewrite.cc
)
target_include_directories(http_remap PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${CMAKE_SOURCE_DIR}/src/records
        ${YAML_INCLUDE_DIRS}
        )
