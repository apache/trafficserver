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

/*
 * These are misc unit tests for header rewrite
 */

#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <ostream>
#include <sys/stat.h>

#include "parser.h"

#if TS_USE_HRW_MAXMINDDB
#include <maxminddb.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

namespace header_rewrite_ns
{
const char PLUGIN_NAME[]     = "TEST_header_rewrite";
const char PLUGIN_NAME_DBG[] = "TEST_dbg_header_rewrite";
} // namespace header_rewrite_ns

void
TSError(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

class ParserTest : public Parser
{
public:
  ParserTest(const std::string &line) : res(true)
  {
    Parser::parse_line(line);
    std::cout << "Finished parser test: " << line << std::endl;
  }

  std::vector<std::string>
  getTokens() const
  {
    return _tokens;
  }

  template <typename T, typename U>
  void
  do_parser_check(T x, U y, int line = 0)
  {
    if (x != y) {
      std::cerr << "CHECK FAILED on line " << line << ": " << x << " != " << y << std::endl;
      res = false;
    }
  }

  bool res;
};

class SimpleTokenizerTest : public HRWSimpleTokenizer
{
public:
  SimpleTokenizerTest(const std::string &line) : HRWSimpleTokenizer(line), res(true)
  {
    std::cout << "Finished tokenizer test: " << line << std::endl;
  }

  template <typename T, typename U>
  void
  do_parser_check(T x, U y, int line = 0)
  {
    if (x != y) {
      std::cerr << "CHECK FAILED on line " << line << ": |" << x << "| != |" << y << "|" << std::endl;
      res = false;
    }
  }

  bool res;
};

#define CHECK_EQ(x, y)                     \
  do {                                     \
    p.do_parser_check((x), (y), __LINE__); \
  } while (false)

#define END_TEST(s) \
  do {              \
    if (!p.res) {   \
      ++errors;     \
    }               \
  } while (false)

int
test_parsing()
{
  int errors = 0;

  {
    ParserTest p("cond      %{READ_REQUEST_HDR_HOOK}");

    CHECK_EQ(p.getTokens().size(), 2U);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{READ_REQUEST_HDR_HOOK}");

    END_TEST();
  }

  {
    ParserTest p("cond %{CLIENT-HEADER:Host}    =a");

    CHECK_EQ(p.getTokens().size(), 4UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:Host}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "a");

    END_TEST();
  }

  {
    ParserTest p(" # COMMENT!");

    CHECK_EQ(p.getTokens().size(), 0UL);
    CHECK_EQ(p.empty(), true);

    END_TEST();
  }

  {
    ParserTest p("# COMMENT");

    CHECK_EQ(p.getTokens().size(), 0UL);
    CHECK_EQ(p.empty(), true);

    END_TEST();
  }

  {
    ParserTest p("cond %{Client-HEADER:Foo} =b");

    CHECK_EQ(p.getTokens().size(), 4UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{Client-HEADER:Foo}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "b");

    END_TEST();
  }

  {
    ParserTest p("cond %{Client-HEADER:Blah}       =        x");

    CHECK_EQ(p.getTokens().size(), 4UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{Client-HEADER:Blah}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "x");

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} =  "shouldnt_   exist    _anyway"          [AND])");

    CHECK_EQ(p.getTokens().size(), 5UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "shouldnt_   exist    _anyway");
    CHECK_EQ(p.getTokens()[4], "[AND]");

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} =  "shouldnt_   =    _anyway"          [AND])");

    CHECK_EQ(p.getTokens().size(), 5UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "shouldnt_   =    _anyway");
    CHECK_EQ(p.getTokens()[4], "[AND]");

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} ="="          [AND])");

    CHECK_EQ(p.getTokens().size(), 5UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "=");
    CHECK_EQ(p.getTokens()[4], "[AND]");

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} =""          [AND])");

    CHECK_EQ(p.getTokens().size(), 5UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "");
    CHECK_EQ(p.getTokens()[4], "[AND]");

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-URL:PATH} /\/foo\/bar/ [OR])");

    CHECK_EQ(p.getTokens().size(), 4UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-URL:PATH}");
    CHECK_EQ(p.getTokens()[2], R"(/\/foo\/bar/)");
    CHECK_EQ(p.getTokens()[3], "[OR]");

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{INBOUND:REMOTE-ADDR} {192.168.201.0/24,10.0.0.0/8})");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{INBOUND:REMOTE-ADDR}");
    CHECK_EQ(p.getTokens()[2], "{192.168.201.0/24,10.0.0.0/8}");

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{INBOUND:REMOTE-ADDR} { 192.168.201.0/24,10.0.0.0/8 })");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{INBOUND:REMOTE-ADDR}");
    CHECK_EQ(p.getTokens()[2], "{ 192.168.201.0/24,10.0.0.0/8 }");

    END_TEST();
  }

  {
    ParserTest p("add-header X-HeaderRewriteApplied true");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "X-HeaderRewriteApplied");
    CHECK_EQ(p.getTokens()[2], "true");

    END_TEST();
  }

  /* backslash-escape */
  {
    ParserTest p(R"(add-header foo \ \=\<\>\"\#\\)");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "foo");
    CHECK_EQ(p.getTokens()[2], R"( =<>"#\)");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header foo \<bar\>)");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "foo");
    CHECK_EQ(p.getTokens()[2], "<bar>");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header foo \bar\)");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "foo");
    CHECK_EQ(p.getTokens()[2], "bar");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header foo "bar")");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "foo");
    CHECK_EQ(p.getTokens()[2], "bar");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header foo "\"bar\"")");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "foo");
    CHECK_EQ(p.getTokens()[2], R"("bar")");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header foo "\"\\\"bar\\\"\"")");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "foo");
    CHECK_EQ(p.getTokens()[2], R"("\"bar\"")");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header Public-Key-Pins "max-age=3000; pin-sha256=\"d6qzRu9zOECb90Uez27xWltNsj0e1Md7GkYYkVoZWmM=\"")");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "Public-Key-Pins");
    CHECK_EQ(p.getTokens()[2], R"(max-age=3000; pin-sha256="d6qzRu9zOECb90Uez27xWltNsj0e1Md7GkYYkVoZWmM=")");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header Public-Key-Pins max-age\=3000;\ pin-sha256\=\"d6qzRu9zOECb90Uez27xWltNsj0e1Md7GkYYkVoZWmM\=\")");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "Public-Key-Pins");
    CHECK_EQ(p.getTokens()[2], R"(max-age=3000; pin-sha256="d6qzRu9zOECb90Uez27xWltNsj0e1Md7GkYYkVoZWmM=")");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header X-Url "http://trafficserver.apache.org/")");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "X-Url");
    CHECK_EQ(p.getTokens()[2], "http://trafficserver.apache.org/");

    END_TEST();
  }

  {
    ParserTest p(R"(add-header X-Url http://trafficserver.apache.org/)");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "X-Url");
    CHECK_EQ(p.getTokens()[2], "http://trafficserver.apache.org/");

    END_TEST();
  }

  {
    ParserTest p(R"(set-header Alt-Svc "quic=\":443\"; v=\"35\"" [L])");

    CHECK_EQ(p.getTokens().size(), 4UL);
    CHECK_EQ(p.getTokens()[0], "set-header");
    CHECK_EQ(p.getTokens()[1], "Alt-Svc");
    CHECK_EQ(p.getTokens()[2], R"(quic=":443"; v="35")");
    CHECK_EQ(p.get_value(), R"(quic=":443"; v="35")");

    END_TEST();
  }

  /*
   * test some failure scenarios
   */

  { /* unterminated quote */
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} =" [AND])");

    CHECK_EQ(p.getTokens().size(), 0UL);

    END_TEST();
  }

  { /* quote in a token */
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} =a"b [AND])");

    CHECK_EQ(p.getTokens().size(), 0UL);

    END_TEST();
  }

  return errors;
}

