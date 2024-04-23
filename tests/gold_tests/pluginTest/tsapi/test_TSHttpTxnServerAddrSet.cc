/*
 * Licensed to the Apache Software Foundation (ASF) under one
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

#include <ts/ts.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdlib>

#include <memory>
#include <string_view>
#include <string>

namespace
{

std::string_view PNAME = "test_plugin";

DbgCtl dbg_ctl{PNAME.data()};

/** Represents the origin address and port that the user specified to which to
 * redirect requests. */
class TargetAddress
{
public:
  /** Constructs a TargetAddress object from the given address and port.
   *
   * @param address The address to which to redirect requests.
   * @param port The port to which to redirect requests.
   */
  TargetAddress(char const *address, int port) : _address(address), _port(port)
  {
    _sockaddr.sin_family = AF_INET;
    _sockaddr.sin_port   = htons(port);
    if (inet_pton(AF_INET, address, &_sockaddr.sin_addr) != 1) {
      TSError("Invalid address %s", address);
      _is_valid = false;
    } else {
      _is_valid = true;
    }
  }

  /** Returns the address to which to redirect requests.
   *
   * @return The address to which to redirect requests.
   */
  std::string_view
  get_address() const
  {
    return _address;
  }

  /** Returns the port to which to redirect requests.
   *
   * @return The port to which to redirect requests.
   */
  int
  get_port() const
  {
    return _port;
  }

  /** Returns whether the address and port are valid.
   *
   * @return Whether the address and port are valid.
   */
  bool
  is_valid() const
  {
    return _is_valid;
  }

  /** Returns the sockaddr representing the user's specified origin address and
   * port.
   *
   * @return The sockaddr representing the user's specified origin address and
   * port.
   */
  sockaddr const *
  get_sockaddr() const
  {
    return reinterpret_cast<sockaddr const *>(&_sockaddr);
  }

private:
  std::string _address;
  int         _port;
  sockaddr_in _sockaddr;
  bool        _is_valid;
};

/** The user specified origin to which requests are redirected. */
std::unique_ptr<TargetAddress> g_target_address{nullptr};

/** Parse the plugin's command line arguments. */
bool
parse_arguments(int argc, char const *argv[])
{
  if (argc != 3) {
    TSError("Must provide the address and port for TSHttpTxnServerAddrSet.");
    return false;
  }
  char const *address = argv[1];
  int         port    = atoi(argv[2]);
  if (port <= 0 || port >= 65536) {
    TSError("Invalid port number %s", argv[2]);
    return false;
  }
  g_target_address = std::make_unique<TargetAddress>(address, port);
  if (!g_target_address->is_valid()) {
    TSError("Invalid address %s:%d", address, port);
    return false;
  }

  return true;
}

/** The handler which sets the user-specified origin. */
int
set_origin(TSCont cont, TSEvent event, void *edata)
{
  if (event != TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE) {
    TSError("Unexpected event: %d", event);
    return TS_ERROR;
  }

  TSHttpTxn txnp = (TSHttpTxn)edata;
  if (TSHttpTxnServerAddrSet(txnp, g_target_address->get_sockaddr()) != TS_SUCCESS) {
    TSError("Failed to set a transaction's origin to %s:%d", g_target_address->get_address().data(), g_target_address->get_port());
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_ERROR;
  }
  Dbg(dbg_ctl, "Successfully set a transaction's origin to %s:%d", g_target_address->get_address().data(),
      g_target_address->get_port());
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

} // anonymous namespace

void
TSPluginInit(int argc, char const *argv[])
{
  Dbg(dbg_ctl, "Initializing plugin.");

  TSPluginRegistrationInfo info;
  info.plugin_name   = PNAME.data();
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";
  TSReleaseAssert(TSPluginRegister(&info) == TS_SUCCESS);

  if (!parse_arguments(argc, argv)) {
    TSError("Failed to parse arguments.");
    return;
  }

  Dbg(dbg_ctl, "Redirecting all requests to %s:%s", argv[1], argv[2]);

  TSCont contp = TSContCreate(set_origin, nullptr);
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
}
