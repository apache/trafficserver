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

noinst_LTLIBRARIES += tools/plugins/cert_update.la
tools_plugins_cert_update_la_SOURCES = tools/plugins/cert_update.cc

noinst_LTLIBRARIES += tools/plugins/conf_remap_stripped.la
tools_plugins_conf_remap_stripped_la_SOURCES = tools/plugins/conf_remap_stripped.cc

noinst_LTLIBRARIES += tools/plugins/continuations_verify.la
tools_plugins_continuations_verify_la_SOURCES = tools/plugins/continuations_verify.cc

noinst_LTLIBRARIES += tools/plugins/cont_schedule.la
tools_plugins_cont_schedule_la_SOURCES = tools/plugins/cont_schedule.cc

noinst_LTLIBRARIES += tools/plugins/custom204plugin.la
tools_plugins_custom204plugin_la_SOURCES = tools/plugins/custom204plugin.cc

noinst_LTLIBRARIES += tools/plugins/emergency_shutdown.la
tools_plugins_emergency_shutdown_la_SOURCES = tools/plugins/emergency_shutdown.cc

noinst_LTLIBRARIES += tools/plugins/fatal_shutdown.la
tools_plugins_fatal_shutdown_la_SOURCES = tools/plugins/fatal_shutdown.cc

noinst_LTLIBRARIES += tools/plugins/hook_add_plugin.la
tools_plugins_hook_add_plugin_la_SOURCES = tools/plugins/hook_add_plugin.cc

noinst_LTLIBRARIES += tools/plugins/missing_mangled_definition.la
tools_plugins_missing_mangled_definition_la_SOURCES = \
  tools/plugins/missing_mangled_definition_c.c \
  tools/plugins/missing_mangled_definition_cpp.cc \
  tools/plugins/missing_mangled_definition.h
tools_plugins_missing_mangled_definition_la_LDFLAGS = \
  -module \
  -shared \
  -avoid-version \
  -rpath $(abs_builddir)

noinst_LTLIBRARIES += tools/plugins/missing_ts_plugin_init.la
tools_plugins_missing_ts_plugin_init_la_SOURCES = tools/plugins/missing_ts_plugin_init.cc

noinst_LTLIBRARIES += tools/plugins/ssl_client_verify_test.la
tools_plugins_ssl_client_verify_test_la_SOURCES = tools/plugins/ssl_client_verify_test.cc

noinst_LTLIBRARIES += tools/plugins/ssl_hook_test.la
tools_plugins_ssl_hook_test_la_SOURCES = tools/plugins/ssl_hook_test.cc

noinst_LTLIBRARIES += tools/plugins/ssl_secret_load_test.la
tools_plugins_ssl_secret_load_test_la_SOURCES = tools/plugins/ssl_secret_load_test.cc

noinst_LTLIBRARIES += tools/plugins/ssl_verify_test.la
tools_plugins_ssl_verify_test_la_SOURCES = tools/plugins/ssl_verify_test.cc

noinst_LTLIBRARIES += tools/plugins/ssntxnorder_verify.la
tools_plugins_ssntxnorder_verify_la_SOURCES = tools/plugins/ssntxnorder_verify.cc

noinst_LTLIBRARIES += tools/plugins/test_cppapi.la
tools_plugins_test_cppapi_la_SOURCES = tools/plugins/test_cppapi.cc
tools_plugins_test_cppapi_la_LDFLAGS = \
  $(AM_LDFLAGS) \
  -L$(top_builddir)/src/tscpp/api
tools_plugins_test_cppapi_la_LIBADD = -ltscppapi

noinst_LTLIBRARIES += tools/plugins/test_hooks.la
tools_plugins_test_hooks_la_SOURCES = tools/plugins/test_hooks.cc

noinst_LTLIBRARIES += tools/plugins/test_log_interface.la
tools_plugins_test_log_interface_la_SOURCES = tools/plugins/test_log_interface.cc

noinst_LTLIBRARIES += tools/plugins/user_args.la
tools_plugins_user_args_la_SOURCES = tools/plugins/user_args.cc

noinst_LTLIBRARIES += tools/plugins/async_engine.la
tools_plugins_async_engine_la_SOURCES = tools/plugins/async_engine.cc
tools_plugins_async_engine_la_LDFLAGS = \
  -module \
  -shared \
  -avoid-version \
  -rpath $(abs_builddir)

