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
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@ -lz
API_INC=../../include

default: gzip.so gunzip.so

gzip.o: gzip.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -I/usr/local/include -o gzip.o -c gzip.c

gunzip.o: gunzip.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -I/usr/local/include -o gunzip.o -c gunzip.c

gzip.so: gzip.o
	$(CC) $(LDFLAGS) -L/usr/local/lib -o gzip.so gzip.o

gunzip.so: gunzip.o
	$(CC) $(LDFLAGS) -L/usr/local/lib -o gunzip.so gunzip.o

clean:
	rm -f gzip.so gzip.o gunzip.so gunzip.o 
