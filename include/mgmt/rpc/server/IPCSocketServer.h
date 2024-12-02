/* @file
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

#pragma once

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <string_view>
#include <memory>

#include "swoc/MemSpan.h"
#include "swoc/BufferWriter.h"
#include "tsutil/ts_bw_format.h"
#include "tsutil/ts_errata.h"
#include "tscore/Layout.h"

#include "mgmt/rpc/server/CommBase.h"
#include "mgmt/rpc/config/JsonRPCConfig.h"
#include "shared/rpc/MessageStorage.h"

namespace rpc::comm
{
///
/// @brief Unix Domain Socket implementation that deals with the JSON RPC call handling mechanism.
///
/// Very basic and straight forward implementation of an unix socket domain. The implementation
/// follows the \link BaseCommInterface.
///
/// @note The server will keep reading the client's requests till the buffer is full or there is no more data in the wire.
///       Buffer size = 32k
class IPCSocketServer : public BaseCommInterface
{
  // Error codes to track any unauthorized call to a rpc handler.
  enum class UnauthorizedErrorCode {
    PEER_CREDENTIALS_ERROR = 1, ///< Error while trying to read the peer credentials from the unix socket.
    PERMISSION_DENIED      = 2  ///< Client's socket credential didn't wasn't sufficient to execute the method.
  };
  static const size_t INTERNAL_BUFFER_SIZE{32000};
  using Buffer = MessageStorage<INTERNAL_BUFFER_SIZE>;
  ///
  /// @brief Connection abstraction class that deals with sending and receiving data from the connected peer.
  ///
  /// When client goes out of scope it will close the socket. If you want to keep the socket connected, you need to keep
  /// the client object around.
  struct Client {
    /// @param fd Peer's socket.
    Client(int fd, size_t max_req_size);
    /// Destructor will close the socket(if opened);
    ~Client();

    /// Close the passed socket (if opened);
    void close();
    /// Reads from the socket, this function calls the system read function.
    /// @return the size of what was read by the read() function.
    ssize_t read(swoc::MemSpan<char> span) const;
    /// Function that reads all the data available in the socket, it will validate the data on every read if there is more than
    /// a single chunk.
    /// The size of the buffer to be read is not defined in this function, but rather passed in the @c bw parameter.
    /// @return A tuple with a boolean flag indicating if the operation did success or not, in case of any error, a text will
    /// be added with a description.
    std::tuple<bool, std::string> read_all(Buffer &bw) const;
    /// Write the the socket with the passed data.
    /// @return std::error_code.
    void write(std::string const &data, std::error_code &ec) const;
    bool is_connected() const;

  private:
    /// Wait for data to be ready for reading.
    /// @return true if the data is ready, false otherwise.
    bool   poll_for_data(std::chrono::milliseconds timeout) const;
    int    _fd;           ///< connected peer's socket.
    size_t _max_req_size; ///< Max incoming request size.
  };

public:
  IPCSocketServer() = default;

  /// Configure the  local socket.
  bool configure(YAML::Node const &params) override;
  /// This function will create the socket, bind it and make  it listen to the new socket.
  /// @return the std::error_code with the collected error(if any)
  std::error_code init() override;

  void run() override;
  bool stop() override;

  std::string const &
  name() const override
  {
    return _name;
  }

protected: // unit test access
  struct Config {
    Config();
    static constexpr auto SOCK_PATH_NAME_KEY_STR{"sock_path_name"};
    static constexpr auto LOCK_PATH_NAME_KEY_STR{"lock_path_name"};
    static constexpr auto BACKLOG_KEY_STR{"backlog"};
    static constexpr auto MAX_RETRY_ON_TR_ERROR_KEY_STR{"max_retry_on_transient_errors"};
    static constexpr auto RESTRICTED_API{"restricted_api"};
    static constexpr auto MAX_BUFFER_SIZE{"incoming_request_max_size"};
    // is it safe to call Layout now?
    std::string sockPathName;
    std::string lockPathName;

    int  backlog{5};
    int  maxRetriesOnTransientErrors{64};
    bool restrictedAccessApi{
      NON_RESTRICTED_API}; // This config value will drive the permissions of the jsonrpc socket(either 0700(default) or 0777).
    size_t incomingRequestMaxBufferSize{INTERNAL_BUFFER_SIZE * 3};
  };

  friend struct YAML::convert<rpc::comm::IPCSocketServer::Config>;
  Config _conf;

private:
  inline static const std::string _name = "Local Socket";
  bool                            poll_for_new_client(std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) const;
  void                            create_socket(std::error_code &ec);

  int  accept(std::error_code &ec) const;
  void bind(std::error_code &ec);
  void listen(std::error_code &ec);
  void close();
  void late_check_peer_credentials(int peedFd, TSRPCHandlerOptions const &options, swoc::Errata &errata) const;

  std::atomic_bool _running;

  struct sockaddr_un _serverAddr;
  int                _socket{-1};
  int                _lock_fd{-1};
};
} // namespace rpc::comm
