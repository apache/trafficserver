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

CC=cc
LD=ld
CFLAGS= -g -D_REENTRANT=1 -m32
LDFLAGS= -g -shared -m32

TS_BASE=../../../..
API_INC= -I. -I$(TS_BASE)/proxy/api/include

default: cache_plugin.so

cache_plugin.o: cache_plugin.cc
	$(CXX) $(CFLAGS) $(API_INC) -o cache_plugin.o -c cache_plugin.cc

cache_plugin.so: cache_plugin.o
	$(CXX) $(LDFLAGS) -o cache_plugin.so cache_plugin.o

clean:
	rm -f cache_plugin.so cache_plugin.o
