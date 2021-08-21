/** @file

  Traffic Dump session handling encapsulation.

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

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <optional>

#include "ts/ts.h"
#include "tscore/ts_file.h"

namespace traffic_dump
{
/** The information associated with an individual session.
 *
 * This class is responsible for containing the members associated with a
 * particular session and defines the session handler callback.
 */
class SessionData
{
public:
  /// By default, Traffic Dump logs will go into a directory called "dump".
  static char const constexpr *const default_log_directory = "dump";
  /// By default, 1 out of 1000 sessions will be dumped.
  static constexpr int64_t default_sample_pool_size = 1000;
  /// By default, logging will stop after 10 MB have been dumped.
  static constexpr int64_t default_max_disk_usage = 10 * 1000 * 1000;

private:
  //
  // Instance Variables
  //

  /// Log file descriptor for this session's dump file.
  int log_fd = -1;
  /// The count of the currently outstanding AIO operations.
  int aio_count = 0;
  /// The offset of the last point written to so for in the dump file for this
  /// session.
  int64_t write_offset = 0;
  /// Whether this session has been closed.
  bool ssn_closed = false;
  /// The filename for this session's dump file.
  ts::file::path log_name;
  /// Whether the first transaction in this session has been written.
  bool has_written_first_transaction = false;
  /// The HTTP version specified in the client protocol stack, or empty string
  /// if it was not specified.
  std::string http_version_in_client_stack;

  TSCont aio_cont = nullptr; /// AIO continuation callback
  TSCont txn_cont = nullptr; /// Transaction continuation callback

  // The following has to be recursive because the stack does not unwind
  // between event invocations.
  std::recursive_mutex disk_io_mutex; /// The mutex for guarding IO calls.

  //
  // Static Variables
  //

  // The index to be used for the TS API for storing this SessionData on a
  // per-session basis.
  static int session_arg_index;

  /// The rate at which to dump sessions. Every one out of sample_pool_size will
  /// be dumped.
  static std::atomic<int64_t> sample_pool_size;
  /// The maximum space logs should take up before stopping the dumping of new
  /// sessions.
  static std::atomic<int64_t> max_disk_usage;
  /// The amount of bytes currently written to dump files.
  static std::atomic<int64_t> disk_usage;

  /// The directory into which to put the dump files.
  static ts::file::path log_directory;

  /// Only sessions with this SNI will be dumped (if set).
  static std::string sni_filter;

  /// The running counter of all sessions dumped by traffic_dump.
  static uint64_t session_counter;

public:
  SessionData();
  ~SessionData();

  /** The getter for the session_arg_index value. */
  static int get_session_arg_index();

  /** Initialize the cross-session values of managing sessions.
   *
   * @return True if initialization is successful, false otherwise.
   */
  static bool init(std::string_view log_directory, int64_t max_disk_usage, int64_t sample_size);
  static bool init(std::string_view log_directory, int64_t max_disk_usage, int64_t sample_size, std::string_view sni_filter);

  /** Set the sample_pool_size to a new value.
   *
   * @param[in] new_sample_size The new value to set for sample_pool_size.
   */
  static void set_sample_pool_size(int64_t new_sample_size);

  /** Reset the disk usage counter to 0. */
  static void reset_disk_usage();

  /** Set the max_disk_usage to a new value.
   *
   * @param[in] new_max_disk_usage The new value to set for max_disk_usage.
   */
  static void set_max_disk_usage(int64_t new_max_disk_usage);

  /** Get the JSON string that describes the server session stack.
   *
   * The server side protocol description may change on a per-transaction
   * basis. Therefore we print this for each transaction and take an TSHttpTxn
   * instead of a TSHttpSsn that the analogous get_client_protocol_description
   * receives.
   *
   * @param[in] txnp The reference to the transaction.
   *
   * @return A JSON description of the server protocol stack for the
   * transaction.
   */
  std::string get_server_protocol_description(TSHttpTxn txnp);

  /** Write the string to the session's dump file.
   *
   * @param[in] content The content to write to the file.
   *
   * @return TS_SUCCESS if the write is successfully scheduled with the AIO
   * system, TS_ERROR otherwise.
   */
  int write_to_disk(std::string_view content);

  /** Write the transaction to the session's dump file.
   *
   * @param[in] content The transaction content to write to the file.
   *
   * @return TS_SUCCESS if the write is successfully scheduled with the AIO
   * system, TS_ERROR otherwise.
   */
  int write_transaction_to_disk(std::string_view content);

  /** The HTTP version specified in the client-side protocol stack.
   *
   * The client protocol stack is obtained at session negotiation, before HTTP
   * traffic is passed. So it may contain stack information if it was
   * negotiated in the TLS handshake, as is often the case with HTTP/2, but it
   * may not. This function returns whether the protocol stack contained HTTP
   * information or not.
   *
   * @return The HTTP version in the client stack or empty string if it was not
   * specified.
   */
  std::string get_http_version_in_client_stack() const;

private:
  /** Write the string to the session's dump file.
   *
   * This assumes that the caller acquired the required AIO lock.
   *
   * @param[in] content The content to write to the file.
   *
   * @return TS_SUCCESS if the write is successfully scheduled with the AIO
   * system, TS_ERROR otherwise.
   */
  int write_to_disk_no_lock(std::string_view content);

  using get_protocol_stack_f  = std::function<TSReturnCode(int, const char **, int *)>;
  using get_tls_description_f = std::function<std::string()>;
  using handle_http_version_f = std::function<void(std::string_view)>;

  /** Create the protocol stack for a session.
   *
   * This function encapsulates the logic common between the client-side and
   * server-side logic for populating a protocol stack.
   *
   * @param[in] get_protocol_stack The function to use to populate a protocol
   * stack.
   *
   * @param[in] get_tls_node The function to use to populate the tls node.
   *
   * @param[in] handle_http_version A function that performs arbitrary logic
   * given the HTTP/2 protocol version.
   *
   * @return The description of the protocol stack and True if the stack
   * contained an HTTP description, false otherwise.
   */
  std::string get_protocol_stack_helper(const get_protocol_stack_f &get_protocol_stack, const get_tls_description_f &get_tls_node,
                                        const handle_http_version_f &handle_http_version);

  /** Get the JSON string that describes the client session stack.
   *
   * @param[in] ssnp The reference to the client session.
   *
   * @return A description of the client protocol stack and True if the stack
   * contained an HTTP description, false otherwise.
   */
  std::string get_client_protocol_description(TSHttpSsn ssnp);

  /** The handler callback for when async IO is done. Used for cleanup. */
  static int session_aio_handler(TSCont contp, TSEvent event, void *edata);

  /** The handler callback for session events. */
  static int global_session_handler(TSCont contp, TSEvent event, void *edata);
};

} // namespace traffic_dump
