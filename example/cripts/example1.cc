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

// The primary include file, this has to always be included
#include <cripts/Preamble.hpp>

#include <cripts/Bundles/Common.hpp>
#include <cripts/Bundles/Caching.hpp>

// Globals for this Cript
static cripts::Matcher::Range::IP CRIPT_ALLOW({"192.168.201.0/24", "10.0.0.0/8"});

// This is called only when the plugin is initialized
do_init()
{
  // TSDebug("Cript", "Hello, example1 plugin is being initialized");
}

do_create_instance()
{
  instance.metrics[0] = cripts::Metrics::Counter::Create("cript.example1.c0");
  instance.metrics[1] = cripts::Metrics::Counter::Create("cript.example1.c1");
  instance.metrics[2] = cripts::Metrics::Counter::Create("cript.example1.c2");
  instance.metrics[3] = cripts::Metrics::Counter::Create("cript.example1.c3");
  instance.metrics[4] = cripts::Metrics::Counter::Create("cript.example1.c4");
  instance.metrics[5] = cripts::Metrics::Counter::Create("cript.example1.c5");
  instance.metrics[6] = cripts::Metrics::Counter::Create("cript.example1.c6");
  instance.metrics[7] = cripts::Metrics::Counter::Create("cript.example1.c7");
  instance.metrics[8] = cripts::Metrics::Counter::Create("cript.example1.c8"); // This one should resize() the storage

  cripts::Bundle::Common::Activate().dscp(10);
  cripts::Bundle::Caching::Activate().cache_control("max-age=259200");
}

do_txn_close()
{
  borrow conn = cripts::Client::Connection::Get();

  conn.pacing = cripts::Pacing::Off;
  CDebug("Cool, TXN close also works");
}

do_cache_lookup()
{
  borrow url2 = cripts::Cache::URL::Get();

  CDebug("Cache URL: {}", url2);
  CDebug("Cache Host: {}", url2.host);
}

do_send_request()
{
  borrow req = cripts::Server::Request::Get();

  req["X-Leif"] = "Meh";
}

do_read_response()
{
  borrow resp = cripts::Server::Response::Get();

  resp["X-DBJ"] = "Vrooom!";
}

do_send_response()
{
  borrow resp = cripts::Client::Response::Get();
  borrow conn = cripts::Client::Connection::Get();
  string msg  = "Eliminate TSCPP";

  resp["Server"]         = "";        // Deletes the Server header
  resp["X-AMC"]          = msg;       // New header
  resp["Cache-Control"]  = "Private"; // Deletes old CC values, and sets a new one
  resp["X-UUID"]         = cripts::UUID::Unique::Get();
  resp["X-tcpinfo"]      = conn.tcpinfo.Log();
  resp["X-Cache-Status"] = resp.cache;
  resp["X-Integer"]      = 666;
  resp["X-Data"]         = AsString(txn_data[2]);

  resp["X-ASN"]         = conn.geo.ASN();
  resp["X-ASN-Name"]    = conn.geo.ASNName();
  resp["X-Country"]     = conn.geo.Country();
  resp["X-ISO-Country"] = conn.geo.CountryCode();

  // Setup some connection parameters
  conn.congestion = "bbr";
  conn.dscp       = 8;
  conn.pacing     = 100000;
  conn.mark       = 17;

  // Some file operations (note that the paths aren't required here, can just be strings, but it's a good practice)
  static const cripts::File::Path p1("/tmp/foo");
  static const cripts::File::Path p2("/tmp/secret.txt");

  if (cripts::File::Status(p1).type() == cripts::File::Type::regular) {
    resp["X-Foo-Exists"] = "yes";
  } else {
    resp["X-Foo-Exists"] = "no";
  }

  string secret = cripts::File::Line::Reader(p2);
  CDebug("Read secret = {}", secret);

  if (resp.status == 200) {
    resp.status = 222;
  }

  CDebug("Txn count: {}", conn.Count());
}

