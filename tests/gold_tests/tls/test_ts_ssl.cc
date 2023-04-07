/** @file test_ts_ssl

  Plugin for exclusive use with ts_ssl Au test.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include <string>
#include <string_view>
#include <fstream>

#include <ts/ts.h>

#include <tscpp/api/Cleanup.h>

namespace
{
atscppapi::TSDbgCtlUniqPtr dbg_ctl{TSDbgCtlCreate("ts_ssl")};

#define DBG(...) TSDbg(dbg_ctl.get(), __VA_ARGS__)

char const Plugin_name[] = "test_ts_ssl";

struct File_path_and_data {
  std::string path;
  std::string data;
};

File_path_and_data tls_cert_2050, tls_cert_2060, tls_key;

int cont_func(TSCont, TSEvent, void *);

atscppapi::TSContUniqPtr cont{TSContCreate(cont_func, nullptr)};

int txn_num, secret_hook_invocation_num;

enum Txn_num { CHECK_2050_EXPIRATION = 1, SET_2060_EXPIRATION, CHECK_2060_EXPIRATION };

void
check_secret(std::string const &name, std::string const &nominal_data)
{
  int actual_length;
  char const *actual_data = TSSslSecretGet(name.c_str(), name.size(), &actual_length);
  TSReleaseAssert(actual_data != nullptr);
  TSReleaseAssert(actual_length != 0);
  TSReleaseAssert(std::string_view(actual_data, actual_length) == nominal_data);
}

int
cont_func(TSCont, TSEvent event, void *event_data)
{
  if (TS_EVENT_SSL_SECRET == event) {
    ++secret_hook_invocation_num;
    DBG("Lifecycle SSL Secret hook invocation number %d", secret_hook_invocation_num);
    return TS_SUCCESS;
  }

  TSReleaseAssert(TS_EVENT_HTTP_READ_REQUEST_HDR == event);
  TSReleaseAssert(event_data != nullptr);

  ++txn_num;
  DBG("HTTP request number number %d", txn_num);

  switch (txn_num) {
  case CHECK_2050_EXPIRATION:
    check_secret(tls_cert_2050.path, tls_cert_2050.data);
    check_secret(tls_key.path, tls_key.data);
    break;

  case SET_2060_EXPIRATION:
    check_secret(tls_cert_2050.path, tls_cert_2050.data);
    check_secret(tls_key.path, tls_key.data);

    TSReleaseAssert(TSSslSecretSet(tls_cert_2050.path.data(), tls_cert_2050.path.size(), tls_cert_2060.data.data(),
                                   tls_cert_2060.data.size()) == TS_SUCCESS);
    TSReleaseAssert(TSSslSecretUpdate(tls_cert_2050.path.data(), tls_cert_2050.path.size()) == TS_SUCCESS);

    check_secret(tls_cert_2050.path, tls_cert_2060.data);
    check_secret(tls_key.path, tls_key.data);

    break;

  case CHECK_2060_EXPIRATION:
    check_secret(tls_cert_2050.path, tls_cert_2060.data);
    check_secret(tls_key.path, tls_key.data);
    break;

  default:
    TSReleaseAssert(false);
  }

  TSHttpTxnReenable(static_cast<TSHttpTxn>(event_data), TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

std::string
load_file(char const *file_spec)
{
  std::ifstream is{file_spec, std::ios::binary | std::ios::ate};
  TSReleaseAssert(!is.fail());
  auto size = is.tellg();
  TSReleaseAssert(!is.fail());
  std::string file_data(size, '\0');
  is.seekg(0);
  TSReleaseAssert(!is.fail());
  is.read(file_data.data(), size);
  TSReleaseAssert(!is.fail());
  return file_data;
}

} // End of anonymous namespace

void
TSPluginInit(int n_arg, char const *arg[])
{
  TSReleaseAssert(2 == n_arg);

  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>(Plugin_name);
  info.vendor_name   = const_cast<char *>("Yahoo");
  info.support_email = const_cast<char *>("ats-devel@yahooinc.com");
  TSReleaseAssert(TSPluginRegister(&info) == TS_SUCCESS);

  tls_cert_2050.path = std::string(arg[1]) + "/2050.crt";
  tls_cert_2050.data = load_file(tls_cert_2050.path.c_str());

  tls_cert_2060.path = std::string(arg[1]) + "/2060.crt";
  tls_cert_2060.data = load_file(tls_cert_2060.path.c_str());

  tls_key.path = std::string(arg[1]) + "/2050_2060.key";
  tls_key.data = load_file(tls_key.path.c_str());

  TSLifecycleHookAdd(TS_LIFECYCLE_SSL_SECRET_HOOK, cont.get());
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont.get());

  DBG("TSPluginInit() completed.");

  return;
}
