# Generated automatically from Makefile.in by configure.
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
CFLAGS= -KPIC -g
# Add flag -g to compile in debug
# Add flag -DLOG_ROLL to enable plugin manual log rolling
LDFLAGS= -G
API_INC=-I../../../include 

default: climate.so

climate.o: climate.c events.h
	$(CC) $(CFLAGS) $(API_INC) -o climate.o -c climate.c

climate.so: climate.o
	$(CC) $(LDFLAGS) -o climate.so climate.o 

clean:
	rm -f climate.so climate.o
