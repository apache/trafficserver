add_library(esicore
        EsiGunzip.cc
        EsiGzip.cc
        EsiParser.cc
        EsiProcessor.cc
        Expression.cc
        Stats.cc
        Variables.cc
)
target_link_libraries(esicore PUBLIC esi-common fetcher)
target_include_directories(esicore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_target_properties(esicore PROPERTIES POSITION_INDEPENDENT_CODE ON)
