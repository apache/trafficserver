add_library(inkevent)
target_sources(inkevent PRIVATE
        EventSystem.cc
        IOBuffer.cc
        Inline.cc
        Lock.cc
        MIOBufferWriter.cc
        PQ-List.cc
        Processor.cc
        ProtectedQueue.cc
        ProxyAllocator.cc
        SocketManager.cc
        Tasks.cc
        Thread.cc
        UnixEThread.cc
        UnixEvent.cc
        UnixEventProcessor.cc
        )
target_include_directories(inkevent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})