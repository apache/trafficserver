/** @file
 *
 * XDebug plugin common types and definitions.
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "ts/ts.h"
#include "tscpp/api/Cleanup.h"

namespace xdebug
{

enum class ProbeType { PROBE_STANDARD, PROBE_FULL_JSON };

/** Encoding strategy for embedding the origin server body in probe-full-json
 * output.
 */
enum class BodyEncoding_t {
  AUTO,      ///< Auto-detect the encoding based on the original response Content-Type.
  ESCAPE,    ///< JSON-escape the body.
  HEX,       ///< Hex-encode the body, treating it as binary data.
  OMIT_BODY, ///< Omit the body entirely.
};

struct BodyBuilder {
  atscppapi::TSContUniqPtr     transform_connp;
  atscppapi::TSContUniqPtr     resolve_encoding_connp;
  atscppapi::TSIOBufferUniqPtr output_buffer;
  // It's important that output_reader comes after output_buffer so it will be deleted first.
  atscppapi::TSIOBufferReaderUniqPtr output_reader;
  TSVIO                              output_vio    = nullptr;
  bool                               wrote_prebody = false;
  bool                               wrote_body    = false;
  bool                               hdr_ready     = false;
  std::atomic_flag                   wrote_postbody;
  ProbeType                          probe_type = ProbeType::PROBE_STANDARD;

  BodyEncoding_t body_encoding       = BodyEncoding_t::AUTO;
  bool           server_body_started = false;

  int64_t nbytes = 0;
};

struct XDebugTxnAuxData {
  std::unique_ptr<BodyBuilder> body_builder;
  unsigned                     xheaders = 0;
};

extern atscppapi::TxnAuxMgrData mgrData;
using AuxDataMgr = atscppapi::TxnAuxDataMgr<XDebugTxnAuxData, mgrData>;

} // namespace xdebug
