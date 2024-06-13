/** @file

  Unit tests for AggregateWriteBuffer

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

#include "main.h"

int  cache_vols           = 1;
bool reuse_existing_cache = false;

// This is a regression test for a bug caught in review. The RegressionSM
// suite did not catch it. Issues related to this would manifest only after
// the cache wraps around, because add() is only used by evacuators.
TEST_CASE("Given 10 bytes are pending to the buffer, "
          "when we add a document with an approximate size of 10, "
          "then there should be 0 bytes pending.")
{
  AggregateWriteBuffer write_buffer;
  Doc                  doc;
  doc.len = sizeof(Doc);
  write_buffer.add_bytes_pending_aggregation(10);
  write_buffer.add(&doc, 10);
  CHECK(0 == write_buffer.get_bytes_pending_aggregation());
}

TEST_CASE("Given 10 bytes are pending to the buffer, "
          "when we emplace a document with an approximate size of 10, "
          "then there should be 0 bytes pending.")
{
  AggregateWriteBuffer write_buffer;
  write_buffer.add_bytes_pending_aggregation(10);
  write_buffer.emplace(10);
  CHECK(0 == write_buffer.get_bytes_pending_aggregation());
}
