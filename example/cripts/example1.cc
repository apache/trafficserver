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
static Matcher::Range::IP CRIPT_ALLOW({"192.168.201.0/24", "10.0.0.0/8"});

// This is called only when the plugin is initialized
do_init()
{
  // TSDebug("Cript", "Hello, example1 plugin is being initialized");
}

do_create_instance()
{
  instance.metrics[0] = Metrics::Counter::create("cript.example1.c0");
  instance.metrics[1] = Metrics::Counter::create("cript.example1.c1");
  instance.metrics[2] = Metrics::Counter::create("cript.example1.c2");
  instance.metrics[3] = Metrics::Counter::create("cript.example1.c3");
  instance.metrics[4] = Metrics::Counter::create("cript.example1.c4");
  instance.metrics[5] = Metrics::Counter::create("cript.example1.c5");
  instance.metrics[6] = Metrics::Counter::create("cript.example1.c6");
  instance.metrics[7] = Metrics::Counter::create("cript.example1.c7");
  instance.metrics[8] = Metrics::Counter::create("cript.example1.c8"); // This one should resize() the storage

  Bundle::Common::Activate().dscp(10);
  Bundle::Caching::Activate().cache_control("max-age=259200");
}

do_txn_close()
{
  borrow conn = Client::Connection::Get();

  conn.pacing = Cript::Pacing::Off;
  CDebug("Cool, TXN close also works");
}

do_cache_lookup()
{
  borrow url2 = Cache::URL::Get();

  CDebug("Cache URL: {}", url2.Getter());
  CDebug("Cache Host: {}", url2.host);
}

do_send_request()
{
  borrow req = Server::Request::Get();

  req["X-Leif"] = "Meh";
}

do_read_response()
{
  borrow resp = Server::Response::Get();

  resp["X-DBJ"] = "Vrooom!";
}

do_send_response()
{
  borrow resp = Client::Response::Get();
  borrow conn = Client::Connection::Get();
  string msg  = "Eliminate TSCPP";

  resp["Server"]         = "";        // Deletes the Server header
  resp["X-AMC"]          = msg;       // New header
  resp["Cache-Control"]  = "Private"; // Deletes old CC values, and sets a new one
  resp["X-UUID"]         = UUID::Unique::Get();
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
  static const File::Path p1("/tmp/foo");
  static const File::Path p2("/tmp/secret.txt");

  if (File::Status(p1).type() == File::Type::regular) {
    resp["X-Foo-Exists"] = "yes";
  } else {
    resp["X-Foo-Exists"] = "no";
  }

  string secret = File::Line::Reader(p2);
  CDebug("Read secret = {}", secret);

  if (resp.status == 200) {
    resp.status = 222;
  }

  CDebug("Txn count: {}", conn.Count());
}

do_remap()
{
  auto   now  = Time::Local::now();
  borrow req  = Client::Request::Get();
  borrow conn = Client::Connection::Get();
  auto   ip   = conn.IP();

  if (CRIPT_ALLOW.contains(ip)) {
    CDebug("Client IP allowed: {}", ip.string(24, 64));
  }

  CDebug("Epoch time is {} (or via .epoch(), {}", now, now.epoch());
  CDebug("Year is {}", now.year());
  CDebug("Month is {}", now.month());
  CDebug("Day is {}", now.day());
  CDebug("Hour is {}", now.hour());
  CDebug("Day number is {}", now.yearday());

  CDebug("from_url = {}", instance.from_url.c_str());
  CDebug("to_url = {}", instance.to_url.c_str());

  // Turn off the cache for testing
  // proxy.config.http.cache.http.set(1);
  // control.cache.nostore.set(true);

  CDebug("Int config cache.http = {}", proxy.config.http.cache.http.Get());
  CDebug("Float config cache.heuristic_lm_factor = {}", proxy.config.http.cache.heuristic_lm_factor.Get());
  CDebug("String config http.response_server_str = {}", proxy.config.http.response_server_str.GetSV(context));
  CDebug("X-Miles = {}", req["X-Miles"]);
  CDebug("random(1000) = {}", Cript::Random(1000));

  borrow url      = Client::URL::Get();
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
  static Matcher::PCRE pcre("^/([^/]+)/(.*)$");

  auto res = pcre.match("/foo/bench/bar"); // Can also call contains(), same thing

  if (res) {
    CDebug("Ovector count is {}", res.count());
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
  auto        hp          = Crypto::Base64::Decode(base64_test);
  auto        hp2         = Crypto::Base64::Encode(hp);

  CDebug("HP quote: {}", hp);
  if (base64_test != hp2) {
    CDebug("Base64 failed: {}", hp2);
  } else {
    CDebug("Base64 encode reproduced the decoded HP string");
  }

  // Some Crypto::Escape (URL escaping) tests
  static auto escape_test = "Hello_World_!@%23$%25%5E&*()_%2B%3C%3E?%2C.%2F";
  auto        uri         = Crypto::Escape::Decode(escape_test);
  auto        uri2        = Crypto::Escape::Encode(uri);

  CDebug("Unescaped URI: {}", uri);
  if (escape_test != uri2) {
    CDebug("URL Escape failed: {}", uri2);
  } else {
    CDebug("Url Escape encode reproduced the decoded HP string");
  }

  // Testing Crypto SHA and encryption
  auto hex = format("{}", Crypto::SHA256::Encode("Hello World"));

  CDebug("SHA256 = {}", hex);

  // Testing iterators
  for (auto hdr : req) {
    CDebug("Header: {} = {}", hdr, req[hdr]);
    if (hdr.starts_with("AWS-")) {
      req[hdr].clear();
    }
  }

  // Testing some simple metrics
  static auto m1 = Metrics::Gauge("cript.example1.m1");
  static auto m2 = Metrics::Counter("cript.example1.m2");

  m1.increment(100);
  m1.decrement(10);
  m2.increment();

  instance.metrics[0]->increment();
  instance.metrics[8]->increment();
}

#include <cripts/Epilogue.hpp>
