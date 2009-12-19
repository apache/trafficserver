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

API_HDR = ../include/InkAPI.h
AM_CFLAGS = -g -Wall -fPIC
AM_LDFLAGS = -g -Wall -shared

SUBDIRS =

default: internal

internal: blacklist-0/blacklist-0.so \
	replace-header/replace-header.so \
	output-header/output-header.so \
	thread-1/thread-1.so \
	gzip-transform/gzip.so \
	gzip-transform/gunzip.so \
	add-header-deprecated/add-header-deprecated.so \
	append-transform-deprecated/append-transform-deprecated.so \
	basic-auth-deprecated/basic-auth-deprecated.so \
	redirect-1-deprecated/redirect-1-deprecated.so \
	check-constants/check-constants.so \
	check-http/check-http-0.so \
	check-url/check-url-0.so \
	check-mime/check-mime-0.so \
	test-mgmt/test-mgmt.so \
	alt-info/alt-info.so \
	check-mime-1/check-mime-1.so \
	session-2/session-2.so \
	test-transform/test-transform.so \
	write-server-ip/write-server-ip.so \
	test-protocol/test-protocol.so \
	CacheAPITester/CacheAPITester.so \
	INKNetConnect/INKNetConnectTest.so \
	lookup/lookup.so \
	blacklist-1-neg/blacklist-1-dbg.so \
	redirect-1-neg/redirect-1-dbg.so
	#cluster-RPC/clusterRPC.so

blacklist-0/blacklist-0.so: blacklist-0/blacklist-0.c $(API_HDR)
	(cd blacklist-0; $(MAKE))

replace-header/replace-header.so: replace-header/replace-header.c $(API_HDR)
	(cd replace-header; $(MAKE))

output-header/output-header.so: output-header/output-header.c $(API_HDR)
	(cd output-header; $(MAKE))

thread-1/thread-1.so: thread-1/thread-1.c $(API_HDR)
	(cd thread-1; $(MAKE))

gzip-transform/gzip.so: gzip-transform/gzip.c $(API_HDR)
	(cd gzip-transform; $(MAKE) gzip.so)

gzip-transform/gunzip.so: gzip-transform/gunzip.c $(API_HDR)
	(cd gzip-transform; $(MAKE) gunzip.so)

add-header-deprecated/add-header-deprecated.so: internal/add-header-deprecated/add-header.deprecated.c $(API_HDR)
	(cd internal/add-header-deprecated; $(MAKE))

append-transform-deprecated/append-transform-deprecated.so: internal/append-transform-deprecated/append-transform.deprecated.c $(API_HDR)
	(cd internal/append-transform-deprecated; $(MAKE))

basic-auth-deprecated/basic-auth-deprecated.so: internal/basic-auth-deprecated/basic-auth.deprecated.c $(API_HDR)
	(cd internal/basic-auth-deprecated; $(MAKE))

redirect-1-deprecated/redirect-1-deprecated.so: internal/redirect-1-deprecated/redirect-1.deprecated.c $(API_HDR)
	(cd internal/redirect-1-deprecated; $(MAKE))

check-constants/check-constants.so: internal/check-constants/check-constants.c $(API_HDR)
	(cd internal/check-constants; $(MAKE))

check-mime/check-mime-0.so: internal/check-mime/check-mime-0.c $(API_HDR)
	(cd internal/check-mime; $(MAKE))

check-http/check-http-0.so: internal/check-http/check-http-0.c $(API_HDR)
	(cd internal/check-http; $(MAKE))

check-url/check-url-0.so: internal/check-url/check-url-0.c $(API_HDR)
	(cd internal/check-url; $(MAKE))

test-mgmt/test-mgmt.so: internal/test-mgmt/test-mgmt.c $(API_HDR)
	(cd internal/test-mgmt; $(MAKE))

alt-info/alt-info.so: internal/alt-info/alt-info.c $(API_HDR)
	(cd internal/alt-info; $(MAKE))

check-mime-1/check-mime-1.so: internal/check-mime-1/check-mime-1.c $(API_HDR)
	(cd internal/check-mime-1; $(MAKE))

session-2/session-2.so: internal/session-2/session-2.c $(API_HDR)
	(cd internal/session-2; $(MAKE))

test-transform/test-transform.so: internal/test-transform/test-transform.c $(API_HDR)
	(cd internal/test-transform; $(MAKE))

write-server-ip/write-server-ip.so: internal/write-server-ip/write-server-ip.c $(API_HDR)
	(cd internal/write-server-ip; $(MAKE))

test-protocol/test-protocol.so: internal/test-protocol/test-protocol.c $(API_HDR)
	(cd internal/test-protocol; $(MAKE))

CacheAPITester/CacheAPITester.so: internal/CacheAPITester/CacheAPITester.c $(API_HDR)
	(cd internal/CacheAPITester; $(MAKE))

INKNetConnect/INKNetConnectTest.so: internal/INKNetConnect/INKNetConnectTest.c $(API_HDR)
	(cd internal/INKNetConnect; $(MAKE))

INKNetConnect/INKqa8418.so: internal/INKNetConnect/INKqa8418.c $(API_HDR)
	(cd internal/INKNetConnect; $(MAKE))

lookup/lookup.so: internal/lookup/lookup.c $(API_HDR)
	(cd internal/lookup; $(MAKE))

blacklist-1-neg/blacklist-1-dbg.so: internal/blacklist-1-neg/blacklist-1-neg.c $(API_HDR)
	(cd internal/blacklist-1-neg; $(MAKE))

redirect-1-neg/redirect-1-dbg.so: internal/redirect-1-neg/redirect-1-neg.c $(API_HDR)
	(cd internal/redirect-1-neg; $(MAKE))

#cluster-RPC/clusterRPC.so: internal/cluster-RPC/clusterRPC.c $(API_HDR)
#	(cd internal/cluster-RPC; $(MAKE))

clean:
	rm -f */*.so */*.o
	rm -f */*/*.so */*/*.o
