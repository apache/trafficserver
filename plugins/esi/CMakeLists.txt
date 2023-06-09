add_subdirectory(common)
add_subdirectory(fetcher)
add_subdirectory(lib)

add_atsplugin(esi
    esi.cc
    serverIntercept.cc
)
target_link_libraries(esi PRIVATE esi-common esicore fetcher)
target_include_directories(esi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_atsplugin(combo_handler
    combo_handler.cc
)

target_link_libraries(combo_handler PRIVATE esicore fetcher)
target_include_directories(combo_handler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(test)
