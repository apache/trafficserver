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

#define CRIPT_CONVENIENCE_APIS 1

// The primary include file, this has to always be included
#include <cripts/Preamble.hpp>

#include <cripts/Bundles/Common.hpp>
#include <cripts/Bundles/Caching.hpp>

// Globals for this Cript
ACL(CRIPT_ALLOW, {"192.168.201.0/24", "10.0.0.0/8"});

// This is called only when the plugin is initialized
do_init()
{
  // TSDebug("Cript", "Hello, example1 plugin is being initialized");
}

do_create_instance()
{
  CreateCounter(0, "cript.example1.c0");
  CreateCounter(1, "cript.example1.c1");
  CreateCounter(2, "cript.example1.c2");
  CreateCounter(3, "cript.example1.c3");
  CreateCounter(4, "cript.example1.c4");
  CreateCounter(5, "cript.example1.c5");
  CreateCounter(6, "cript.example1.c6");
  CreateCounter(7, "cript.example1.c7");
  CreateCounter(8, "cript.example1.c8"); // This one should resize the storage

  cripts::Bundle::Common::Activate().dscp(10);
  cripts::Bundle::Caching::Activate().cache_control("max-age=259200");
}

do_txn_close()
{
  client.connection.pacing = cripts::Pacing::Off;
  CDebug("Cool, TXN close also works");
}

do_cache_lookup()
{
  borrow url2 = cripts::Cache::URL::Get();

  CDebug("Cache URL: {}", url2.String());
  CDebug("Cache Host: {}", url2.host);
}

do_send_request()
{
  server.request["X-Leif"] = "Meh";
}

do_read_response()
{
  server.response["X-DBJ"] = "Vrooom!";
}

do_send_response()
{
  string msg = "Eliminate TSCPP";

  client.response["Server"]         = "";        // Deletes the Server header
  client.response["X-AMC"]          = msg;       // New header
  client.response["Cache-Control"]  = "Private"; // Deletes old CC values, and sets a new one
  client.response["X-UUID"]         = UniqueUUID();
  client.response["X-tcpinfo"]      = client.connection.tcpinfo.Log();
  client.response["X-Cache-Status"] = client.response.cache;
  client.response["X-Integer"]      = 666;
  client.response["X-Data"]         = AsString(txn_data[2]);

  client.response["X-ASN"]         = client.connection.geo.ASN();
  client.response["X-ASN-Name"]    = client.connection.geo.ASNName();
  client.response["X-Country"]     = client.connection.geo.Country();
  client.response["X-ISO-Country"] = client.connection.geo.CountryCode();

  // Setup some connection parameters
  client.connection.congestion = "bbr";
  client.connection.dscp       = 8;
  client.connection.pacing     = 100000;
  client.connection.mark       = 17;

  // Some file operations (note that the paths aren't required here, can just be strings, but it's a good practice)
  FilePath(p1, "/tmp/foo");
  FilePath(p2, "/tmp/secret.txt");

  if (cripts::File::Status(p1).type() == cripts::File::Type::regular) {
    client.response["X-Foo-Exists"] = "yes";
  } else {
    client.response["X-Foo-Exists"] = "no";
  }

  string secret = cripts::File::Line::Reader(p2);
  CDebug("Read secret = {}", secret);

  if (client.response.status == 200) {
    client.response.status = 222;
  }

  CDebug("Txn count: {}", client.connection.Count());
}

do_remap()
{
  auto ip  = client.connection.IP();
  auto now = TimeNow();

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
  CDebug("X-Miles = {}", client.request["X-Miles"]);
  CDebug("random(1000) = {}", cripts::Random(1000));

  borrow url      = cripts::Client::URL::Get();
  auto   old_port = url.port;

  CDebug("Method is {}", client.request.method);
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
  Regex(pcre, "^/([^/]+)/(.*)$");

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
  for (auto hdr : client.request) {
    CDebug("Header: {} = {}", hdr, client.request[hdr]);
    if (hdr.starts_with("AWS-")) {
      client.request[hdr].clear();
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
