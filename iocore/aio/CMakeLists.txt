add_library(aio)
target_sources(aio PRIVATE AIO.cc Inline.cc)
target_include_directories(aio PRIVATE ${CMAKE_SOURCE_DIR}/iocore/eventsystem)
