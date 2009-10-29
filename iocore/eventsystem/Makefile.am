# Makefile.am for the traffic/iocore/eventsystem hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/libinktomi++

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkevent.a

libinkevent_a_SOURCES = \
  EventSystem.cc \
  I_Action.h \
  I_Continuation.h \
  I_EThread.h \
  I_Event.h \
  I_EventProcessor.h \
  I_EventSystem.h \
  I_IOBuffer.h \
  I_Lock.h \
  Inline.cc \
  IOBuffer.cc \
  I_PriorityEventQueue.h \
  I_Processor.h \
  I_ProtectedQueue.h \
  I_ProxyAllocator.h \
  I_SocketManager.h \
  I_Thread.h \
  I_VConnection.h \
  I_VIO.h \
  Lock.cc \
  P_EventSystem.h \
  P_Freer.h \
  P_IOBuffer.h \
  P_ProtectedQueue.h \
  PQ-List.cc \
  Processor.cc \
  ProtectedQueue.cc \
  P_Thread.h \
  P_UnixEThread.h \
  P_UnixEvent.h \
  P_UnixEventProcessor.h \
  P_UnixSocketManager.h \
  P_VConnection.h \
  P_VIO.h \
  SocketManager.cc \
  Thread.cc \
  UnixEThread.cc \
  UnixEvent.cc \
  UnixEventProcessor.cc

