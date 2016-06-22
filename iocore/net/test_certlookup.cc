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
make_endpoint(const char *address)
{
  IpEndpoint ip;

  assert(ats_ip_pton(address, &ip) == 0);
  return ip;
}

REGRESSION_TEST(SSLCertificateLookup)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  SSLCertLookup lookup;

  SSL_CTX *wild      = SSL_CTX_new(SSLv23_server_method());
  SSL_CTX *notwild   = SSL_CTX_new(SSLv23_server_method());
  SSL_CTX *b_notwild = SSL_CTX_new(SSLv23_server_method());
  SSL_CTX *foo       = SSL_CTX_new(SSLv23_server_method());
  SSL_CTX *all_com   = SSL_CTX_new(SSLv23_server_method());
  SSLCertContext wild_cc(wild);
  SSLCertContext notwild_cc(notwild);
  SSLCertContext b_notwild_cc(b_notwild);
  SSLCertContext foo_cc(foo);
  SSLCertContext all_com_cc(all_com);

  box = REGRESSION_TEST_PASSED;

  assert(wild != NULL);
  assert(notwild != NULL);
  assert(b_notwild != NULL);
  assert(foo != NULL);
  assert(all_com != NULL);

  box.check(lookup.insert("www.foo.com", foo_cc) >= 0, "insert host context");
  // Insert the same SSL_CTX instance under another name too
  // Should be ok, but also need to make sure that the cleanup does not
  // double free the SSL_CTX
  box.check(lookup.insert("www.foo2.com", foo_cc) >= 0, "insert host context");
  box.check(lookup.insert("*.wild.com", wild_cc) >= 0, "insert wildcard context");
  box.check(lookup.insert("*.notwild.com", notwild_cc) >= 0, "insert wildcard context");
  box.check(lookup.insert("*.b.notwild.com", b_notwild_cc) >= 0, "insert wildcard context");
  box.check(lookup.insert("*.com", all_com_cc) >= 0, "insert wildcard context");

  // To test name collisions, we need to shuffle the SSL_CTX's so that we try to
  // index the same name with a different SSL_CTX.
  box.check(lookup.insert("*.com", wild_cc) < 0, "insert host duplicate");
  box.check(lookup.insert("*.wild.com", foo_cc) < 0, "insert wildcard duplicate");
  box.check(lookup.insert("*.notwild.com", b_notwild_cc) < 0, "insert wildcard context duplicate");
  box.check(lookup.insert("*.b.notwild.com", notwild_cc) < 0, "insert wildcard context duplicate");
  box.check(lookup.insert("www.foo.com", all_com_cc) < 0, "insert wildcard context duplicate");

  // Basic wildcard cases.
  box.check(lookup.find("a.wild.com")->ctx == wild, "wildcard lookup for a.wild.com");
  box.check(lookup.find("b.wild.com")->ctx == wild, "wildcard lookup for b.wild.com");
  box.check(lookup.insert("www.foo.com", all_com_cc) < 0, "insert wildcard context duplicate");

  // Verify that wildcard does longest match.
  box.check(lookup.find("a.notwild.com")->ctx == notwild, "wildcard lookup for a.notwild.com");
  box.check(lookup.find("notwild.com")->ctx == all_com, "wildcard lookup for notwild.com");
  box.check(lookup.find("c.b.notwild.com")->ctx == b_notwild, "wildcard lookup for c.b.notwild.com");

  // Basic hostname cases.
  box.check(lookup.find("www.foo.com")->ctx == foo, "host lookup for www.foo.com");
  box.check(lookup.find("www.bar.com")->ctx == all_com, "host lookup for www.bar.com");
  box.check(lookup.find("www.bar.net") == NULL, "host lookup for www.bar.net");
}

