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

CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: CacheAPITester.so CacheAPITester-dbg.so

CacheAPITester.o: CacheAPITester.c CacheAPITester.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o CacheAPITester.o -c CacheAPITester.c

CacheAPITester.so: CacheAPITester.o
	$(CC) $(LDFLAGS) -o CacheAPITester.so CacheAPITester.o

CacheAPITester-dbg.o: CacheAPITester.c CacheAPITester.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o CacheAPITester-dbg.o -c CacheAPITester.c

CacheAPITester-dbg.so: CacheAPITester-dbg.o
	$(CC) $(LDFLAGS) -DDEBUG -o CacheAPITester-dbg.so CacheAPITester-dbg.o

clean:
	rm -f CacheAPITester.so CacheAPITester.o
	rm -f CacheAPITester-dbg.so CacheAPITester-dbg.o

