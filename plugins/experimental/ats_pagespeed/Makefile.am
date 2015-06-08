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

include $(top_srcdir)/build/plugins.mk

pkglib_LTLIBRARIES = ats_pagespeed.la ats_pagespeed_gzip.la

ats_pagespeed_la_SOURCES = \
  ats_base_fetch.cc \
  ats_config.cc \
  ats_log_message_handler.cc \
  ats_pagespeed.cc \
  ats_resource_intercept.cc \
  ats_rewrite_options.cc \
  ats_beacon_intercept.cc \
  ats_header_utils.cc \
  ats_message_handler.cc \
  ats_process_context.cc \
  ats_rewrite_driver_factory.cc \
  ats_server_context.cc

ats_pagespeed_la_LDFLAGS = \
  $(TS_PLUGIN_LDFLAGS) \
  $(PSOL_HOME)/lib/Release/@ATS_PAGESPEED_OS@/@ATS_PAGESPEED_ARCH@/pagespeed_automatic.a

ats_pagespeed_gzip_la_SOURCES = \
  gzip/configuration.cc \
  gzip/gzip.cc \
  gzip/misc.cc

ats_pagespeed_gzip_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

AM_CPPFLAGS += \
  -I$(PSOL_HOME)/include/\
  -I$(PSOL_HOME)/include/third_party/chromium/src/\
  -I$(PSOL_HOME)/include/third_party/google-sparsehash/src\
  -I$(PSOL_HOME)/include/third_party/google-sparsehash/gen/arch/@ATS_PAGESPEED_OS@/@ATS_PAGESPEED_ARCH@/include\
  -I$(PSOL_HOME)/include/third_party/protobuf/src\
  -I$(PSOL_HOME)/include/third_party/re2/src\
  -I$(PSOL_HOME)/include/third_party/out/Release/obj/gen\
  -I$(PSOL_HOME)/include/third_party/apr/src/include/\
  -I$(PSOL_HOME)/include/third_party/aprutil/src/include/\
  -I$(PSOL_HOME)/include/third_party/apr/gen/arch/@ATS_PAGESPEED_OS@/@ATS_PAGESPEED_ARCH@/include/\
  -I$(PSOL_HOME)/include/third_party/aprutil/gen/arch/@ATS_PAGESPEED_OS@/@ATS_PAGESPEED_ARCH@/include/\
  -I$(PSOL_HOME)/include/out/Release/obj/gen\
  -I$(PSOL_HOME)/include/out/Release/obj/gen/protoc_out/instaweb

