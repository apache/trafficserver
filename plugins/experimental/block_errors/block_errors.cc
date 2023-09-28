/*
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

#include <ts/ts.h>
#include <unordered_map>
#include <limits>
#include <tscore/ink_inet.h>
#include <tscore/BufferWriter.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <shared_mutex>
#include <cinttypes>
#include <string_view>
#include <mutex>

#define PLUGIN_NAME "block_errors"
#define PLUGIN_NAME_CLEAN "block_clean"
static uint32_t RESET_LIMIT     = 1000;
static uint32_t TIMEOUT_CYCLES  = 4;
static int StatCountBlocks      = -1;
static bool shutdown_connection = false;
static bool enabled             = true;

//-------------------------------------------------------------------------
static int
msg_hook(TSCont *contp, TSEvent event, void *edata)
{
  TSPluginMsg *msg = static_cast<TSPluginMsg *>(edata);
  std::string_view tag(static_cast<const char *>(msg->tag));
  std::string_view data(static_cast<const char *>(msg->data));

  TSDebug(PLUGIN_NAME, "msg_hook: tag=%s data=%s", tag.data(), data.data());

  if (tag == "block_errors.enabled") {
    enabled = static_cast<bool>(atoi(data.data()));
  } else if (tag == "block_errors.limit") {
    RESET_LIMIT = atoi(data.data());
  } else if (tag == "block_errors.cycles") {
    TIMEOUT_CYCLES = atoi(data.data());
  } else if (tag == "block_errors.shutdown") {
    shutdown_connection = static_cast<bool>(atoi(data.data()));
  } else {
    TSDebug(PLUGIN_NAME, "msg_hook: unknown message tag '%s'", tag.data());
    TSError("block_errors: unknown message tag '%s'", tag.data());
  }

  TSDebug(PLUGIN_NAME, "reset limit: %d per minute, timeout limit: %d minutes, shutdown connection: %d enabled: %d", RESET_LIMIT,
          TIMEOUT_CYCLES, shutdown_connection, enabled);

  return 0;
}

//-------------------------------------------------------------------------
// convert a sockaddr to a string
std::string &
ipaddr_to_string(const IpAddr &ip, std::string &address)
{
  ts::LocalBufferWriter<128> writer;
  writer.print("{}", ip);
  address = writer.view();

  return address;
}

//-------------------------------------------------------------------------
struct IPTableItem {
  uint32_t _count  = 1;
  uint32_t _cycles = 0;
};

//-------------------------------------------------------------------------
class IPTable
{
public:
  IPTable() = default;

  uint32_t
  increment(IpAddr const &ip)
  {
    std::unique_lock lock(_mutex);
    auto item = _table.find(ip);
    if (item == _table.end()) {
      _table.insert(std::make_pair(ip, IPTableItem()));
      return 1;
    } else {
      ++item->second._count;
      uint32_t tmp_count = item->second._count;
      return tmp_count;
    }
  }

  uint32_t
  getCount(IpAddr const &ip)
  {
    std::shared_lock lock(_mutex);
    auto item = _table.find(ip);
    if (item == _table.end()) {
      return 0;
    } else {
      uint32_t tmp_count = item->second._count;
      return tmp_count;
    }
  }

  void
  clean()
  {
    std::string address;
    std::unique_lock lock(_mutex);
    for (auto item = _table.begin(); item != _table.end();) {
      if (item->second._count <= RESET_LIMIT || item->second._cycles >= TIMEOUT_CYCLES) {
        // remove the item if the count is below the limit or the timeout has expired
        TSDebug(PLUGIN_NAME_CLEAN, "ip=%s count=%d removing", ipaddr_to_string(item->first, address).c_str(), item->second._count);
        item = _table.erase(item);
      } else {
        // increment the timeout cycles if the count is above the limit
        if (item->second._cycles == 0) {
          // log only once per ip address per timeout period
          TSError("block_errors: blocking or downgrading ip=%s for %d minutes, reset count=%d",
                  ipaddr_to_string(item->first, address).c_str(), TIMEOUT_CYCLES, item->second._count);
          TSStatIntIncrement(StatCountBlocks, 1);
        }
        ++item->second._cycles;
        TSDebug(PLUGIN_NAME_CLEAN, "ip=%s count=%d incrementing cycles=%d", ipaddr_to_string(item->first, address).c_str(),
                item->second._count, item->second._cycles);
        ++item;
      }
    }
  }

private:
  std::unordered_map<IpAddr, IPTableItem> _table;
  std::shared_mutex _mutex;
};

IPTable ip_table;

//-------------------------------------------------------------------------
static int
handle_start_hook(TSCont *contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "handle_start_hook");
  auto vconn = static_cast<TSVConn>(edata);

  if (enabled == false) {
    TSDebug(PLUGIN_NAME, "plugin disabled");
    TSVConnReenable(vconn);
    return 0;
  }

  // only handle ssl connections
  if (TSVConnIsSsl(vconn) == 0) {
    TSDebug(PLUGIN_NAME, "not a ssl connection");
    TSVConnReenable(vconn);
    return 0;
  }

  // get the ip address
  const sockaddr *addr = TSNetVConnRemoteAddrGet(vconn);
  IpAddr ipaddr(addr);

  // get the count for the ip address
  uint32_t count = ip_table.getCount(ipaddr);
  TSDebug(PLUGIN_NAME, "count=%d", count);

  // if the count is over the limit, shutdown or downgrade the connection
  if (count > RESET_LIMIT) {
    std::string address;
    if (shutdown_connection == true) {
      // shutdown the connection
      TSDebug(PLUGIN_NAME, "ip=%s count=%d is over the limit, shutdown connection on start",
              ipaddr_to_string(ipaddr, address).c_str(), count);
      int fd = TSVConnFdGet(vconn);
      shutdown(fd, SHUT_RDWR);
      char buffer[4096];
      while (read(fd, buffer, sizeof(buffer)) > 0) {
        // drain the connection
      }
    } else {
      // downgrade the connection
      TSDebug(PLUGIN_NAME, "ip=%s count=%d is over the limit, downgrading connection", ipaddr_to_string(ipaddr, address).c_str(),
              count);
      TSVConnProtocolDisable(vconn, TS_ALPN_PROTOCOL_HTTP_2_0);
    }
  }

  TSVConnReenable(vconn);
  return 0;
}

//-------------------------------------------------------------------------
struct Errors {
  uint32_t cls  = 0; // class of error
  uint64_t code = 0; // error code
};

//-------------------------------------------------------------------------
static int
handle_close_hook(TSCont *contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "handle_close_hook");
  auto txnp = static_cast<TSHttpTxn>(edata);

  if (enabled == false) {
    TSDebug(PLUGIN_NAME, "plugin disabled");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  // get the errors from the state machine
  Errors transaction;
  Errors session;
  TSHttpTxnClientReceivedErrorGet(txnp, &transaction.cls, &transaction.code);
  TSHttpTxnClientSentErrorGet(txnp, &session.cls, &session.code);

  // debug if we have an error
  if (transaction.cls != 0 || session.cls != 0 || transaction.code != 0 || session.code != 0) {
    TSDebug(PLUGIN_NAME, "transaction error class=%d code=%" PRIu64 " session error class=%d code=%" PRIu64, transaction.cls,
            transaction.code, session.cls, session.code);
  }

  // count the error if there is a transaction error CANCEL or a session error ENHANCE_YOUR_CALM
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-error-codes
  if ((transaction.cls == 2 && transaction.code == 8) || (session.cls == 1 && session.code == 11)) {
    TSHttpSsn ssn        = TSHttpTxnSsnGet(txnp);
    TSVConn vconn        = TSHttpSsnClientVConnGet(ssn);
    const sockaddr *addr = TSNetVConnRemoteAddrGet(vconn);
    IpAddr ipaddr(addr);
    uint32_t count = ip_table.increment(ipaddr);
    if (count > RESET_LIMIT) {
      std::string address;
      TSDebug(PLUGIN_NAME, "ip=%s count=%d is over the limit, shutdown connection on close",
              ipaddr_to_string(ipaddr, address).c_str(), count);
      int fd = TSVConnFdGet(vconn);
      shutdown(fd, SHUT_RDWR);
    }
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

//-------------------------------------------------------------------------
static int
clean_table(TSCont *contp, TSEvent event, void *edata)
{
  ip_table.clean();
  return 0;
}

//-------------------------------------------------------------------------
void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PLUGIN_NAME, "TSPluginInit");

  // register the plugin
  TSPluginRegistrationInfo info;
  info.plugin_name   = "block_errors";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("Plugin registration failed");
  }

  // set the reset and timeout values
  if (argc == 5) {
    RESET_LIMIT         = atoi(argv[1]);
    TIMEOUT_CYCLES      = atoi(argv[2]);
    shutdown_connection = static_cast<bool>(atoi(argv[3]));
    enabled             = static_cast<bool>(atoi(argv[4]));
  } else if (argc > 1 && argc < 5) {
    TSDebug(PLUGIN_NAME,
            "block_errors: invalid number of arguments, using the defaults - usage: block_errors.so <reset limit> <timeout "
            "cycles> <shutdown connection> <enabled>");
    TSError("block_errors: invalid number of arguments, using the defaults - usage: block_errors.so <reset limit> <timeout cycles> "
            "<shutdown connection> <enabled>");
  }

  TSDebug(PLUGIN_NAME, "reset limit: %d per minute, timeout limit: %d minutes, shutdown connection: %d enabled: %d", RESET_LIMIT,
          TIMEOUT_CYCLES, shutdown_connection, enabled);

  // create a stat counter
  StatCountBlocks = TSStatCreate("block_errors.count", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);

  // register the hooks
  TSHttpHookAdd(TS_VCONN_START_HOOK, TSContCreate(reinterpret_cast<TSEventFunc>(handle_start_hook), nullptr));
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, TSContCreate(reinterpret_cast<TSEventFunc>(handle_close_hook), nullptr));
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(reinterpret_cast<TSEventFunc>(msg_hook), nullptr));

  // schedule cleanup on task thread every 60 seconds
  TSContScheduleEveryOnPool(TSContCreate((TSEventFunc)clean_table, TSMutexCreate()), 60 * 1000, TS_THREAD_POOL_TASK);
}
