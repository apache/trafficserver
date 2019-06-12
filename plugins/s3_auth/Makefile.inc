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

pkglib_LTLIBRARIES += s3_auth/s3_auth.la
s3_auth_s3_auth_la_SOURCES = s3_auth/s3_auth.cc s3_auth/aws_auth_v4.cc

check_PROGRAMS +=  s3_auth/test_s3auth

s3_auth_test_s3auth_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include -DAWS_AUTH_V4_UNIT_TEST
s3_auth_test_s3auth_LDADD = $(OPENSSL_LIBS)
s3_auth_test_s3auth_SOURCES = \
    s3_auth/unit_tests/test_aws_auth_v4.cc \
    s3_auth/aws_auth_v4.cc