REGRESSION_TEST(SSLAddressLookup)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  SSLCertLookup lookup;

  struct {
    SSL_CTX *ip6;
    SSL_CTX *ip6p;
    SSL_CTX *ip4;
    SSL_CTX *ip4p;
  } context;

  struct {
    IpEndpoint ip6;
    IpEndpoint ip6p;
    IpEndpoint ip4;
    IpEndpoint ip4p;
  } endpoint;

  context.ip6  = SSL_CTX_new(SSLv23_server_method());
  context.ip6p = SSL_CTX_new(SSLv23_server_method());
  context.ip4  = SSL_CTX_new(SSLv23_server_method());
  context.ip4p = SSL_CTX_new(SSLv23_server_method());
  SSLCertContext ip6_cc(context.ip6);
  SSLCertContext ip6p_cc(context.ip6p);
  SSLCertContext ip4_cc(context.ip4);
  SSLCertContext ip4p_cc(context.ip4p);

  endpoint.ip6  = make_endpoint("fe80::7ed1:c3ff:fe90:2582");
  endpoint.ip6p = make_endpoint("[fe80::7ed1:c3ff:fe90:2582]:80");
  endpoint.ip4  = make_endpoint("10.0.0.5");
  endpoint.ip4p = make_endpoint("10.0.0.5:80");

  box = REGRESSION_TEST_PASSED;

  // For each combination of address with port and address without port, make sure that we find the
  // the most specific match (ie. find the context with the port if it is available) ...

  box.check(lookup.insert(endpoint.ip6, ip6_cc) >= 0, "insert IPv6 address");
  box.check(lookup.find(endpoint.ip6)->ctx == context.ip6, "IPv6 exact match lookup");
  box.check(lookup.find(endpoint.ip6p)->ctx == context.ip6, "IPv6 exact match lookup w/ port");

  box.check(lookup.insert(endpoint.ip6p, ip6p_cc) >= 0, "insert IPv6 address w/ port");
  box.check(lookup.find(endpoint.ip6)->ctx == context.ip6, "IPv6 longest match lookup");
  box.check(lookup.find(endpoint.ip6p)->ctx == context.ip6p, "IPv6 longest match lookup w/ port");

  box.check(lookup.insert(endpoint.ip4, ip4_cc) >= 0, "insert IPv4 address");
  box.check(lookup.find(endpoint.ip4)->ctx == context.ip4, "IPv4 exact match lookup");
  box.check(lookup.find(endpoint.ip4p)->ctx == context.ip4, "IPv4 exact match lookup w/ port");

  box.check(lookup.insert(endpoint.ip4p, ip4p_cc) >= 0, "insert IPv4 address w/ port");
  box.check(lookup.find(endpoint.ip4)->ctx == context.ip4, "IPv4 longest match lookup");
  box.check(lookup.find(endpoint.ip4p)->ctx == context.ip4p, "IPv4 longest match lookup w/ port");
}

static unsigned
load_hostnames_csv(const char *fname, SSLCertLookup &lookup)
{
  std::fstream infile(fname, std::ios_base::in);
  unsigned count = 0;

  // SSLCertLookup correctly handles indexing the same certificate
  // with multiple names, an it's way faster to load a lot of names
  // if we don't need a new context every time.

  SSL_CTX *ctx = SSL_CTX_new(SSLv23_server_method());
  SSLCertContext ctx_cc(ctx);

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
      lookup.insert(host.c_str(), ctx_cc);
    } else {
      // No comma? Assume the whole line is the hostname
      lookup.insert(line.c_str(), ctx_cc);
    }

    ++count;
  }

  return count;
}

// This stub version of SSLReleaseContext saves us from having to drag in a lot
// of binary dependencies. We don't have session tickets in this test environment
// so it's safe to do this; just a bit ugly.
void
SSLReleaseContext(SSL_CTX *ctx)
{
  SSL_CTX_free(ctx);
}

int
main(int argc, const char **argv)
{
  BaseLogFile *blf = new BaseLogFile("stdout");
  diags            = new Diags(NULL, NULL, blf);
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
