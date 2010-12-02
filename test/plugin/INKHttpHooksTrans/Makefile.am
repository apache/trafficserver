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

#CPLUSPLUS=g++
CPLUSPLUS=CC

CPP=@CPP@
CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../proxy/api/include
LIBINK_INC=../../../../../lib/ts

default: INKHttpHook.so INKHttpSsnHookAdd.so INKHttpSelAlt.so \
	INKHttpTransaction.so INKHttpTxnErrBodySet.so


########## INKHttpHook.so 
INKHttpHook.o: INKHttpHook.c $(API_INC)/ts.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpHook.o -c INKHttpHook.c
INKHttpHook.so: INKHttpHook.o
	$(CC) $(LDFLAGS) -g -o INKHttpHook.so INKHttpHook.o

########## INKHttpSsnHookAdd.so 
INKHttpSsnHookAdd.o: INKHttpSsnHookAdd.c $(API_INC)/ts.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpSsnHookAdd.o -c \
		INKHttpSsnHookAdd.c
INKHttpSsnHookAdd.so: INKHttpSsnHookAdd.o
	$(CC) $(LDFLAGS) -g -o INKHttpSsnHookAdd.so INKHttpSsnHookAdd.o

INKHttpSelAlt.so: INKHttpSelAlt.o 
	$(CC) $(LDFLAGS) -g -o INKHttpSelAlt.so INKHttpSelAlt.o
INKHttpSelAlt.o: INKHttpSelAlt.c $(API_INC)/ts.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpSelAlt.o -c INKHttpSelAlt.c

INKHttpTransaction.so: INKHttpTransaction.o 
	$(CC) $(LDFLAGS) -g -o INKHttpTransaction.so INKHttpTransaction.o
INKHttpTransaction.o: INKHttpTransaction.c $(API_INC)/ts.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpTransaction.o -c INKHttpTransaction.c

INKHttpTxnErrBodySet.so: INKHttpTxnErrBodySet.o
	$(CC) $(LDFLAGS) -g -o INKHttpTxnErrBodySet.so INKHttpTxnErrBodySet.o
	
INKHttpTxnErrBodySet.o: INKHttpTxnErrBodySet.c $(API_INC)/ts.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpTxnErrBodySet.o \
		-c INKHttpTxnErrBodySet.c

INKHttpTxnIPAddress.so: INKHttpTxnIPAddress.o
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKHttpTxnIPAddress.so INKHttpTxnIPAddress.o
INKHttpTxnIPAddress.o: INKHttpTxnIPAddress.cc $(API_INC)/ts.h \
			$(LIBINK_INC)/ink_assert.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) -o INKHttpTxnIPAddress.o \
		-I$(LIBINK_INC) -c INKHttpTxnIPAddress.cc

clean:
	rm -f   INKHttpHook.o		INKHttpHook.so  \
		INKHttpSsnHookAdd.o	INKHttpSsnHookAdd.so \
		INKHttpSelAlt.o		INKHttpSelAlt.so \
		INKHttpTransaction.o	INKHttpTransaction.so \
		INKHttpTxnErrBodySet.o  INKHttpTxnErrBodySet.so \
		INKHttpTxnIPAddress.o	INKHttpTxnIPAddress.so
