/** @file

  SSL Context management

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

#include "P_SSLCertLookup.h"
#include "ts/TestBox.h"
#include <fstream>

static IpEndpoint
make_endpoint(const char * address)
{
  IpEndpoint ip;

  assert(ats_ip_pton(address, &ip) == 0);
  return ip;
}

REGRESSION_TEST(SSLCertificateLookup)(RegressionTest* t, int /* atype ATS_UNUSED */, int * pstatus)
{
  TestBox       box(t, pstatus);
  SSLCertLookup lookup;

  SSL_CTX * wild = SSL_CTX_new(SSLv23_server_method());
  SSL_CTX * notwild = SSL_CTX_new(SSLv23_server_method());
  SSL_CTX * b_notwild = SSL_CTX_new(SSLv23_server_method());
  SSL_CTX * foo = SSL_CTX_new(SSLv23_server_method());

  box = REGRESSION_TEST_PASSED;

  assert(wild != NULL);
  assert(notwild != NULL);
  assert(b_notwild != NULL);
  assert(foo != NULL);

  box.check(lookup.insert(foo, "www.foo.com"), "insert host context");
  box.check(lookup.insert(wild, "*.wild.com"), "insert wildcard context");
  box.check(lookup.insert(notwild, "*.notwild.com"), "insert wildcard context");
  box.check(lookup.insert(b_notwild, "*.b.notwild.com"), "insert wildcard context");

  // XXX inserting the same certificate multiple times ought to always fail, but it doesn't
  // when we store a hash value.
  lookup.insert(foo, "www.foo.com");
  lookup.insert(wild, "*.wild.com");
  lookup.insert(notwild, "*.notwild.com");
  lookup.insert(b_notwild, "*.b.notwild.com");

  // Basic wildcard cases.
  box.check(lookup.findInfoInHash("a.wild.com") == wild, "wildcard lookup for a.wild.com");
  box.check(lookup.findInfoInHash("b.wild.com") == wild, "wildcard lookup for b.wild.com");
  box.check(lookup.findInfoInHash("wild.com") == wild, "wildcard lookup for wild.com");

  // Verify that wildcard does longest match.
  box.check(lookup.findInfoInHash("a.notwild.com") == notwild, "wildcard lookup for a.notwild.com");
  box.check(lookup.findInfoInHash("notwild.com") == notwild, "wildcard lookup for notwild.com");
  box.check(lookup.findInfoInHash("c.b.notwild.com") == b_notwild, "wildcard lookup for c.b.notwild.com");

  // Basic hostname cases.
  box.check(lookup.findInfoInHash("www.foo.com") == foo, "host lookup for www.foo.com");
  box.check(lookup.findInfoInHash("www.bar.com") == NULL, "host lookup for www.bar.com");
}

