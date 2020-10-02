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

#include "tscpp/util/MemSpan.h"
#include "tscore/BufferWriter.h"

#include "rpc/server/CommBase.h"
#include "rpc/config/JsonRpcConfig.h"

namespace rpc::comm
{
///
/// @brief Unix Domain Socket implementation that deals with the JSON RPC call handling mechanism.
///
/// Very basic and straight forward implementation of an unix socket domain. The implementation
/// follows the \link BaseCommInterface.
///
/// Message completion is checked base on the \link MessageParserLigic implementation. As we are not using any
/// user defined protocol more than the defined json, we keep reading till we find a well-formed json or the buffer if full.
///
class LocalUnixSocket : public BaseCommInterface
{
  static constexpr std::string_view _summary{"Local Socket"};

  ///
  /// @brief Connection abstraction class that deals with sending and receiving data from the connected peer.
  ///
  /// When client goes out of scope it will close the socket. If you want to keep the socket connected, you need to keep
  /// the client object around.
  struct Client {
    // Convenience definitons to implement the message boundary logic.
    struct yamlparser;
    using MessageParserLogic = yamlparser;

    /// @param fd Peer's socket.
    Client(int fd);
    /// Destructor will close the socket(if opened);
    ~Client();

    /// Wait for data to be ready for reading.
    /// @return true if the data is ready, false otherwise.
    bool wait_for_data(int timeout = 1000) const;
    /// Close the passed socket (if opened);
    void close();
    /// Reads from the socket, this function calls the system read function.
    /// @return the size of what was read by the read() function.
    ssize_t read(ts::MemSpan<char> span) const;
    /// Function that reads all the data available in the socket, it will validate the data on every read if there is more than
    /// a single chunk.
    /// This function relies on \link MessageParser::is_complete implementation.
    /// The size of the buffer to be read is not defined in this function, but rather passed in the @c bw parameter.
    /// @return false if any error, true otherwise.
    bool read_all(ts::FixedBufferWriter &bw) const;
    /// Write the the socket with the passed data.
    /// @return false if any error, true otherwise.
    bool write(std::string const &data) const;

  private:
    int _fd; ///< connected peer's socket.
  };

public:
  LocalUnixSocket() = default;
  virtual ~LocalUnixSocket() override;

  /// Configure the  local socket.
  ts::Errata configure(YAML::Node const &params) override;
  /// This function will create the socket, bind it and make  it listen to the new socket.
  /// @return the errata with the collected error(if any)
  ts::Errata init() override;

  void run() override;
  bool stop() override;

  std::string_view
  name() const override
  {
    return _summary;
  }

protected: // unit test access
  struct Config {
    static constexpr auto SOCK_PATH_NAME_KEY_STR{"sock_path_name"};
    static constexpr auto LOCK_PATH_NAME_KEY_STR{"lock_path_name"};
    static constexpr auto BACKLOG_KEY_STR{"backlog"};
    static constexpr auto MAX_RETRY_ON_TR_ERROR_KEY_STR{"max_retry_on_transient_errors"};

    static constexpr auto DEFAULT_SOCK_NAME{"/tmp/jsonrpc20.sock"};
    static constexpr auto DEFAULT_LOCK_NAME{"/tmp/jsonrpc20.lock"};

    // Review this names
    std::string sockPathName{DEFAULT_SOCK_NAME};
    std::string lockPathName{DEFAULT_LOCK_NAME};

    int backlog{5};
    int maxRetriesOnTransientErrors{64};
  };

  friend struct YAML::convert<rpc::comm::LocalUnixSocket::Config>;
  Config _conf;

private:
  // TODO: 1000 what? add units.
  bool wait_for_new_client(int timeout = 1000) const;
  bool check_for_transient_errors() const;
  void create_socket(std::error_code &ec);

  int accept(std::error_code &ec) const;
  void bind(std::error_code &ec);
  void listen(std::error_code &ec);

  std::atomic_bool _running;

  struct sockaddr_un _serverAddr;
  int _socket;
};
} // namespace rpc::comm