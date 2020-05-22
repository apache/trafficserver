/** @txn_data.h
  Traffic Dump data specific to transactions.
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

#include <string>

#include "ts/ts.h"

#include "sensitive_fields.h"

namespace traffic_dump
{
/** The information associated with an single transaction.
 *
 * This class is responsible for containing the members associated with a
 * particular transaction and defines the transaction handler callback.
 */
class TransactionData
{
private:
  /** The string for the JSON content of this transaction. */
  std::string txn_json;

  // The index to be used for the TS API for storing this TransactionData on a
  // per-transaction basis.
  static int transaction_arg_index;

  /// The set of fields, default and user-specified, that are sensitive and
  /// whose values will be replaced with auto-generated generic content.
  static sensitive_fields_t sensitive_fields;

public:
  /** Initialize TransactionData, using the provided sensitive fields.
   *
   * @return True if initialization is successful, false otherwise.
   */
  static bool init(sensitive_fields_t &&sensitive_fields);

  /** Initialize TransactionData, using default sensitive fields.
   * @return True if initialization is successful, false otherwise.
   */
  static bool init();

  /// Read the txn information from TSMBuffer and write the header information.
  /// This function does not write the content node.
  std::string write_message_node_no_content(TSMBuffer &buffer, TSMLoc &hdr_loc);

  /// Read the txn information from TSMBuffer and write the header information including
  /// the content node describing the body characteristics.
  std::string write_message_node(TSMBuffer &buffer, TSMLoc &hdr_loc, int64_t num_body_bytes);

  /// The handler callback for transaction events.
  static int global_transaction_handler(TSCont contp, TSEvent event, void *edata);

private:
  /** Common logic for the init overloads. */
  static bool init_helper();

  /** Initialize the generic sensitive field to be dumped. This is used instead
   * of the sensitive field values seen on the wire.
   */
  static void initialize_default_sensitive_field();

  /** Return a separated string representing the HTTP fields considered sensitive.
   *
   * @return A comma-separated string representing the sensitive HTTP fields.
   */
  static std::string get_sensitive_field_description();

  /** Inspect the field to see whether it is sensitive and return a generic value
   * of equal size to the original if it is.
   *
   * @param[in] name The field name to inspect.
   * @param[in] original_value The field value to inspect.
   *
   * @return The value traffic_dump should dump for the given field.
   */
  std::string_view replace_sensitive_fields(std::string_view name, std::string_view original_value);

  /// Write the content JSON node for an HTTP message.
  //
  /// "content"
  ///    "encoding"
  ///    "size"
  std::string write_content_node(int64_t num_body_bytes);

  /** Remove the scheme prefix from the url.
   *
   * @return The view without the scheme prefix.
   */
  std::string_view remove_scheme_prefix(std::string_view url);
};

} // namespace traffic_dump
