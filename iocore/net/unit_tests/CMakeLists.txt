add_executable(test_inknet
    ../libinknet_stub.cc
    unit_test_main.cc
    test_ProxyProtocol.cc
    test_SSLSNIConfig.cc
    test_YamlSNIConfig.cc
)

target_link_libraries(test_inknet
    PRIVATE
        catch2::catch2
        ts::inknet # transitive
        ts::hdrs # transitive
        ts::proxy # transitive
)

target_compile_definitions(test_inknet
    PRIVATE
        -DLIBINKNET_UNIT_TEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME test_inknet COMMAND $<TARGET_FILE:test_inknet>)
