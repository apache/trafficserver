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
#include <parser.h>

const char PLUGIN_NAME[]     = "TEST_header_rewrite";
const char PLUGIN_NAME_DBG[] = "TEST_dbg_header_rewrite";

extern "C" void
TSError(const char *fmt, ...)
{
  char buf[2048];
  int bytes = 0;
  va_list args;
  va_start(args, fmt);
  if ((bytes = vsnprintf(buf, sizeof(buf), fmt, args)) > 0) {
    fprintf(stderr, "TSError: %s: %.*s\n", PLUGIN_NAME, bytes, buf);
  }
  va_end(args);
}

extern "C" void
TSDebug(const char *tag, const char *fmt, ...)
{
  char buf[2048];
  int bytes = 0;
  va_list args;
  va_start(args, fmt);
  if ((bytes = vsnprintf(buf, sizeof(buf), fmt, args)) > 0) {
    fprintf(stdout, "TSDebug: %s: %.*s\n", PLUGIN_NAME, bytes, buf);
  }
  va_end(args);
}

#define CHECK_EQ(x, y)                   \
  do {                                   \
    if ((x) != (y)) {                    \
      fprintf(stderr, "CHECK FAILED\n"); \
      return 1;                          \
    }                                    \
  } while (false);

class ParserTest : public Parser
{
public:
  ParserTest(std::string line) : Parser(line) {}
  std::vector<std::string>
  getTokens()
  {
    return _tokens;
  }
};

int
test_parsing()
{
  {
    ParserTest p("cond      %{READ_REQUEST_HDR_HOOK}");
    CHECK_EQ(p.getTokens().size(), 2);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{READ_REQUEST_HDR_HOOK}");
  }

  {
    ParserTest p("cond %{CLIENT-HEADER:Host}    =a");
    CHECK_EQ(p.getTokens().size(), 4);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:Host}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "a");
  }

  {
    ParserTest p(" # COMMENT!");
    CHECK_EQ(p.getTokens().size(), 0);
    CHECK_EQ(p.empty(), true);
  }

  {
    ParserTest p("# COMMENT");
    CHECK_EQ(p.getTokens().size(), 0);
    CHECK_EQ(p.empty(), true);
  }

  {
    ParserTest p("cond %{Client-HEADER:Foo} =b");
    CHECK_EQ(p.getTokens().size(), 4);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{Client-HEADER:Foo}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "b");
  }

  {
    ParserTest p("cond %{Client-HEADER:Blah}       =        x");
    CHECK_EQ(p.getTokens().size(), 4);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{Client-HEADER:Blah}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "x");
  }

  {
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =  \"shouldnt_   exist    _anyway\"          [AND]");
    CHECK_EQ(p.getTokens().size(), 5);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "shouldnt_   exist    _anyway");
    CHECK_EQ(p.getTokens()[4], "[AND]");
  }

  {
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =  \"shouldnt_   =    _anyway\"          [AND]");
    CHECK_EQ(p.getTokens().size(), 5);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "shouldnt_   =    _anyway");
    CHECK_EQ(p.getTokens()[4], "[AND]");
  }

  {
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =\"=\"          [AND]");
    CHECK_EQ(p.getTokens().size(), 5);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "=");
    CHECK_EQ(p.getTokens()[4], "[AND]");
  }

  {
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =\"\"          [AND]");
    CHECK_EQ(p.getTokens().size(), 5);
    CHECK_EQ(p.getTokens()[0], "cond");
    CHECK_EQ(p.getTokens()[1], "%{CLIENT-HEADER:non_existent_header}");
    CHECK_EQ(p.getTokens()[2], "=");
    CHECK_EQ(p.getTokens()[3], "");
    CHECK_EQ(p.getTokens()[4], "[AND]");
  }

  {
    ParserTest p("add-header X-HeaderRewriteApplied true");
    CHECK_EQ(p.getTokens().size(), 3);
    CHECK_EQ(p.getTokens()[0], "add-header");
    CHECK_EQ(p.getTokens()[1], "X-HeaderRewriteApplied");
    CHECK_EQ(p.getTokens()[2], "true");
  }

  /*
   * test some failure scenarios
   */

  { /* unterminated quote */
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =\" [AND]");
    CHECK_EQ(p.getTokens().size(), 0);
  }

  { /* quote in a token */
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =a\"b [AND]");
    CHECK_EQ(p.getTokens().size(), 0);
  }

  return 0;
}

int
test_processing()
{
  /*
   * These tests are designed to verify that the processing of the parsed input is correct.
   */
  {
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =\"=\"          [AND]");
    CHECK_EQ(p.getTokens().size(), 5);
    CHECK_EQ(p.get_op(), "CLIENT-HEADER:non_existent_header");
    CHECK_EQ(p.get_arg(), "==");
    CHECK_EQ(p.is_cond(), true);
  }

  {
    ParserTest p("cond %{CLIENT-HEADER:non_existent_header} =  \"shouldnt_   =    _anyway\"          [AND]");
    CHECK_EQ(p.getTokens().size(), 5);
    CHECK_EQ(p.get_op(), "CLIENT-HEADER:non_existent_header");
    CHECK_EQ(p.get_arg(), "=shouldnt_   =    _anyway");
    CHECK_EQ(p.is_cond(), true);
  }

  {
    ParserTest p("add-header X-HeaderRewriteApplied true");
    CHECK_EQ(p.getTokens().size(), 3);
    CHECK_EQ(p.get_op(), "add-header");
    CHECK_EQ(p.get_arg(), "X-HeaderRewriteApplied");
    CHECK_EQ(p.get_value(), "true")
    CHECK_EQ(p.is_cond(), false);
  }

  return 0;
}

int
tests()
{
  if (test_parsing() || test_processing()) {
    return 1;
  }

  return 0;
}

int
main()
{
  return tests();
}
