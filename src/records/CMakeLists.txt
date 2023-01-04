add_library(records_lm STATIC
        P_RecCore.cc
        RecConfigParse.cc
        RecCore.cc
        RecDebug.cc
        RecFile.cc
        RecHttp.cc
        RecMessage.cc
        RecMutex.cc
        RecRawStats.cc
        RecUtils.cc
        RecLocal.cc
)
target_include_directories(records_lm PRIVATE
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/api/include
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${CMAKE_SOURCE_DIR}/iocore/eventsystem
        ${CMAKE_SOURCE_DIR}/iocore/utils
)

add_library(records_p STATIC
        P_RecCore.cc
        RecConfigParse.cc
        RecCore.cc
        RecDebug.cc
        RecFile.cc
        RecHttp.cc
        RecMessage.cc
        RecMutex.cc
        RecRawStats.cc
        RecUtils.cc
        RecProcess.cc
        )
target_include_directories(records_p PRIVATE
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/api/include
        ${CMAKE_SOURCE_DIR}/mgmt/utils
        ${CMAKE_SOURCE_DIR}/iocore/eventsystem
        ${CMAKE_SOURCE_DIR}/iocore/utils
        )
