/** @file

  A brief file description

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

#include <string>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "EsiProcessor.h"
#include "TestHttpDataFetcher.h"
#include "Utils.h"
#include "HandlerMap.h"

using std::string;
using namespace EsiLib;

static const int FETCHER_STATIC_DATA_SIZE = 30;

TEST_CASE("esi processor test")
{
  Utils::HeaderValueList allowlistCookies;
  Variables esi_vars("vars", allowlistCookies);
  HandlerManager handler_mgr("handler_mgr");
  TestHttpDataFetcher data_fetcher;
  EsiProcessor esi_proc("processor", "parser", "expression", data_fetcher, esi_vars, handler_mgr);

  SECTION("call sequence")
  {
    string input_data("");
    const char *output_data;
    int output_data_len = 0;

    SECTION("Negative test - process()ing without completeParse()ing")
    {
      REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
      REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
      esi_proc.stop();
    }

    SECTION("Implicit call to start() #1")
    {
      REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
      REQUIRE(esi_proc.completeParse() == true);
      REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
      REQUIRE(output_data_len == 0);
      esi_proc.stop();
    }

    SECTION("Implicit call to start() #2")
    {
      REQUIRE(esi_proc.completeParse() == true);
      REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
      REQUIRE(output_data_len == 0);
      esi_proc.stop();
    }

    SECTION("Negative test: calling process() before start()")
    {
      REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    }

    SECTION("Negative test: calling addParseData() after process()")
    {
      REQUIRE(esi_proc.completeParse() == true);
      REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
      REQUIRE(output_data_len == 0);
      REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
      esi_proc.stop();
    }

    SECTION("Negative test: calling completeParse() after process()")
    {
      REQUIRE(esi_proc.completeParse() == true);
      REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
      REQUIRE(output_data_len == 0);
      REQUIRE(esi_proc.completeParse() == false);
      esi_proc.stop();
    }

    SECTION("Good call sequence with no data")
    {
      REQUIRE(esi_proc.start() == true);
      REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
      REQUIRE(esi_proc.completeParse() == true);
      REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
      REQUIRE(output_data_len == 0);
    }
  }

  SECTION("Negative test: invalid ESI tag")
  {
    string input_data("foo<esi:blah/>bar");

    const char *output_data;
    int output_data_len = 10;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    REQUIRE(output_data_len == 10); // should remain unchanged
  }

  SECTION("comment tag 1")
  {
    string input_data("foo<esi:comment text=\"bleh\"/>bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 6);
    REQUIRE(strncmp(output_data, "foobar", output_data_len) == 0);
  }

  SECTION("comment tag 2")
  {
    string input_data("<esi:comment text=\"bleh\"/>bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 3);
    REQUIRE(strncmp(output_data, "bar", output_data_len) == 0);
  }

  SECTION("comment tag 3")
  {
    string input_data("foo<esi:comment text=\"bleh\"/>");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 3);
    REQUIRE(strncmp(output_data, "foo", output_data_len) == 0);
  }

  SECTION("multi-line comment tag")
  {
    string input_data("foo\n<esi:comment text=\"\nbleh\"/>\nbar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 8);
    REQUIRE(strncmp(output_data, "foo\n\nbar", output_data_len) == 0);
  }

  SECTION("multi-line remove tag")
  {
    string input_data("foo\n<esi:remove><img src=\"http://www.example.com\"></esi:remove>\nbar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 8);
    REQUIRE(strncmp(output_data, "foo\n\nbar", output_data_len) == 0);
  }

  SECTION("remove and comment tags")
  {
    string input_data("foo\n<esi:remove><img src=\"http://www.example.com\"></esi:remove>\nbar"
                      "foo2\n<esi:comment text=\"bleh\"/>\nbar2");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 18);
    REQUIRE(strncmp(output_data, "foo\n\nbarfoo2\n\nbar2", output_data_len) == 0);
  }

  SECTION("multiple remove and comment tags")
  {
    string input_data("foo1<esi:remove><img src=\"http://www.example.com\"></esi:remove>bar1\n"
                      "foo1<esi:comment text=\"bleh\"/>bar1\n"
                      "foo2<esi:remove><img src=\"http://www.example.com\"></esi:remove>bar2\n"
                      "foo2<esi:comment text=\"bleh\"/>bar2\n"
                      "foo3<esi:remove><img src=\"http://www.example.com\"></esi:remove>bar3\n"
                      "foo3<esi:comment text=\"bleh\"/>bar3\n");
    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 54);
    REQUIRE(strncmp(output_data, "foo1bar1\nfoo1bar1\nfoo2bar2\nfoo2bar2\nfoo3bar3\nfoo3bar3\n", output_data_len) == 0);
  }

  SECTION("include tag")
  {
    string input_data("foo <esi:include src=url1/> bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 8 + 4 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data, "foo >>>>> Content for URL [url1] <<<<< bar", output_data_len) == 0);
  }

  SECTION("include tag with no URL")
  {
    string input_data("foo <esi:include src=/> bar");
    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
  }

  SECTION("include tag with no src")
  {
    string input_data("foo <esi:include /> bar");
    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
  }

  SECTION("multiple include tags")
  {
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len ==
            11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data,
                    "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                    "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                    ">>>>> Content for URL [blah bleh] <<<<<",
                    output_data_len) == 0);
  }

  SECTION("remove, comment and include tags")
  {
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>"
                      "<esi:comment text=\"bleh\"/>"
                      "<esi:remove> <a href=> </esi:remove>");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len ==
            11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data,
                    "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                    "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                    ">>>>> Content for URL [blah bleh] <<<<<",
                    output_data_len) == 0);
  }

  SECTION("multiple addParseData calls")
  {
    char line1[] = "foo1 <esi:include src=url1/> bar1\n";
    char line2[] = "foo2 <esi:include src=url2/> bar2\n";
    char line3[] = "<esi:include src=\"blah bleh\"/>";
    char line4[] = "<esi:comment text=\"bleh\"/>";
    char line5[] = "<esi:remove> <a href=>";
    char line6[] = "</esi:remove>";

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(line1, sizeof(line1) - 1) == true);
    REQUIRE(esi_proc.addParseData(line2, sizeof(line2) - 1) == true);
    REQUIRE(esi_proc.addParseData(line3, sizeof(line3) - 1) == true);
    REQUIRE(esi_proc.addParseData(line4, sizeof(line4) - 1) == true);
    REQUIRE(esi_proc.addParseData(line5, sizeof(line5) - 1) == true);
    REQUIRE(esi_proc.addParseData(line6, 13) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len ==
            11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data,
                    "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                    "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                    ">>>>> Content for URL [blah bleh] <<<<<",
                    output_data_len) == 0);
  }

  SECTION("one-shot parse")
  {
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>"
                      "<esi:comment text=\"bleh\"/>"
                      "<esi:remove> <a href=> </esi:remove>");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.completeParse(input_data.data(), input_data.size()) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len ==
            11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data,
                    "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                    "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                    ">>>>> Content for URL [blah bleh] <<<<<",
                    output_data_len) == 0);
  }

  SECTION("final chunk call")
  {
    char line1[] = "foo1 <esi:include src=url1/> bar1\n";
    char line2[] = "foo2 <esi:include src=url2/> bar2\n";
    char line3[] = "<esi:include src=\"blah bleh\"/>";
    char line4[] = "<esi:comment text=\"bleh\"/>";
    char line5[] = "<esi:remove> <a href=>";
    char line6[] = "</esi:remove>";

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(line1, sizeof(line1) - 1) == true);
    REQUIRE(esi_proc.addParseData(line2, sizeof(line2) - 1) == true);
    REQUIRE(esi_proc.addParseData(line3, sizeof(line3) - 1) == true);
    REQUIRE(esi_proc.addParseData(line4, sizeof(line4) - 1) == true);
    REQUIRE(esi_proc.addParseData(line5, sizeof(line5) - 1) == true);
    REQUIRE(esi_proc.completeParse(line6, sizeof(line6) - 1) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len ==
            11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data,
                    "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                    "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                    ">>>>> Content for URL [blah bleh] <<<<<",
                    output_data_len) == 0);
  }

  SECTION("no length arg")
  {
    string input_data("foo <esi:include src=url1/> bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 8 + 4 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data, "foo >>>>> Content for URL [url1] <<<<< bar", output_data_len) == 0);
  }

  SECTION("std::string arg")
  {
    string input_data("foo <esi:include src=url1/> bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 8 + 4 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data, "foo >>>>> Content for URL [url1] <<<<< bar", output_data_len) == 0);
  }

  SECTION("one-shot parse, std::string arg")
  {
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>"
                      "<esi:comment text=bleh />"
                      "<esi:remove> <a href=> </esi:remove>");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.completeParse(input_data) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len ==
            11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    REQUIRE(strncmp(output_data,
                    "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                    "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                    ">>>>> Content for URL [blah bleh] <<<<<",
                    output_data_len) == 0);
  }

  SECTION("invalidly expanding url")
  {
    string input_data("foo <esi:include src=$(HTTP_HOST) /> bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    REQUIRE(output_data_len == 0);
  }

  SECTION("vars node with simple expression")
  {
    string input_data("foo <esi:vars>HTTP_HOST</esi:vars> bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 17);
    REQUIRE(strncmp(output_data, "foo HTTP_HOST bar", output_data_len) == 0);
  }

  SECTION("vars node expression with valid variable")
  {
    string input_data("foo <esi:vars>$(HTTP_HOST)</esi:vars> bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 8);
    REQUIRE(strncmp(output_data, "foo  bar", output_data_len) == 0);
  }

  SECTION("vars node with invalid expression")
  {
    string input_data("foo <esi:vars>$(HTTP_HOST</esi:vars> bar");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 8);
    REQUIRE(strncmp(output_data, "foo  bar", output_data_len) == 0);
  }

  SECTION("choose-when")
  {
    string input_data("<esi:choose>"
                      "<esi:when test=foo>"
                      "<esi:include src=foo />"
                      "</esi:when>"
                      "<esi:when test=bar>"
                      "<esi:include src=bar />"
                      "</esi:when>"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "</esi:choose>");
    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.completeParse(input_data) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == FETCHER_STATIC_DATA_SIZE + 3);
    REQUIRE(strncmp(output_data, ">>>>> Content for URL [foo] <<<<<", output_data_len) == 0);
  }

  SECTION("choose-when")
  {
    string input_data("<esi:choose>"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "</esi:choose>");
    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.completeParse(input_data) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == FETCHER_STATIC_DATA_SIZE + 9);
    REQUIRE(strncmp(output_data, ">>>>> Content for URL [otherwise] <<<<<", output_data_len) == 0);
  }

  SECTION("try block 1")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == FETCHER_STATIC_DATA_SIZE + 7);
    REQUIRE(strncmp(output_data, ">>>>> Content for URL [attempt] <<<<<", output_data_len) == 0);
  }

  SECTION("try block 2")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    data_fetcher.setReturnData(true);
    REQUIRE(output_data_len == 0);
  }

  SECTION("try block 3")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == FETCHER_STATIC_DATA_SIZE + 6);
    REQUIRE(strncmp(output_data, ">>>>> Content for URL [except] <<<<<", output_data_len) == 0);
  }

  SECTION("try block 4")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "except"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    data_fetcher.setReturnData(true);
    REQUIRE(output_data_len == 6);
    REQUIRE(strncmp(output_data, "except", output_data_len) == 0);
  }

  SECTION("try block 5")
  {
    string input_data("<esi:include src=pre />"
                      "foo"
                      "<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>"
                      "bar");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == FETCHER_STATIC_DATA_SIZE + 3 + 3 + FETCHER_STATIC_DATA_SIZE + 6 + 3);
    REQUIRE(strncmp(output_data, ">>>>> Content for URL [pre] <<<<<foo>>>>> Content for URL [except] <<<<<bar", output_data_len) ==
            0);
  }

  SECTION("html comment node")
  {
    string input_data("<esi:include src=helloworld />"
                      "foo"
                      "<!--esi <esi:vars>blah</esi:vars>-->"
                      "bar");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == FETCHER_STATIC_DATA_SIZE + 10 + 3 + 4 + 3);
    REQUIRE(strncmp(output_data, ">>>>> Content for URL [helloworld] <<<<<fooblahbar", output_data_len) == 0);
  }

  SECTION("invalid html comment node")
  {
    string input_data("<esi:include src=helloworld />"
                      "foo"
                      "<!--esi <esi:vars>blah</esi:var>-->"
                      "bar");

    REQUIRE(esi_proc.completeParse(input_data) == false);
  }

  SECTION("choose-when")
  {
    string input_data("<esi:choose>\n\t"
                      "<esi:when test=foo>"
                      "\t<esi:include src=foo />"
                      "</esi:when>\n"
                      "<esi:when test=bar>"
                      "<esi:include src=bar />"
                      "</esi:when>\n"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>\n"
                      "</esi:choose>");
    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.completeParse(input_data) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 1 + FETCHER_STATIC_DATA_SIZE + 3);
    REQUIRE(strncmp(output_data, "\t>>>>> Content for URL [foo] <<<<<", output_data_len) == 0);
  }

  SECTION("special-include 1")
  {
    string input_data("<esi:special-include handler=stub/>");
    gHandlerMap.clear();
    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(gHandlerMap.size() == 1);
    REQUIRE(gHandlerMap.begin()->first == "stub");
    StubIncludeHandler *handler = gHandlerMap["stub"];
    REQUIRE(handler->parseCompleteCalled == false);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(handler->parseCompleteCalled == true);

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    REQUIRE(strncmp(output_data, "Special data for include id 1", output_data_len) == 0);
  }

  SECTION("special-include 2")
  {
    string input_data("foo <esi:special-include handler=stub/> <esi:special-include handler=stub/> bar");
    gHandlerMap.clear();
    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(gHandlerMap.size() == 1);
    REQUIRE(gHandlerMap.begin()->first == "stub");
    StubIncludeHandler *handler = gHandlerMap["stub"];
    REQUIRE(handler->parseCompleteCalled == false);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(handler->parseCompleteCalled == true);

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == (4 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 1 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 4));
    REQUIRE(strncmp(output_data, "foo Special data for include id 1 Special data for include id 2 bar", output_data_len) == 0);
  }

  SECTION("special-include 3")
  {
    string input_data("foo <esi:special-include handler=ads/> <esi:special-include handler=udb/> bar");
    gHandlerMap.clear();
    REQUIRE(esi_proc.addParseData(input_data) == true);
    REQUIRE(gHandlerMap.size() == 2);
    REQUIRE(gHandlerMap.find(string("ads")) != gHandlerMap.end());
    REQUIRE(gHandlerMap.find(string("udb")) != gHandlerMap.end());
    StubIncludeHandler *ads_handler = gHandlerMap["ads"];
    StubIncludeHandler *udb_handler = gHandlerMap["udb"];
    REQUIRE(ads_handler->parseCompleteCalled == false);
    REQUIRE(udb_handler->parseCompleteCalled == false);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(ads_handler->parseCompleteCalled == true);
    REQUIRE(udb_handler->parseCompleteCalled == true);

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == (4 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 1 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 4));
    REQUIRE(strncmp(output_data, "foo Special data for include id 1 Special data for include id 1 bar", output_data_len) == 0);
  }

  SECTION("special-include negative")
  {
    string input_data("<esi:special-include handler=stub/>");
    gHandlerMap.clear();
    StubIncludeHandler::includeResult = false;
    REQUIRE(esi_proc.addParseData(input_data) == false);
    REQUIRE(gHandlerMap.size() == 1); // it'll still be created
    REQUIRE(gHandlerMap.begin()->first == "stub");
    StubIncludeHandler::includeResult = true;
  }

  SECTION("try block with special include 1")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    REQUIRE(strncmp(output_data, "Special data for include id 1", output_data_len) == 0);
  }

  SECTION("try block with special include 2")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    data_fetcher.setReturnData(true);
    REQUIRE(output_data_len == 0);
  }

  SECTION("try block with special include 3")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    REQUIRE(strncmp(output_data, "Special data for include id 2", output_data_len) == 0);
  }

  SECTION("special include try block")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "except"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len = 0;
    REQUIRE(esi_proc.completeParse(input_data) == true);

    // this is to make the StubHandler return failure
    data_fetcher.setReturnData(false);

    // this is to decrement HttpDataFetcher's pending request count - argument content doesn't matter
    data_fetcher.getContent("blah", output_data, output_data_len);

    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    data_fetcher.setReturnData(true);
    REQUIRE(output_data_len == 6);
    REQUIRE(strncmp(output_data, "except", output_data_len) == 0);
  }

  SECTION("comment tag")
  {
    string input_data("<esi:comment text=\"bleh\"/>");

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 0);
  }

  SECTION("using packed node list 1")
  {
    EsiParser parser("parser");
    DocNodeList node_list;
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");
    REQUIRE(parser.parse(node_list, input_data) == true);

    string packedNodeList = node_list.pack();

    REQUIRE(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_SUCCESS);
    const char *output_data;
    int output_data_len = 0;
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    REQUIRE(strncmp(output_data, "Special data for include id 2", output_data_len) == 0);

    esi_proc.stop();
    node_list.clear();
    input_data = "<esi:choose>\n\t"
                 "<esi:when test=foo>"
                 "\t<esi:include src=foo />"
                 "</esi:when>\n"
                 "<esi:when test=bar>"
                 "<esi:include src=bar />"
                 "</esi:when>\n"
                 "<esi:otherwise>"
                 "<esi:include src=otherwise />"
                 "</esi:otherwise>\n"
                 "</esi:choose>";
    REQUIRE(parser.parse(node_list, input_data) == true);
    packedNodeList = node_list.pack();
    REQUIRE(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_SUCCESS);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 1 + FETCHER_STATIC_DATA_SIZE + 3);
    REQUIRE(strncmp(output_data, "\t>>>>> Content for URL [foo] <<<<<", output_data_len) == 0);
  }

  SECTION("using packed node list 2")
  {
    string input_data("<esi:comment text=\"bleh\"/>");

    EsiParser parser("parser");
    DocNodeList node_list;
    string input_data2("<esi:try>"
                       "<esi:attempt>"
                       "<esi:special-include handler=stub />"
                       "</esi:attempt>"
                       "<esi:except>"
                       "<esi:special-include handler=stub />"
                       "</esi:except>"
                       "</esi:try>");
    REQUIRE(parser.parse(node_list, input_data2) == true);

    string packedNodeList = node_list.pack();

    const char *output_data;
    int output_data_len = 0;

    REQUIRE(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    REQUIRE(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_IN_PROGRESS);
    REQUIRE(esi_proc.completeParse() == true);
    REQUIRE(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_IN_PROGRESS);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == 0);
  }

  SECTION("special include with footer")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");

    const char *output_data;
    int output_data_len             = 0;
    StubIncludeHandler::FOOTER      = "<!--footer-->";
    StubIncludeHandler::FOOTER_SIZE = strlen(StubIncludeHandler::FOOTER);
    REQUIRE(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    REQUIRE(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    REQUIRE(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1 + StubIncludeHandler::FOOTER_SIZE);
    REQUIRE(strncmp(output_data, "Special data for include id 2", output_data_len - StubIncludeHandler::FOOTER_SIZE) == 0);
    REQUIRE(strncmp(output_data + StubIncludeHandler::DATA_PREFIX_SIZE + 1, StubIncludeHandler::FOOTER,
                    StubIncludeHandler::FOOTER_SIZE) == 0);
    StubIncludeHandler::FOOTER      = nullptr;
    StubIncludeHandler::FOOTER_SIZE = 0;
  }

  SECTION("using packed node list 3")
  {
    EsiParser parser("parser");
    DocNodeList node_list;
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");
    REQUIRE(parser.parse(node_list, input_data) == true);

    string packedNodeList = node_list.pack();

    REQUIRE(esi_proc.usePackedNodeList(nullptr, packedNodeList.size()) == EsiProcessor::UNPACK_FAILURE);
  }

  SECTION("using packed node list 4")
  {
    EsiParser parser("parser");
    DocNodeList node_list;
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");
    REQUIRE(parser.parse(node_list, input_data) == true);

    string packedNodeList = node_list.pack();

    REQUIRE(esi_proc.usePackedNodeList(packedNodeList.data(), 0) == EsiProcessor::UNPACK_FAILURE);
  }
}