REGRESSION_TEST(SSLAddressLookup)(RegressionTest* t, int /* atype ATS_UNUSED */, int * pstatus)
{
  TestBox       box(t, pstatus);
  SSLCertLookup lookup;

  struct {
    SSL_CTX * ip6;
    SSL_CTX * ip6p;
    SSL_CTX * ip4;
    SSL_CTX * ip4p;
  } context;

  struct {
     IpEndpoint ip6;
     IpEndpoint ip6p;
     IpEndpoint ip4;
     IpEndpoint ip4p;
  } endpoint;

  context.ip6 = SSL_CTX_new(SSLv23_server_method());
  context.ip6p = SSL_CTX_new(SSLv23_server_method());
  context.ip4 = SSL_CTX_new(SSLv23_server_method());
  context.ip4p = SSL_CTX_new(SSLv23_server_method());

  endpoint.ip6 = make_endpoint("fe80::7ed1:c3ff:fe90:2582");
  endpoint.ip6p = make_endpoint("[fe80::7ed1:c3ff:fe90:2582]:80");
  endpoint.ip4 = make_endpoint("10.0.0.5");
  endpoint.ip4p = make_endpoint("10.0.0.5:80");

  box = REGRESSION_TEST_PASSED;

  // For each combination of address with port and address without port, make sure that we find the
  // the most specific match (ie. find the context with the port if it is available) ...

  box.check(lookup.insert(context.ip6, endpoint.ip6), "insert IPv6 address");
  box.check(lookup.findInfoInHash(endpoint.ip6) == context.ip6, "IPv6 exact match lookup");
  box.check(lookup.findInfoInHash(endpoint.ip6p) == context.ip6, "IPv6 exact match lookup w/ port");

  box.check(lookup.insert(context.ip6p, endpoint.ip6p), "insert IPv6 address w/ port");
  box.check(lookup.findInfoInHash(endpoint.ip6) == context.ip6, "IPv6 longest match lookup");
  box.check(lookup.findInfoInHash(endpoint.ip6p) == context.ip6p, "IPv6 longest match lookup w/ port");

  box.check(lookup.insert(context.ip4, endpoint.ip4), "insert IPv4 address");
  box.check(lookup.findInfoInHash(endpoint.ip4) == context.ip4, "IPv4 exact match lookup");
  box.check(lookup.findInfoInHash(endpoint.ip4p) == context.ip4, "IPv4 exact match lookup w/ port");

  box.check(lookup.insert(context.ip4p, endpoint.ip4p), "insert IPv4 address w/ port");
  box.check(lookup.findInfoInHash(endpoint.ip4) == context.ip4, "IPv4 longest match lookup");
  box.check(lookup.findInfoInHash(endpoint.ip4p) == context.ip4p, "IPv4 longest match lookup w/ port");
}

static unsigned
load_hostnames_csv(const char * fname, SSLCertLookup& lookup)
{
  std::fstream infile(fname, std::ios_base::in);
  unsigned count = 0;

  // SSLCertLookup correctly handles indexing the same certificate
  // with multiple names, an it's way faster to load a lot of names
  // if we don't need a new context every time.

  SSL_CTX * ctx = SSL_CTX_new(SSLv23_server_method());

  // The input should have 2 comma-separated fields; this is the format that you get when
  // you download the top 1M sites from alexa.
  //
  // For example:
  //    1,google.com
  //    2,facebook.com
  //    3,youtube.com
  //    4,yahoo.com
  //    5,baidu.com

  while (!infile.eof()) {
    std::string line;
    std::string::size_type pos;

    infile >> line;
    if (line.empty()) {
      break;
    }

    pos = line.find_first_of(',');
    if (pos != std::string::npos) {
      std::string host(line.substr(pos + 1));
      lookup.insert(ctx, host.c_str());
    } else {
      // No comma? Assume the whole line is the hostname
      lookup.insert(ctx, line.c_str());
    }

    ++count;
  }

  return count;
}

// This stub version of SSLReleaseContext saves us from having to drag in a lot
// of binary dependencies. We don't have session tickets in this test environment
// so it's safe to do this; just a bit ugly.
void
SSLReleaseContext(SSL_CTX * ctx)
{
   SSL_CTX_free(ctx);
}

int main(int argc, const char ** argv)
{
  res_track_memory = 1;

  SSL_library_init();
  ink_freelists_snap_baseline();

  if (argc > 1) {
    SSLCertLookup lookup;
    unsigned count = 0;

    for (int i = 1; i < argc; ++i) {
      count += load_hostnames_csv(argv[i], lookup);
    }

    printf("loaded %u host names\n", count);

  } else {
    // Standard regression tests.
    RegressionTest::run();
  }

  ink_freelists_dump(stdout);

  // On Darwin, fail the tests if we have any memory leaks.
#if defined(darwin)
  if (system("xcrun leaks test_certlookup") != 0) {
    RegressionTest::final_status = REGRESSION_TEST_FAILED;
  }
#endif

  return RegressionTest::final_status == REGRESSION_TEST_PASSED ? 0 : 1;
}
