# Makefile.am for inkfio driver
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

INKFASTIO_KFLAGS=-D_KERNEL
INKFASTIO_MACH_INCLUDES=-I/net/devi/usr/include
INKFASTIO_MACH_DEFS=-DSunOS56 -Dsparc

AM_CPPFLAGS = $(INKFASTIO_MACH_INCLUDES) \
  -I$(top_srcdir)/proxy/iocore/fastio/include

DEFS += $(INKFASTIO_DEFS) $(INKFASTIO_MACH_DEFS)
CFLAGS = $(INKFASTIO_KFLAGS) $(DEBUG)

# Pure tools won't work with '-r' so don't use them.
LINK = $(LD) $(LDFLAGS) -o $@

EXTRA_PROGRAMS = inkfio

# Solaris
inkfio_SOURCES = fio_dev_sol.c \
		fio_ioctl.c \
		fio_request.c \
		fio_vsession.c

inkfio_LDFLAGS = -r 
inkfio_LDADD =

