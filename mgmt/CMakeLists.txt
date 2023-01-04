add_subdirectory(utils)

add_library(mgmt_c SHARED RecordsConfig.cc)
add_library(mgmt_p SHARED
        BaseManager.cc
        RecordsConfigUtils.cc
        ProcessManager.cc
        ProxyConfig.cc
        YamlCfg.cc
)
add_library(mgmt_lm SHARED
        BaseManager.cc
        RecordsConfigUtils.cc
        Alarms.cc
        DerivativeMetrics.cc
        FileManager.cc
        LocalManager.cc
        ConfigManager.cc
        WebMgmtUtils.cc
)
include_directories(${IOCORE_INCLUDE_DIRS} ${PROXY_INCLUDE_DIRS} ${YAML_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/utils)