add_library(logging STATIC
        Log.cc
        LogAccess.cc
        LogBuffer.cc
        LogConfig.cc
        LogField.cc
        LogFieldAliasMap.cc
        LogFile.cc
        LogFilter.cc
        LogFormat.cc
        LogObject.cc
        LogUtils.cc
        RolledLogDeleter.cc
        YamlLogConfig.cc
        YamlLogConfigDecoders.cc
)
target_include_directories(logging PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${YAML_INCLUDE_DIRS}
        ${SWOC_INCLUDE_DIR}
)