# Makefile.am for libfastIO.a
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
 

FASTIO_MACH_DEFS=-fPIC -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 -D_POSIX_PTHREAD_SEMANTICS=1
FASTIO_LIB_DEFS=-DDEBUG

DEFS += $(FASTIO_MACH_DEFS) $(FASTIO_LIB_DEFS)
AM_CPPFLAGS = -I$(top_srcdir)/proxy/iocore/fastio/include

# Pure tools won't work with '-r' so don't use them.
CXXLINK = $(LD) $(LDFLAGS) -o $@

noinst_LIBRARIES =libfastIO.a

DEFS += $(FASTIO_MACH_DEFS) $(FASTIO_LIB_DEFS)

noinst_PROGRAMS =

# libfastIO source files
libfastIO_a_SOURCES =  libfastio.c 
