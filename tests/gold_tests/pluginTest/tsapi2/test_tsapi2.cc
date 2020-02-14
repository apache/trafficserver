/* Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
Additional regression testing code for TS API.
*/

#include <string>
#include <cstdarg>
#include <cstdio>
#include <cinttypes>
#include <string_view>
#include <utility>
#include <mutex>

#include <sys/socket.h>
#include <netinet/in.h>

#include <yaml-cpp/yaml.h>
#include <ts/ts.h>

namespace Tsapi2Test
{
char PIName[] = "test_tsapi2";

class AccessYaml
{
public:
  AccessYaml() {}

  void
  init(char const *file_path)
  {
    _n = YAML::LoadFile(file_path);
  }

  // This code:
  //
  // YAML::Node n = YAML::LoadFile(file_path);
  // T v;
  // v = n[Key1][Key2]...[KeyN].as<T>();
  //
  // (with zero or more keys) is equlavlent to:
  //
  // AccessYaml n;
  // n.init(file_path);
  // T v;
  // n.get_unlocked(v, KeyN, ..., Key2, Key1);

  template <typename T, typename... Args>
  void
  get_unlocked(T &v, Args &&... args)
  {
    YAML::Node n = _get_node(std::forward<Args>(args)...);
    v            = n.as<T>();
  }

  template <typename T>
  void
  get_unlocked(T &v)
  {
    v = _n.as<T>();
  }

  // Protect access with a mutex.
  //
  template <typename... Args>
  void
  operator()(Args &&... args)
  {
    std::lock_guard<std::mutex> lg(_mtx);

    get_unlocked(std::forward<Args>(args)...);
  }

  // No copying or moving.
  //
  AccessYaml(AccessYaml const &) = delete;
  AccessYaml &operator=(AccessYaml const &) = delete;

private:
  template <typename KT, typename... Args>
  YAML::Node
  _get_node(const KT &k, Args &&... args)
  {
    return _get_node(std::forward<Args>(args)...)[k];
  }

  YAML::Node
  _get_node()
  {
    return _n;
  }

  YAML::Node _n;
  std::mutex _mtx;
};

AccessYaml yaml_data;

std::string run_dir_path;
std::uint16_t server_port, mute_server_port;

// Each wave of transactions for this test runs in parallel, so no guaranteed order.  To avoid variations in order
// of log output for different transactions, transactions that are part of the same wave must log to different files.
// In order to be available for gold file comparison, the log files are flushed on the TXN_CLOSE hook.

class Logger
{
public:
  void
  open(std::string const &log_file_path)
  {
    fp = std::fopen(log_file_path.c_str(), "w");
    TSReleaseAssert(fp != nullptr);
  }

  void
  flush()
  {
    std::fflush(fp);
  }

  void
  close()
  {
    std::fclose(fp);
  }

  void
  operator()(char const *fmt, ...)
  {
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(fp, fmt, args);
    va_end(args);
    fputc('\n', fp);
  }

private:
  FILE *fp{nullptr};
};

// Global hooks will trigger for all tests.  This class returns the TxnID string for a session or transaction,
// so a hook continuation function can determine which test transaction triggered it.
//
class GetTxnID
{
public:
  static void
  init()
  {
    TSReleaseAssert(TSUserArgIndexReserve(TS_USER_ARGS_SSN, "test_tsapi2", "client port", &ssnArgIndex) == TS_SUCCESS);
  }

  GetTxnID(TSHttpSsn ssnp) { _set_txn_id(ssnp); }

  GetTxnID(TSHttpTxn txn)
  {
    TSHttpSsn ssnp = TSHttpTxnSsnGet(txn);

    TSReleaseAssert(!!ssnp);

    _set_txn_id(ssnp);
  }

  operator std::string const &() const { return _txn_id; }

  std::string const &
  txn_id() const
  {
    return _txn_id;
  }

private:
  std::string _txn_id;

  static inline int ssnArgIndex{-1};