int
test_processing()
{
  int errors = 0;
  /*
   * These tests are designed to verify that the processing of the parsed input is correct.
   */
  {
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} ="="          [AND])");

    CHECK_EQ(p.getTokens().size(), 5UL);
    CHECK_EQ(p.get_op(), "CLIENT-HEADER:non_existent_header");
    CHECK_EQ(p.get_arg(), "==");
    CHECK_EQ(p.is_cond(), true);

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-HEADER:non_existent_header} =  "shouldnt_   =    _anyway"          [AND])");

    CHECK_EQ(p.getTokens().size(), 5UL);
    CHECK_EQ(p.get_op(), "CLIENT-HEADER:non_existent_header");
    CHECK_EQ(p.get_arg(), "=shouldnt_   =    _anyway");
    CHECK_EQ(p.is_cond(), true);

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-URL:PATH} /\.html|\.txt/)");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.get_op(), "CLIENT-URL:PATH");
    CHECK_EQ(p.get_arg(), R"(/\.html|\.txt/)");
    CHECK_EQ(p.is_cond(), true);

    END_TEST();
  }

  {
    ParserTest p(R"(cond %{CLIENT-URL:PATH} /\/foo\/bar/)");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.get_op(), "CLIENT-URL:PATH");
    CHECK_EQ(p.get_arg(), R"(/\/foo\/bar/)");
    CHECK_EQ(p.is_cond(), true);

    END_TEST();
  }

  {
    ParserTest p("add-header X-HeaderRewriteApplied true");

    CHECK_EQ(p.getTokens().size(), 3UL);
    CHECK_EQ(p.get_op(), "add-header");
    CHECK_EQ(p.get_arg(), "X-HeaderRewriteApplied");
    CHECK_EQ(p.get_value(), "true");
    CHECK_EQ(p.is_cond(), false);

    END_TEST();
  }

  return errors;
}