do_remap()
{
  auto   now  = cripts::Time::Local::Now();
  borrow req  = cripts::Client::Request::Get();
  borrow conn = cripts::Client::Connection::Get();
  auto   ip   = conn.IP();

  if (CRIPT_ALLOW.contains(ip)) {
    CDebug("Client IP allowed: {}", ip.string(24, 64));
  }

  CDebug("Epoch time is {} (or via .epoch(), {}", now, now.Epoch());
  CDebug("Year is {}", now.Year());
  CDebug("Month is {}", now.Month());
  CDebug("Day is {}", now.Day());
  CDebug("Hour is {}", now.Hour());
  CDebug("Day number is {}", now.YearDay());

  CDebug("from_url = {}", instance.from_url.c_str());
  CDebug("to_url = {}", instance.to_url.c_str());

  // Turn off the cache for testing
  // proxy.config.http.cache.http.set(1);
  // control.cache.nostore.set(true);

  CDebug("Int config cache.http = {}", proxy.config.http.cache.http.Get());
  CDebug("Float config cache.heuristic_lm_factor = {}", proxy.config.http.cache.heuristic_lm_factor.Get());
  CDebug("String config http.response_server_str = {}", proxy.config.http.response_server_str.GetSV(context));
  CDebug("X-Miles = {}", req["X-Miles"]);
  CDebug("random(1000) = {}", cripts::Random(1000));

  borrow url      = cripts::Client::URL::Get();
  auto   old_port = url.port;

  CDebug("Method is {}", req.method);
  CDebug("Scheme is {}", url.scheme);
  CDebug("Host is {}", url.host);
  CDebug("Port is {}", url.port);
  CDebug("Path is {}", url.path);
  CDebug("Path[1] is {}", url.path[1]);
  CDebug("Query is {}", url.query);

  auto testing_trim = url.path.trim();

  CDebug("Trimmed path is {}", testing_trim);

  if (url.query["foo"] > 100) {
    CDebug("Query[foo] is > 100");
  }

  if (url.path == "some/url" || url.path[0] == "other") {
    CDebug("The path comparison triggered");
  }

  url.host = "foobar.com";
  url.port = "81";
  url.port = old_port;

  // TXN data slots
  txn_data[0] = true;
  txn_data[1] = 17;
  txn_data[2] = "DBJ";

  // Regular expressions
  static cripts::Matcher::PCRE pcre("^/([^/]+)/(.*)$");

  auto res = pcre.Match("/foo/bench/bar"); // Can also call contains(), same thing

  if (res) {
    CDebug("Ovector count is {}", res.Count());
    CDebug("First capture is {}", res[1]);
    CDebug("Second capture is {}", res[2]);
  } else {
    CDebug("Regular expression did not match, that is not expected!");
  }

  // ATS versions
  CDebug("ATS version = {}", version);
  CDebug("ATS Major Version = {}", version.major);

  // Some Crypto::Base64 tests
  static auto base64_test = "VGltZSB3aWxsIG5vdCBzbG93IGRvd24gd2hlbiBzb21ldGhpbmcgdW5wbGVhc2FudCBsaWVzIGFoZWFkLg==";
  auto        hp          = cripts::Crypto::Base64::Decode(base64_test);
  auto        hp2         = cripts::Crypto::Base64::Encode(hp);

  CDebug("HP quote: {}", hp);
  if (base64_test != hp2) {
    CDebug("Base64 failed: {}", hp2);
  } else {
    CDebug("Base64 encode reproduced the decoded HP string");
  }

  // Some Crypto::Escape (URL escaping) tests
  static auto escape_test = "Hello_World_!@%23$%25%5E&*()_%2B%3C%3E?%2C.%2F";
  auto        uri         = cripts::Crypto::Escape::Decode(escape_test);
  auto        uri2        = cripts::Crypto::Escape::Encode(uri);

  CDebug("Unescaped URI: {}", uri);
  if (escape_test != uri2) {
    CDebug("URL Escape failed: {}", uri2);
  } else {
    CDebug("Url Escape encode reproduced the decoded HP string");
  }

  // Testing Crypto SHA and encryption
  auto hex = format("{}", cripts::Crypto::SHA256::Encode("Hello World"));

  CDebug("SHA256 = {}", hex);

  // Testing iterators
  for (auto hdr : req) {
    CDebug("Header: {} = {}", hdr, req[hdr]);
    if (hdr.starts_with("AWS-")) {
      req[hdr].clear();
    }
  }

  // Testing some simple metrics
  static auto m1 = cripts::Metrics::Gauge("cript.example1.m1");
  static auto m2 = cripts::Metrics::Counter("cript.example1.m2");

  m1.Increment(100);
  m1.Decrement(10);
  m2.Increment();

  instance.metrics[0]->Increment();
  instance.metrics[8]->Increment();
}

#include <cripts/Epilogue.hpp>
