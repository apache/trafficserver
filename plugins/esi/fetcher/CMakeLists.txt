add_library(fetcher HttpDataFetcherImpl.cc)
target_link_libraries(fetcher PUBLIC esi-common)
target_include_directories(fetcher PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(fetcher PROPERTIES POSITION_INDEPENDENT_CODE ON)