int
test_tokenizer()
{
  int errors = 0;

  {
    SimpleTokenizerTest p("a simple test");
    CHECK_EQ(p.get_tokens().size(), 1UL);
    CHECK_EQ(p.get_tokens()[0], "a simple test");

    END_TEST();
  }

  {
    SimpleTokenizerTest p(R"(quic=":443"; v="35")");
    CHECK_EQ(p.get_tokens().size(), 1UL);
    CHECK_EQ(p.get_tokens()[0], R"(quic=":443"; v="35")");

    END_TEST();
  }

  {
    SimpleTokenizerTest p(R"(let's party like it's  %{NOW:YEAR})");
    CHECK_EQ(p.get_tokens().size(), 2UL);
    CHECK_EQ(p.get_tokens()[0], "let's party like it's  ");
    CHECK_EQ(p.get_tokens()[1], "%{NOW:YEAR}");

    END_TEST();
  }
  {
    SimpleTokenizerTest p("A racoon's favorite tag is %{METHOD} in %{NOW:YEAR}!");
    CHECK_EQ(p.get_tokens().size(), 5UL);
    CHECK_EQ(p.get_tokens()[0], "A racoon's favorite tag is ");
    CHECK_EQ(p.get_tokens()[1], "%{METHOD}");
    CHECK_EQ(p.get_tokens()[2], " in ");
    CHECK_EQ(p.get_tokens()[3], "%{NOW:YEAR}");
    CHECK_EQ(p.get_tokens()[4], "!");

    END_TEST();
  }

  {
    SimpleTokenizerTest p(R"(Hello from %{IP:SERVER}:%{INBOUND:LOCAL-PORT})");
    CHECK_EQ(p.get_tokens().size(), 4UL);
    CHECK_EQ(p.get_tokens()[0], "Hello from ");
    CHECK_EQ(p.get_tokens()[1], "%{IP:SERVER}");
    CHECK_EQ(p.get_tokens()[2], ":");
    CHECK_EQ(p.get_tokens()[3], "%{INBOUND:LOCAL-PORT}");

    END_TEST();
  }

  return errors;
}