  void
  _set_txn_id(TSHttpSsn ssnp)
  {
    // A unique proxy port is configure for each HTTP transaction peformed by this Au test.  The incoming
    // proxy port is used to determine the transaction ID string.

    auto port = reinterpret_cast<std::uintptr_t>(TSUserArgGet(ssnp, ssnArgIndex));

    if (!port) {
      sockaddr const *sock_addr = TSHttpSsnIncomingAddrGet(ssnp);

      TSReleaseAssert(nullptr != sock_addr);

      TSReleaseAssert(AF_INET == sock_addr->sa_family);

      auto sock_addr_in = reinterpret_cast<sockaddr_in const *>(sock_addr);

      port = ntohs(sock_addr_in->sin_port);

      TSUserArgSet(ssnp, ssnArgIndex, reinterpret_cast<void *>(port));
    }

    yaml_data(_txn_id, port, "proxy_port_to_txn");
  }
};

// Returns true if X-Request-ID (for requests) and X-Response-ID (for responses) field in the HTTP message header
// contains test_num as the value.  When the status parameter is not none, this indicates the message is a response,
// and this function also checks that the response status is the status given by the parameter.  If test_num is less
// than 0, no ID field check is done.
//
template <typename HandleT>
bool
checkHttpTxnReqOrResp(Logger &log, HandleT hndl, TSReturnCode (*func)(HandleT, TSMBuffer *, TSMLoc *), char const *label,
                      int test_num, TSHttpStatus status = TS_HTTP_STATUS_NONE)
{
  TSMBuffer bufp;
  TSMLoc mloc;

  if (func(hndl, &bufp, &mloc) != TS_SUCCESS) {
    log("Unable to get handle to %s", label);
    return false;
  }

  bool is_response = (status != TS_HTTP_STATUS_NONE);

  if (is_response) {
    TSHttpStatus status_in_msg = TSHttpHdrStatusGet(bufp, mloc);
    if (status_in_msg != status) {
      log("Status in message (%d) is not the expected status (%d)", static_cast<int>(status_in_msg), static_cast<int>(status));
      return false;
    }
  }

  if (test_num >= 0) {
    std::string_view checked_fld_name{is_response ? "X-Response-ID" : "X-Request-ID"};

    TSMLoc fld_loc = TSMimeHdrFieldFind(bufp, mloc, checked_fld_name.data(), checked_fld_name.length());
    if (TS_NULL_MLOC == fld_loc) {
      log("Unable to find %s field in %s", checked_fld_name.data(), label);
      return false;
    }

    bool value_is_test_num =
      (TSMimeHdrFieldValuesCount(bufp, mloc, fld_loc) == 1) && (TSMimeHdrFieldValueIntGet(bufp, mloc, fld_loc, 0) == test_num);

    TSReleaseAssert(TSHandleMLocRelease(bufp, mloc, fld_loc) == TS_SUCCESS);

    if (value_is_test_num) {
      log("%s ok", label);
    } else {
      log("value of %s field %s is not %d", label, checked_fld_name.data(), test_num);
      return false;
    }
  }
  return true;
}

} // namespace Tsapi2Test

using namespace Tsapi2Test;

// Individual tests are in separate header files.  They depend on the contents of the anonymous namespace in this
// header file.
//
#include "hooks.h"
#include "cache.h"
#include "ssn.h"
#include "transform.h"
#include "parent_proxy.h"
#include "alt_info.h"

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PIName;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("%s: Plugin registration failed", PIName);

    return;
  }

  yaml_data.init(argv[1]);

  yaml_data.get_unlocked(run_dir_path, "run_dir_path");
  yaml_data.get_unlocked(server_port, "server_port");
  yaml_data.get_unlocked(mute_server_port, "mute_server_port");

  GetTxnID::init();

  HooksTest::init();
  SsnTest::init();
  CacheTest::init();
  TransformTest::init();
  ParentProxyTest::init();
  AltInfoTest::init();
}

namespace
{
class Cleanup
{
public:
  ~Cleanup()
  {
    HooksTest::cleanup();
    SsnTest::cleanup();
    CacheTest::cleanup();
    TransformTest::cleanup();
    ParentProxyTest::cleanup();
    AltInfoTest::cleanup();
  }
};

// Do any needed cleanup for this source file at program termination time.
//
Cleanup cleanup;

} // end anonymous namespace
