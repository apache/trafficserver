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

OS := $(shell uname -s)
MACHINE := $(shell uname -m)
PLATFORM := $(shell uname -p)


SYSDEFINES = -D$(OS) -DMACHINE=$(MACHINE) -DPLATFORM=$(PLATFORM)

CCFLAGS = $(SYSDEFINES)

ifeq ($(OS), SunOS)
  LIBS = -lnsl -lsocket
endif


CC=gcc

TARGET= UdpSend.$(OS).$(PLATFORM).$(MACHINE)

all : $(TARGET)


$(TARGET): UdpSend.c
	$(CC) UdpSend.c -o $(TARGET) $(LIBS) $(CCFLAGS)


clean:
	rm -f $(TARGET)



