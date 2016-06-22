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

#include <iostream>
#include <assert.h>
#include <string>

#include "EsiProcessor.h"
#include "TestHttpDataFetcher.h"
#include "print_funcs.h"
#include "Utils.h"
#include "HandlerMap.h"

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using namespace EsiLib;

pthread_key_t threadKey;

static const int FETCHER_STATIC_DATA_SIZE = 30;

int
main()
{
  Variables esi_vars("vars", &Debug, &Error);
  HandlerManager handler_mgr("handler_mgr", &Debug, &Error);

  pthread_key_create(&threadKey, NULL);
  Utils::init(&Debug, &Error);

  {
    cout << endl << "===================== Test 1) call sequence" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("");
    const char *output_data;
    int output_data_len = 0;

    cout << "Negative test - process()ing without completeParse()ing..." << endl;
    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    esi_proc.stop();

    cout << "Implicit call to start() #1..." << endl;
    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 0);
    esi_proc.stop();

    cout << "Implicit call to start() #2..." << endl;
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 0);
    esi_proc.stop();

    cout << "Negative test: calling process() before start()" << endl;
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);

    cout << "Negative test: calling addParseData() after process()" << endl;
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 0);
    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
    esi_proc.stop();

    cout << "Negative test: calling completeParse() after process()" << endl;
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 0);
    assert(esi_proc.completeParse() == false);
    esi_proc.stop();

    cout << "Good call sequence with no data" << endl;
    assert(esi_proc.start() == true);
    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 0);
  }

  {
    cout << endl << "===================== Test 2) Negative test: invalid ESI tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo<esi:blah/>bar");

    const char *output_data;
    int output_data_len = 10;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    assert(output_data_len == 10); // should remain unchanged
  }

  {
    cout << endl << "===================== Test 3) comment tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo<esi:comment text=\"bleh\"/>bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 6);
    assert(strncmp(output_data, "foobar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 4) comment tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:comment text=\"bleh\"/>bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 3);
    assert(strncmp(output_data, "bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 5) comment tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo<esi:comment text=\"bleh\"/>");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 3);
    assert(strncmp(output_data, "foo", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 6) multi-line comment tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo\n<esi:comment text=\"\nbleh\"/>\nbar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 8);
    assert(strncmp(output_data, "foo\n\nbar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 7) multi-line remove tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo\n<esi:remove><img src=\"http://www.example.com\"></esi:remove>\nbar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 8);
    assert(strncmp(output_data, "foo\n\nbar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 8) remove and comment tags" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo\n<esi:remove><img src=\"http://www.example.com\"></esi:remove>\nbar"
                      "foo2\n<esi:comment text=\"bleh\"/>\nbar2");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 18);
    assert(strncmp(output_data, "foo\n\nbarfoo2\n\nbar2", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 9) multiple remove and comment tags" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo1<esi:remove><img src=\"http://www.example.com\"></esi:remove>bar1\n"
                      "foo1<esi:comment text=\"bleh\"/>bar1\n"
                      "foo2<esi:remove><img src=\"http://www.example.com\"></esi:remove>bar2\n"
                      "foo2<esi:comment text=\"bleh\"/>bar2\n"
                      "foo3<esi:remove><img src=\"http://www.example.com\"></esi:remove>bar3\n"
                      "foo3<esi:comment text=\"bleh\"/>bar3\n");
    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 54);
    assert(strncmp(output_data, "foo1bar1\nfoo1bar1\nfoo2bar2\nfoo2bar2\nfoo3bar3\nfoo3bar3\n", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 10) include tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:include src=url1/> bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 8 + 4 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo >>>>> Content for URL [url1] <<<<< bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 11) include tag with no URL" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:include src=/> bar");
    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
  }

  {
    cout << endl << "===================== Test 12) include tag with no src" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:include /> bar");
    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == false);
  }

  {
    cout << endl << "===================== Test 13) multiple include tags" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                                "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                                ">>>>> Content for URL [blah bleh] <<<<<",
                   output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 14) remove, comment and include tags" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>"
                      "<esi:comment text=\"bleh\"/>"
                      "<esi:remove> <a href=> </esi:remove>");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                                "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                                ">>>>> Content for URL [blah bleh] <<<<<",
                   output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 15) multiple addParseData calls" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    char line1[] = "foo1 <esi:include src=url1/> bar1\n";
    char line2[] = "foo2 <esi:include src=url2/> bar2\n";
    char line3[] = "<esi:include src=\"blah bleh\"/>";
    char line4[] = "<esi:comment text=\"bleh\"/>";
    char line5[] = "<esi:remove> <a href=>";
    char line6[] = "</esi:remove>";

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(line1, sizeof(line1) - 1) == true);
    assert(esi_proc.addParseData(line2, sizeof(line2) - 1) == true);
    assert(esi_proc.addParseData(line3, sizeof(line3) - 1) == true);
    assert(esi_proc.addParseData(line4, sizeof(line4) - 1) == true);
    assert(esi_proc.addParseData(line5, sizeof(line5) - 1) == true);
    assert(esi_proc.addParseData(line6, 13) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                                "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                                ">>>>> Content for URL [blah bleh] <<<<<",
                   output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 16) one-shot parse" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>"
                      "<esi:comment text=\"bleh\"/>"
                      "<esi:remove> <a href=> </esi:remove>");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.completeParse(input_data.data(), input_data.size()) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                                "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                                ">>>>> Content for URL [blah bleh] <<<<<",
                   output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 17) final chunk call" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    char line1[] = "foo1 <esi:include src=url1/> bar1\n";
    char line2[] = "foo2 <esi:include src=url2/> bar2\n";
    char line3[] = "<esi:include src=\"blah bleh\"/>";
    char line4[] = "<esi:comment text=\"bleh\"/>";
    char line5[] = "<esi:remove> <a href=>";
    char line6[] = "</esi:remove>";

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(line1, sizeof(line1) - 1) == true);
    assert(esi_proc.addParseData(line2, sizeof(line2) - 1) == true);
    assert(esi_proc.addParseData(line3, sizeof(line3) - 1) == true);
    assert(esi_proc.addParseData(line4, sizeof(line4) - 1) == true);
    assert(esi_proc.addParseData(line5, sizeof(line5) - 1) == true);
    assert(esi_proc.completeParse(line6, sizeof(line6) - 1) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                                "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                                ">>>>> Content for URL [blah bleh] <<<<<",
                   output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 18) no length arg" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:include src=url1/> bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 8 + 4 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo >>>>> Content for URL [url1] <<<<< bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 19) std::string arg" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:include src=url1/> bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 8 + 4 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo >>>>> Content for URL [url1] <<<<< bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 20) one-shot parse, std::string arg" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo1 <esi:include src=url1/> bar1\n"
                      "foo2 <esi:include src=url2/> bar2\n"
                      "<esi:include src=\"blah bleh\"/>"
                      "<esi:comment text=bleh />"
                      "<esi:remove> <a href=> </esi:remove>");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.completeParse(input_data) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 11 + 4 + FETCHER_STATIC_DATA_SIZE + 11 + 4 + FETCHER_STATIC_DATA_SIZE + 9 + FETCHER_STATIC_DATA_SIZE);
    assert(strncmp(output_data, "foo1 >>>>> Content for URL [url1] <<<<< bar1\n"
                                "foo2 >>>>> Content for URL [url2] <<<<< bar2\n"
                                ">>>>> Content for URL [blah bleh] <<<<<",
                   output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 21) invalidly expanding url" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:include src=$(HTTP_HOST) /> bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    assert(output_data_len == 0);
  }

  {
    cout << endl << "===================== Test 22) vars node with simple expression" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:vars>HTTP_HOST</esi:vars> bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 17);
    assert(strncmp(output_data, "foo HTTP_HOST bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 23) vars node expression with valid variable" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:vars>$(HTTP_HOST)</esi:vars> bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 8);
    assert(strncmp(output_data, "foo  bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 24) vars node with invalid expression" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:vars>$(HTTP_HOST</esi:vars> bar");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 8);
    assert(strncmp(output_data, "foo  bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 25) choose-when" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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

    assert(esi_proc.completeParse(input_data) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == FETCHER_STATIC_DATA_SIZE + 3);
    assert(strncmp(output_data, ">>>>> Content for URL [foo] <<<<<", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 26) choose-when" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:choose>"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "</esi:choose>");
    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.completeParse(input_data) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == FETCHER_STATIC_DATA_SIZE + 9);
    assert(strncmp(output_data, ">>>>> Content for URL [otherwise] <<<<<", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 27) try block" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == FETCHER_STATIC_DATA_SIZE + 7);
    assert(strncmp(output_data, ">>>>> Content for URL [attempt] <<<<<", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 28) try block" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    data_fetcher.setReturnData(true);
    assert(output_data_len == 0);
  }

  {
    cout << endl << "===================== Test 29) try block" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == FETCHER_STATIC_DATA_SIZE + 6);
    assert(strncmp(output_data, ">>>>> Content for URL [except] <<<<<", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 30) try block" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    data_fetcher.setReturnData(true);
    assert(output_data_len == 6);
    assert(strncmp(output_data, "except", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 31) try block" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == FETCHER_STATIC_DATA_SIZE + 3 + 3 + FETCHER_STATIC_DATA_SIZE + 6 + 3);
    assert(strncmp(output_data, ">>>>> Content for URL [pre] <<<<<foo>>>>> Content for URL [except] <<<<<bar", output_data_len) ==
           0);
  }

  {
    cout << endl << "===================== Test 32) html comment node" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:include src=helloworld />"
                      "foo"
                      "<!--esi <esi:vars>blah</esi:vars>-->"
                      "bar");

    const char *output_data;
    int output_data_len = 0;
    assert(esi_proc.completeParse(input_data) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == FETCHER_STATIC_DATA_SIZE + 10 + 3 + 4 + 3);
    assert(strncmp(output_data, ">>>>> Content for URL [helloworld] <<<<<fooblahbar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 33) invalid html comment node" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:include src=helloworld />"
                      "foo"
                      "<!--esi <esi:vars>blah</esi:var>-->"
                      "bar");

    assert(esi_proc.completeParse(input_data) == false);
  }

  {
    cout << endl << "===================== Test 34) choose-when" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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

    assert(esi_proc.completeParse(input_data) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 1 + FETCHER_STATIC_DATA_SIZE + 3);
    assert(strncmp(output_data, "\t>>>>> Content for URL [foo] <<<<<", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 35) special-include 1" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:special-include handler=stub/>");
    gHandlerMap.clear();
    assert(esi_proc.addParseData(input_data) == true);
    assert(gHandlerMap.size() == 1);
    assert(gHandlerMap.begin()->first == "stub");
    StubIncludeHandler *handler = gHandlerMap["stub"];
    assert(handler->parseCompleteCalled == false);
    assert(esi_proc.completeParse() == true);
    assert(handler->parseCompleteCalled == true);

    const char *output_data;
    int output_data_len = 0;
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    assert(strncmp(output_data, "Special data for include id 1", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 36) special-include 2" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:special-include handler=stub/> <esi:special-include handler=stub/> bar");
    gHandlerMap.clear();
    assert(esi_proc.addParseData(input_data) == true);
    assert(gHandlerMap.size() == 1);
    assert(gHandlerMap.begin()->first == "stub");
    StubIncludeHandler *handler = gHandlerMap["stub"];
    assert(handler->parseCompleteCalled == false);
    assert(esi_proc.completeParse() == true);
    assert(handler->parseCompleteCalled == true);

    const char *output_data;
    int output_data_len = 0;
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == (4 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 1 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 4));
    assert(strncmp(output_data, "foo Special data for include id 1 Special data for include id 2 bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 37) special-include 3" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("foo <esi:special-include handler=ads/> <esi:special-include handler=udb/> bar");
    gHandlerMap.clear();
    assert(esi_proc.addParseData(input_data) == true);
    assert(gHandlerMap.size() == 2);
    assert(gHandlerMap.find(string("ads")) != gHandlerMap.end());
    assert(gHandlerMap.find(string("udb")) != gHandlerMap.end());
    StubIncludeHandler *ads_handler = gHandlerMap["ads"];
    StubIncludeHandler *udb_handler = gHandlerMap["udb"];
    assert(ads_handler->parseCompleteCalled == false);
    assert(udb_handler->parseCompleteCalled == false);
    assert(esi_proc.completeParse() == true);
    assert(ads_handler->parseCompleteCalled == true);
    assert(udb_handler->parseCompleteCalled == true);

    const char *output_data;
    int output_data_len = 0;
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == (4 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 1 + StubIncludeHandler::DATA_PREFIX_SIZE + 1 + 4));
    assert(strncmp(output_data, "foo Special data for include id 1 Special data for include id 1 bar", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 38) special-include negative" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:special-include handler=stub/>");
    gHandlerMap.clear();
    StubIncludeHandler::includeResult = false;
    assert(esi_proc.addParseData(input_data) == false);
    assert(gHandlerMap.size() == 1); // it'll still be created
    assert(gHandlerMap.begin()->first == "stub");
    StubIncludeHandler::includeResult = true;
  }

  {
    cout << endl << "===================== Test 39) try block with special include" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    assert(strncmp(output_data, "Special data for include id 1", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 40) try block with special include" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::FAILURE);
    data_fetcher.setReturnData(true);
    assert(output_data_len == 0);
  }

  {
    cout << endl << "===================== Test 41) try block with special include" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    assert(strncmp(output_data, "Special data for include id 2", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 42) special include try block" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);

    // this is to make the StubHandler return failure
    data_fetcher.setReturnData(false);

    // this is to decrement HttpDataFetcher's pending request count - argument content doesn't matter
    data_fetcher.getContent("blah", output_data, output_data_len);

    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    data_fetcher.setReturnData(true);
    assert(output_data_len == 6);
    assert(strncmp(output_data, "except", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 43) comment tag" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:comment text=\"bleh\"/>");

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 0);
  }

  {
    cout << endl << "===================== Test 44) using packed node list" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiParser parser("parser", &Debug, &Error);
    DocNodeList node_list;
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");
    assert(parser.parse(node_list, input_data) == true);

    string packedNodeList = node_list.pack();

    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);

    assert(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_SUCCESS);
    const char *output_data;
    int output_data_len = 0;
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1);
    assert(strncmp(output_data, "Special data for include id 2", output_data_len) == 0);

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
    assert(parser.parse(node_list, input_data) == true);
    packedNodeList = node_list.pack();
    assert(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_SUCCESS);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 1 + FETCHER_STATIC_DATA_SIZE + 3);
    assert(strncmp(output_data, "\t>>>>> Content for URL [foo] <<<<<", output_data_len) == 0);
  }

  {
    cout << endl << "===================== Test 45) using packed node list" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
    string input_data("<esi:comment text=\"bleh\"/>");

    EsiParser parser("parser", &Debug, &Error);
    DocNodeList node_list;
    string input_data2("<esi:try>"
                       "<esi:attempt>"
                       "<esi:special-include handler=stub />"
                       "</esi:attempt>"
                       "<esi:except>"
                       "<esi:special-include handler=stub />"
                       "</esi:except>"
                       "</esi:try>");
    assert(parser.parse(node_list, input_data2) == true);

    string packedNodeList = node_list.pack();

    const char *output_data;
    int output_data_len = 0;

    assert(esi_proc.addParseData(input_data.c_str(), input_data.size()) == true);
    assert(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_IN_PROGRESS);
    assert(esi_proc.completeParse() == true);
    assert(esi_proc.usePackedNodeList(packedNodeList) == EsiProcessor::PROCESS_IN_PROGRESS);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == 0);
  }

  {
    cout << endl << "===================== Test 46) special include with footer" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);
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
    assert(esi_proc.completeParse(input_data) == true);
    data_fetcher.setReturnData(false);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::NEED_MORE_DATA);
    data_fetcher.setReturnData(true);
    assert(esi_proc.process(output_data, output_data_len) == EsiProcessor::SUCCESS);
    assert(output_data_len == StubIncludeHandler::DATA_PREFIX_SIZE + 1 + StubIncludeHandler::FOOTER_SIZE);
    assert(strncmp(output_data, "Special data for include id 2", output_data_len - StubIncludeHandler::FOOTER_SIZE) == 0);
    assert(strncmp(output_data + StubIncludeHandler::DATA_PREFIX_SIZE + 1, StubIncludeHandler::FOOTER,
                   StubIncludeHandler::FOOTER_SIZE) == 0);
    StubIncludeHandler::FOOTER      = 0;
    StubIncludeHandler::FOOTER_SIZE = 0;
  }

  {
    cout << endl << "===================== Test 47) using packed node list" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiParser parser("parser", &Debug, &Error);
    DocNodeList node_list;
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");
    assert(parser.parse(node_list, input_data) == true);

    string packedNodeList = node_list.pack();

    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);

    assert(esi_proc.usePackedNodeList(0, packedNodeList.size()) == EsiProcessor::UNPACK_FAILURE);
  }

  {
    cout << endl << "===================== Test 48) using packed node list" << endl;
    TestHttpDataFetcher data_fetcher;
    EsiParser parser("parser", &Debug, &Error);
    DocNodeList node_list;
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:special-include handler=stub />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:special-include handler=stub />"
                      "</esi:except>"
                      "</esi:try>");
    assert(parser.parse(node_list, input_data) == true);

    string packedNodeList = node_list.pack();

    EsiProcessor esi_proc("processor", "parser", "expression", &Debug, &Error, data_fetcher, esi_vars, handler_mgr);

    assert(esi_proc.usePackedNodeList(packedNodeList.data(), 0) == EsiProcessor::UNPACK_FAILURE);
  }

  cout << endl << "All tests passed!" << endl;
  return 0;
}
