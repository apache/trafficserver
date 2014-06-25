#
# Makefile for building the lighttpd mod_generator enabled version
# of lighttpd.
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

VERSION=1.4.35

all: mod_generator.so

lighttpd:
	if [ ! -f lighttpd-$(VERSION).tar.bz2 ]; then wget http://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-$(VERSION).tar.bz2; fi
	if [ ! -d lighttpd-$(VERSION) ]; then tar xjf lighttpd-$(VERSION).tar.bz2; fi
	cd lighttpd-$(VERSION); ./configure --prefix=/usr/local --without-bzip2
	cd lighttpd-$(VERSION); gmake

mod_generator.so: mod_generator.c lighttpd
	gcc -shared -DHAVE_CONFIG_H -DLIBRARY_DIR=\"/usr/local/lib64\" -DSBIN_DIR=\"/usr/local/sbin\" -I. -I.. -D_REENTRANT -D__EXTENSIONS__ -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGE_FILES -g -O2 -Wall -W -Wshadow -pedantic -std=gnu99 -MTmod_scgi.lo -MD -MP mod_generator.c  -fPIC -DPIC -I lighttpd-$(VERSION)/src/ -I lighttpd-$(VERSION)/ -o mod_generator.so

clean:
	rm -rf lighttpd-$(VERSION).tar.bz2 lighttpd-$(VERSION) mod_generator.so
