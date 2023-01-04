add_library(hdrs STATIC
        HTTP.cc
        HdrHeap.cc
        HdrTSOnly.cc
        HdrToken.cc
        HdrUtils.cc
        HttpCompat.cc
        MIME.cc
        URL.cc
        VersionConverter.cc
        HuffmanCodec.cc
        XPACK.cc
)
target_include_directories(hdrs PRIVATE
        ${IOCORE_INCLUDE_DIRS}
)