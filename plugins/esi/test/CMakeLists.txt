add_library(esitest
    print_funcs.cc
    HandlerMap.cc
    StubIncludeHandler.cc
    TestHandlerManager.cc
)
target_link_libraries(esitest PUBLIC esi-common esicore)

macro(ADD_ESI_TEST NAME)
    add_executable(${NAME} ${ARGN})
    target_link_libraries(${NAME} PRIVATE esitest)
    add_test(NAME ${NAME}_esi COMMAND $<TARGET_FILE:${NAME}>)
endmacro()

add_esi_test(test_docnode docnode_test.cc)
target_link_libraries(test_docnode PRIVATE esi-common esicore)
add_esi_test(test_parser parser_test.cc)
target_link_libraries(test_parser PRIVATE esi-common esicore)
add_esi_test(test_processor processor_test.cc)
target_link_libraries(test_processor PRIVATE esi-common esicore)
add_esi_test(test_utils utils_test.cc)
target_link_libraries(test_utils PRIVATE esi-common)
add_esi_test(test_vars vars_test.cc)
target_link_libraries(test_vars PRIVATE esi-common esicore)
add_esi_test(test_gzip gzip_test.cc)
target_link_libraries(test_gzip PRIVATE catch2::catch2 esi-common)
