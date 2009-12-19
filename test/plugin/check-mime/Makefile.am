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
OTHER_INC=../include_other

default: check-mime-0.so check-mime-0-dbg.so

check-mime-0.o: check-mime-0.c $(OTHER_INC)/macro.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(OTHER_INC) -I$(API_INC) -o check-mime-0.o -c check-mime-0.c

check-mime-0.so: check-mime-0.o
	$(CC) $(LDFLAGS) -o check-mime-0.so check-mime-0.o

check-mime-0-dbg.o: check-mime-0.c $(OTHER_INC)/macro.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(OTHER_INC) -I$(API_INC) -o check-mime-0-dbg.o -c check-mime-0.c

check-mime-0-dbg.so: check-mime-0-dbg.o
	$(CC) $(LDFLAGS) -o check-mime-0-dbg.so check-mime-0-dbg.o

clean:
	rm -f check-mime-0.so check-mime-0.o
	rm -f check-mime-0-dbg.so check-mime-0-dbg.o
