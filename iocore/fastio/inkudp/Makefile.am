# Makefile.am for inkudp
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

INKFASTUDP_KFLAGS=-D_KERNEL
INKFASTUDP_MACH_INCLUDES=
INKFASTUDP_MACH_DEFS=-DSunOS56 -Dsparc

AM_CPPFLAGS = $(INKFASTUDP_MACH_INCLUDES) \
  -I$(top_srcdir)/proxy/iocore/fastio/include

DEFS += $(INKFASTUDP_DEFS) $(INKFASTUDP_MACH_DEFS)
CFLAGS = $(INKFASTUDP_KFLAGS) $(DEBUG)

# Pure tools won't work with '-r' so don't use them.
LINK = $(LD) $(LDFLAGS) -o $@

EXTRA_PROGRAMS = inkudp

# Solaris
inkudp_SOURCES =solaris.c \
		inkudp_ioctl_util.c\
		inkudp_ioctl_fini.c\
		inkudp_sched.c
inkudp_LDFLAGS = -r -dy -N "pseudo/inkfio"
inkudp_LDADD = 

