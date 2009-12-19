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
CFLAGS= -g -KPIC -D_REENTRANT=1
LDFLAGS= -G -g

TS_BASE=../../../../..
API_INC= -I. -I$(TS_BASE)/proxy/api/include

default: assembly.so

headers.o: headers.c headers.h common.h
	$(CC) $(CFLAGS) $(API_INC) -o headers.o -c headers.c

list.o: list.c list.h common.h
	$(CC) $(CFLAGS) $(API_INC) -o list.o -c list.c

assembly.o: assembly.c common.h
	$(CC) $(CFLAGS) $(API_INC) -o assembly.o -c assembly.c

assembly.so: assembly.o headers.o list.o
	$(CC) $(LDFLAGS) -o assembly.so assembly.o headers.o list.o

clean:
	rm -f assembly.so assembly.o headers.o list.o
