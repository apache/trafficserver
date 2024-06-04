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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"
#include "cripts/Bundles/LogsMetrics.hpp"

namespace Bundle
{
const Cript::string LogsMetrics::_name = "Bundle::LogsMetrics";

namespace
{
  enum {
    PROPSTAT_CACHE_MISS = 0,  // TS_CACHE_LOOKUP_MISS == 0
    PROPSTAT_CACHE_HIT_STALE, // TS_CACHE_LOOKUP_HIT_STALE == 1
    PROPSTAT_CACHE_HIT_FRESH, // TS_CACHE_LOOKUP_HIT_FRESH == 2
    PROPSTAT_CACHE_SKIP,      // TS_CACHE_LOOKUP_SKIPPED == 3
    PROPSTAT_CLIENT_BYTES_IN,
    PROPSTAT_CLIENT_BYTES_OUT,
    PROPSTAT_RESPONSE_2xx,
    PROPSTAT_RESPONSE_200,
    PROPSTAT_RESPONSE_206,
    PROPSTAT_RESPONSE_3xx,
    PROPSTAT_RESPONSE_4xx,
    PROPSTAT_RESPONSE_404,
    PROPSTAT_RESPONSE_5xx,
    PROPSTAT_RESPONSE_504,
    PROPSTAT_SERVER_BYTES_IN,
    PROPSTAT_SERVER_BYTES_OUT,
    PROPSTAT_CLIENT_ABORTED_REQUESTS,
    PROPSTAT_CLIENT_COMPLETED_REQUESTS,

    PROPSTAT_MAX
  };

  // This has to align with the enum above!!
  static constexpr Cript::string_view PROPSTAT_SUFFIXES[] = {
    // clang-format off
    "cache.miss",
    "cache.hit_stale",
    "cache.hit_fresh",
    "cache.skip",
    "client.bytes_in",
    "client.bytes_out",
    "http.2XX_responses",
    "http.200_responses",
    "http.206_responses",
    "http.3XX_responses",
    "http.4XX_responses",
    "http.404_responses",
    "http.5XX_responses",
    "http.504_responses",
    "server.bytes_in",
    "server.bytes_out",
    "http.aborted_requests",
    "http.completed_requests"
    // clang-format on
  };

} // namespace

// The .propstats(str) is particularly complicated, since it has to create all the metrics
LogsMetrics::self_type &
LogsMetrics::propstats(const Cript::string_view &label)
{
  _label = label;

  if (_label.length() > 0) {
    needCallback({Cript::Callbacks::DO_CACHE_LOOKUP, Cript::Callbacks::DO_TXN_CLOSE});

    for (int ix = 0; ix < Bundle::PROPSTAT_MAX; ++ix) {
      auto name = fmt::format("{}.{}", _label, Bundle::PROPSTAT_SUFFIXES[ix]);

      _inst->debug("Creating metrics for: {}", name);
      _inst->metrics[ix] = Metrics::Counter::create(name);
    }
  }

  return *this;
}

void
LogsMetrics::doTxnClose(Cript::Context *context)
{
  borrow resp = Client::Response::get();

  // .tcpinfo(bool)
  if (_tcpinfo && control.logging.get()) {
    borrow conn = Client::Connection::get();

    resp["@TCPInfo"] += fmt::format(",TC; {}", conn.tcpinfo.log());
  }

  // .label(str)
  if (_label.length() > 0) {
    instance.metrics[Bundle::PROPSTAT_CLIENT_BYTES_IN]->increment(TSHttpTxnClientReqHdrBytesGet(transaction.txnp) +
                                                                  TSHttpTxnClientReqBodyBytesGet(transaction.txnp));
    instance.metrics[Bundle::PROPSTAT_CLIENT_BYTES_OUT]->increment(TSHttpTxnClientRespHdrBytesGet(transaction.txnp) +
                                                                   TSHttpTxnClientRespBodyBytesGet(transaction.txnp));
    instance.metrics[Bundle::PROPSTAT_SERVER_BYTES_IN]->increment(TSHttpTxnServerReqHdrBytesGet(transaction.txnp) +
                                                                  TSHttpTxnServerReqBodyBytesGet(transaction.txnp));
    instance.metrics[Bundle::PROPSTAT_SERVER_BYTES_OUT]->increment(TSHttpTxnServerRespHdrBytesGet(transaction.txnp) +
                                                                   TSHttpTxnServerRespBodyBytesGet(transaction.txnp));

    if (resp.status == 200) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_200]->increment();
    } else if (resp.status == 206) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_206]->increment();
    } else if (resp.status == 404) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_404]->increment();
    } else if (resp.status == 504) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_504]->increment();
    } else if (resp.status >= 200 && resp.status < 300) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_2xx]->increment();
    } else if (resp.status >= 300 && resp.status < 400) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_3xx]->increment();
    } else if (resp.status >= 400 && resp.status < 500) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_4xx]->increment();
    } else if (resp.status >= 500 && resp.status < 600) {
      instance.metrics[Bundle::PROPSTAT_RESPONSE_5xx]->increment();
    }

    if (transaction.aborted()) {
      instance.metrics[Bundle::PROPSTAT_CLIENT_ABORTED_REQUESTS]->increment();
    } else {
      instance.metrics[Bundle::PROPSTAT_CLIENT_COMPLETED_REQUESTS]->increment();
    }
  }
}

void
LogsMetrics::doSendResponse(Cript::Context *context)
{
  borrow resp = Client::Response::get();
  borrow conn = Client::Connection::get();

  // .sample(int)
  if (_log_sample > 0) {
    resp["@sampleratio"] = _log_sample;
  }

  // .tcpinfo(bool)
  if (_tcpinfo && control.logging.get()) {
    resp["@TCPInfo"] = fmt::format("SR; {}", conn.tcpinfo.log());
  }
}

void
LogsMetrics::doCacheLookup(Cript::Context *context)
{
  auto status = transaction.lookupStatus();

  // .label(str)
  if (_label.length() > 0) {
    if (status >= 0 && status <= 3) {
      instance.metrics[status]->increment(); // This assumes the 4 cache stats are first
    }
  }
}

void
LogsMetrics::doRemap(Cript::Context *context)
{
  bool sampled = false;

  // .logsample(int)
  if ((_log_sample > 0) && (Cript::random(_log_sample) != 1)) {
    control.logging.set(false);
    sampled = true;
  }

  // .tcpinfo(bool)
  if (_tcpinfo && !sampled) {
    borrow req      = Client::Request::get();
    borrow conn     = Client::Connection::get();
    req["@TCPInfo"] = fmt::format("TS; {}", conn.tcpinfo.log());
  }
}

} // namespace Bundle
