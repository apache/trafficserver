/** @file

  Reference decoder for the self-describing v3 binary log format.

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

struct LogEntryHeader;
struct LogBufferHeader;

/** Self-describing decode of a single v3 binary-log entry to JSON.

    The reference an out-of-tree decoder mirrors against the v3 spec
    (doc/developer-guide/logging-architecture/binary-log-v3-format.en.rst):
    dispatches purely on the schema's LogField::Type codes, with no global field
    table or symbol->type map.

    Framing, not semantics: each value is emitted in its type's generic form
    (sINT/dINT as JSON number(s), STRING as text, IP as address string). Do NOT
    render field semantics here -- alias maps ("TCP_HIT"), date/hex variants,
    slicing, or anything symbol-specific; the symbol is only the JSON key. That
    is what keeps this a faithful file-only reference.

    The segment may be untrusted, so every read is bounded; a malformed segment
    yields -1 rather than an out-of-bounds read.

    @return bytes written to @a buf (excluding the trailing NUL), or -1 on
            error, on a v2 segment lacking the schema, or on insufficient space.
*/
int log_entry_to_json(LogEntryHeader *entry, LogBufferHeader *header, char *buf, int buf_len);
