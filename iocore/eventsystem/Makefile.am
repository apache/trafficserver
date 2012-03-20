# Makefile.am for the traffic/iocore/eventsystem hierarchy
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

AM_CPPFLAGS = \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts

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
  UnixEventProcessor.cc \
  Inline.cc \
  Tasks.cc \
  I_Tasks.h
