add_library(diagsconfig STATIC DiagsConfig.cc)
target_include_directories(diagsconfig PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${YAMLCPP_INCLUDE_DIR}
)
add_library(UglyLogStubs STATIC UglyLogStubs.cc)
target_include_directories(UglyLogStubs PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${YAMLCPP_INCLUDE_DIR}
)