#if TS_USE_HRW_MAXMINDDB
static bool
file_exists(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

static const char *
find_country_mmdb()
{
  static const char *paths[] = {
    "/usr/share/GeoIP/GeoLite2-Country.mmdb", "/usr/local/share/GeoIP/GeoLite2-Country.mmdb",
    "/var/lib/GeoIP/GeoLite2-Country.mmdb",   "/opt/geoip/GeoLite2-Country.mmdb",
    "/usr/share/GeoIP/GeoIP2-Country.mmdb",   "/usr/share/GeoIP/dbip-country-lite.mmdb",
  };
  for (auto *p : paths) {
    if (file_exists(p)) {
      return p;
    }
  }
  if (const char *env = getenv("MMDB_COUNTRY_PATH")) {
    if (file_exists(env)) {
      return env;
    }
  }
  return nullptr;
}

int
test_maxmind_geo()
{
  const char *db_path = find_country_mmdb();
  if (db_path == nullptr) {
    std::cout << "SKIP: No MaxMind country mmdb found (set MMDB_COUNTRY_PATH to override)" << std::endl;
    return 0;
  }

  std::cout << "Testing MaxMind geo lookups with: " << db_path << std::endl;

  int    errors = 0;
  MMDB_s mmdb;
  int    status = MMDB_open(db_path, MMDB_MODE_MMAP, &mmdb);
  if (MMDB_SUCCESS != status) {
    std::cerr << "Cannot open " << db_path << ": " << MMDB_strerror(status) << std::endl;
    return 1;
  }

  // MMDB_lookup_string() returns two independent error codes:
  //   gai_error  - getaddrinfo() failure (string-to-IP conversion)
  //   mmdb_error - MMDB lookup failure (database query)
  // Check both to avoid masking failures.
  int                  gai_error, mmdb_error;
  MMDB_lookup_result_s result = MMDB_lookup_string(&mmdb, "8.8.8.8", &gai_error, &mmdb_error);

  if (gai_error != 0) {
    std::cerr << "getaddrinfo failed for 8.8.8.8: " << gai_strerror(gai_error) << std::endl;
    MMDB_close(&mmdb);
    return 1;
  }
  if (MMDB_SUCCESS != mmdb_error || !result.found_entry) {
    std::cerr << "Cannot look up 8.8.8.8 in " << db_path << ": " << MMDB_strerror(mmdb_error) << std::endl;
    MMDB_close(&mmdb);
    return 1;
  }

  MMDB_entry_data_s entry_data;

  // Verify "country" -> "iso_code" path (used by GEO_QUAL_COUNTRY)
  status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
  if (MMDB_SUCCESS != status || !entry_data.has_data || entry_data.type != MMDB_DATA_TYPE_UTF8_STRING) {
    std::cerr << "FAIL: country/iso_code lookup failed for 8.8.8.8" << std::endl;
    ++errors;
  } else {
    std::string iso(entry_data.utf8_string, entry_data.data_size);
    if (iso != "US") {
      std::cerr << "FAIL: expected country iso_code 'US' for 8.8.8.8, got '" << iso << "'" << std::endl;
      ++errors;
    } else {
      std::cout << "  PASS: country/iso_code = " << iso << std::endl;
    }
  }

  // Verify "country" -> "names" -> "en" path exists (not used by header_rewrite but validates structure)
  status = MMDB_get_value(&result.entry, &entry_data, "country", "names", "en", NULL);
  if (MMDB_SUCCESS != status || !entry_data.has_data || entry_data.type != MMDB_DATA_TYPE_UTF8_STRING) {
    std::cerr << "FAIL: country/names/en lookup failed for 8.8.8.8" << std::endl;
    ++errors;
  } else {
    std::string name(entry_data.utf8_string, entry_data.data_size);
    std::cout << "  PASS: country/names/en = " << name << std::endl;
  }

  // Verify loopback returns no entry
  result = MMDB_lookup_string(&mmdb, "127.0.0.1", &gai_error, &mmdb_error);
  if (gai_error != 0) {
    std::cerr << "FAIL: getaddrinfo failed for 127.0.0.1: " << gai_strerror(gai_error) << std::endl;
    ++errors;
  } else if (MMDB_SUCCESS == mmdb_error && result.found_entry) {
    std::cerr << "FAIL: expected no entry for 127.0.0.1" << std::endl;
    ++errors;
  } else {
    std::cout << "  PASS: 127.0.0.1 correctly returns no entry" << std::endl;
  }

  MMDB_close(&mmdb);

  if (errors == 0) {
    std::cout << "MaxMind geo tests passed" << std::endl;
  }
  return errors;
}
#endif

int
main()
{
  if (test_parsing() || test_processing() || test_tokenizer()) {
    return 1;
  }

#if TS_USE_HRW_MAXMINDDB
  if (test_maxmind_geo()) {
    return 1;
  }
#endif

  return 0;
}
