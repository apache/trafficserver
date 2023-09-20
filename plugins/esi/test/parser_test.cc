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

#include "EsiParser.h"
#include "Utils.h"

using std::string;
using namespace EsiLib;

void
check_node_attr(const Attribute &attr, const char *name, const char *value)
{
  int name_len  = strlen(name);
  int value_len = strlen(value);
  REQUIRE(attr.name_len == name_len);
  REQUIRE(attr.value_len == value_len);
  REQUIRE(strncmp(attr.name, name, name_len) == 0);
  REQUIRE(strncmp(attr.value, value, value_len) == 0);
}

TEST_CASE("esi parser test")
{
  EsiParser parser;

  SECTION("No src attr")
  {
    string input_data = "<esi:include />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Empty src")
  {
    string input_data = "<esi:include src=/>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Valid src")
  {
    string input_data = "<esi:include src=abc />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNode &node = node_list.back();
    REQUIRE(node.type == DocNode::TYPE_INCLUDE);
    REQUIRE(node.data_len == 0);
    REQUIRE(node.attr_list.size() == 1);
    REQUIRE(node.child_nodes.size() == 0);
    check_node_attr(node.attr_list.front(), "src", "abc");
  }

  SECTION("Invalid Quoted URL")
  {
    string input_data = "<esi:include src=\"abc def />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Invalid Quoted URL 2")
  {
    string input_data = "<esi:include src=abcdef\" />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Invalid Quoted URL 3")
  {
    string input_data = R"(<esi:include src=abc""de"f />)";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Quoted URL")
  {
    string input_data = "<esi:include src=\"abc def\" />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNode &node = node_list.back();
    REQUIRE(node.type == DocNode::TYPE_INCLUDE);
    REQUIRE(node.data_len == 0);
    REQUIRE(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "src", "abc def");
    REQUIRE(node.child_nodes.size() == 0);
  }

  SECTION("Invalid tag")
  {
    string input_data = "bleh <esi:blah /> flah";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Invalid Comment tag")
  {
    string input_data = "<esi:comment></esi:comment>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Valid Comment tag")
  {
    string input_data = "<esi:comment text=\"blah\"/>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    REQUIRE(node_list.begin()->child_nodes.size() == 0);
  }

  SECTION("Invalid remove tag")
  {
    string input_data = "<esi:remove />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Valid remove tag")
  {
    string input_data = "<esi:remove> </esi:remove>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
  }

  SECTION("Iterleaving raw text")
  {
    string input_data = "foo <esi:remove> </esi:remove> bar";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 3);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_REMOVE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->data == nullptr);
    REQUIRE((list_iter->child_nodes).size() == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
  }

  SECTION("Interleaving different nodes")
  {
    string input_data = "foo <esi:remove> </esi:remove> bar <esi:include src=blah /><esi:vars>bleh</esi:vars>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 5);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_REMOVE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->data == nullptr);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 5);
    REQUIRE(strncmp(list_iter->data, " bar ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_VARS);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "bleh", list_iter->data_len) == 0);
  }

  SECTION("empty parse")
  {
    DocNodeList node_list;
    REQUIRE(parser.completeParse(node_list) == true);
  }

  SECTION("clear()")
  {
    DocNodeList node_list;
    string input_data = "foo <esi:remove> </esi:remove> bar <esi:include src=blah />";

    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 4);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_REMOVE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->data == nullptr);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 5);
    REQUIRE(strncmp(list_iter->data, " bar ", list_iter->data_len) == 0);
    REQUIRE((list_iter->child_nodes).size() == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");

    parser.clear();
    node_list.clear();

    input_data = "foo <esi:remove> </esi:remove> bar";

    REQUIRE(parser.parseChunk(input_data.c_str(), node_list, -1) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 3);
    list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    REQUIRE((list_iter->child_nodes).size() == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_REMOVE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->data == nullptr);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
  }

  SECTION("multi-chunk")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <esi:include src=url1/> bar1\n";
    char line2[] = "foo2 <esi:include src=url2/> bar2\n";
    char line3[] = "<esi:include src=\"blah bleh\"/>";
    char line4[] = "<esi:comment text=\"bleh\"/>";
    char line5[] = "<esi:remove> <a href=> </esi:remove>";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.parseChunk(line2, node_list) == true);
    REQUIRE(node_list.size() == 4);
    REQUIRE(parser.parseChunk(line3, node_list) == true);
    REQUIRE(node_list.size() == 6);
    REQUIRE(parser.parseChunk(line4, node_list) == true);
    REQUIRE(node_list.size() == 7);
    REQUIRE(parser.parseChunk(line5, node_list) == true);
    REQUIRE(node_list.size() == 8);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 8);
  }

  SECTION("multi-chunk 1")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <esi:include";
    char line2[] = "src=url2/>";
    char line3[] = "bar3";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 1);
    REQUIRE(parser.parseChunk(line2, node_list) == false);
    REQUIRE(node_list.size() == 1);
    REQUIRE(parser.parseChunk(line3, node_list) == false);
    REQUIRE(node_list.size() == 1);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("multi-chunk 3")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <esi:include ";
    char line2[] = "src=url2/>";
    char line3[] = "bar3";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 1);
    REQUIRE(parser.parseChunk(line2, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.parseChunk(line3, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 3);
  }

  SECTION("multi-chunk 4")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=url2/>";
    char line3[] = "bar3";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 0);
    REQUIRE(parser.parseChunk(line2, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.parseChunk(line3, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 3);
  }

  SECTION("multi-chunk 5")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "xsi:include src=url2/>";
    char line3[] = "bar3";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 0);
    REQUIRE(parser.parseChunk(line2, node_list) == true);
    REQUIRE(node_list.size() == 0);
    REQUIRE(parser.parseChunk(line3, node_list) == true);
    REQUIRE(node_list.size() == 0);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
  }

  SECTION("multi-chunk 6")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=ur";
    char line3[] = "l2/>bar3";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 0);
    REQUIRE(parser.parseChunk(line2, node_list) == true);
    REQUIRE(node_list.size() == 1);
    REQUIRE(parser.parseChunk(line3, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 3);

    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 5);
    REQUIRE(strncmp(list_iter->data, "foo1 ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "bar3", list_iter->data_len) == 0);
  }

  SECTION("multi-chunk 7")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=ur";
    char line3[] = "l2/>bar3";
    char line4[] = "<esi:remove>blah</esi:remove> <esi:comment";
    char line5[] = " text=\"foo\"/>";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 0);
    REQUIRE(parser.parseChunk(line2, node_list) == true);
    REQUIRE(node_list.size() == 1);
    REQUIRE(parser.parseChunk(line3, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.parseChunk(line4, node_list) == true);
    REQUIRE(node_list.size() == 5);
    REQUIRE(parser.parseChunk(line5, node_list) == true);
    REQUIRE(node_list.size() == 6);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 6);

    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 5);
    REQUIRE(strncmp(list_iter->data, "foo1 ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "bar3", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_REMOVE);
    REQUIRE(list_iter->data_len == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 1);
    REQUIRE(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_COMMENT);
    REQUIRE(list_iter->data_len == 0);
  }

  SECTION("one-shot parse")
  {
    string input_data = "foo <esi:include src=blah /> bar";

    DocNodeList node_list;
    REQUIRE(parser.completeParse(node_list, input_data) == true);
    REQUIRE(node_list.size() == 3);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
    REQUIRE((list_iter->child_nodes).size() == 0);
  }

  SECTION("final chunk")
  {
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=ur";
    char line3[] = "l2/>bar3";
    char line4[] = "<esi:remove>blah</esi:remove> <esi:comment";
    char line5[] = " bar/>";
    REQUIRE(parser.parseChunk(line1, node_list) == true);
    REQUIRE(node_list.size() == 0);
    REQUIRE(parser.parseChunk(line2, node_list) == true);
    REQUIRE(node_list.size() == 1);
    REQUIRE(parser.parseChunk(line3, node_list) == true);
    REQUIRE(node_list.size() == 2);
    REQUIRE(parser.parseChunk(line4, node_list) == true);
    REQUIRE(node_list.size() == 5);
    REQUIRE(parser.completeParse(node_list, line5, sizeof(line5) - 1) == true);
    REQUIRE(node_list.size() == 6);

    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 5);
    REQUIRE(strncmp(list_iter->data, "foo1 ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "bar3", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_REMOVE);
    REQUIRE(list_iter->data_len == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 1);
    REQUIRE(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_COMMENT);
    REQUIRE(list_iter->data_len == 0);
  }

  SECTION("partial trailing tag")
  {
    string input_data = "foo <esi:include src=blah /> <esi";

    DocNodeList node_list;
    REQUIRE(parser.completeParse(node_list, input_data) == true);
    REQUIRE(node_list.size() == 3);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 5);
    REQUIRE(strncmp(list_iter->data, " <esi", list_iter->data_len) == 0);
  }

  SECTION("partial trailing tag 2")
  {
    string input_data = "foo <esi:include src=blah /> <esi:remov";

    DocNodeList node_list;
    REQUIRE(parser.completeParse(node_list, input_data) == true);
    REQUIRE(node_list.size() == 4);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 4);
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    // parsing code adds the space and partial tag as two separate nodes
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 1);
    REQUIRE(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 10);
    REQUIRE(strncmp(list_iter->data, "<esi:remov", list_iter->data_len) == 0);
  }

  SECTION("empty vars tag")
  {
    string input_data = "<esi:vars></esi:vars>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_VARS);
    REQUIRE(list_iter->data_len == 0);
  }

  SECTION("non-empty vars tag")
  {
    string input_data = "<esi:vars>$(HTTP_COOKIE)</esi:vars>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_VARS);
    REQUIRE(list_iter->data_len == 14);
    REQUIRE(strncmp(list_iter->data, "$(HTTP_COOKIE)", list_iter->data_len) == 0);
  }

  SECTION("choose tag")
  {
    string input_data = "<esi:choose>"
                        "<esi:when test=blah><esi:include src=url /></esi:when>"
                        "</esi:choose>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_CHOOSE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
    REQUIRE(list_iter->attr_list.size() == 0);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_WHEN);
    REQUIRE(list_iter2->data_len == 0);
    REQUIRE(list_iter2->child_nodes.size() == 1);
    REQUIRE(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "test", "blah");
    DocNodeList::iterator list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->data_len == 0);
    REQUIRE(list_iter3->child_nodes.size() == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "url");
  }

  SECTION("when tag")
  {
    string input_data = "<esi:when test=blah><esi:include src=url /></esi:when>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_WHEN);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "test", "blah");
  }

  SECTION("otherwise tag")
  {
    string input_data = "<esi:otherwise><esi:include src=url /></esi:otherwise>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_OTHERWISE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
  }

  SECTION("try tag")
  {
    string input_data = "<esi:try>"
                        "<esi:attempt><esi:include src=url1 /></esi:attempt>"
                        "<esi:except><esi:include src=url2 /></esi:except>"
                        "</esi:try>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_TRY);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->child_nodes.size() == 2);
    DocNodeList &child_nodes = list_iter->child_nodes;
    REQUIRE(child_nodes.size() == 2);
    list_iter = child_nodes.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_ATTEMPT);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter2->data_len == 0);
    REQUIRE(list_iter2->child_nodes.size() == 0);
    REQUIRE(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url1");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_EXCEPT);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
    REQUIRE(list_iter->attr_list.size() == 0);
    list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter2->data_len == 0);
    REQUIRE(list_iter2->child_nodes.size() == 0);
    REQUIRE(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url2");
  }

  SECTION("attempt/except tags")
  {
    string input_data = "<esi:attempt><esi:include src=url1 /></esi:attempt>"
                        "<esi:except><esi:include src=url2 /></esi:except>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 2);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_ATTEMPT);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter2->data_len == 0);
    REQUIRE(list_iter2->child_nodes.size() == 0);
    REQUIRE(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url1");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_EXCEPT);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
    list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter2->data_len == 0);
    REQUIRE(list_iter2->child_nodes.size() == 0);
    REQUIRE(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url2");
  }

  SECTION("internal data pointer")
  {
    string input_data = "<esi:include src=abc />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNode &node = node_list.back();
    REQUIRE(node.type == DocNode::TYPE_INCLUDE);
    REQUIRE(node.data_len == 0);
    REQUIRE(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "src", "abc");
    input_data = "blah";
    REQUIRE(node.type == DocNode::TYPE_INCLUDE);
    REQUIRE(node.data_len == 0);
    REQUIRE(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "src", "abc");
  }

  SECTION("external data pointer")
  {
    string input_data = "<esi:include src=abc />";

    DocNodeList orig_node_list;
    REQUIRE(parser.parseChunk(input_data, orig_node_list) == true);
    REQUIRE(parser.completeParse(orig_node_list) == true);
    REQUIRE(orig_node_list.size() == 1);
    DocNode &orig_node = orig_node_list.back();
    REQUIRE(orig_node.type == DocNode::TYPE_INCLUDE);
    REQUIRE(orig_node.data_len == 0);
    REQUIRE(orig_node.attr_list.size() == 1);
    check_node_attr(orig_node.attr_list.front(), "src", "abc");

    DocNodeList new_node_list;
    input_data = "foo<esi:try><esi:attempt></esi:attempt><esi:except></esi:except></esi:try>";
    REQUIRE(parser.parse(new_node_list, input_data) == true);

    // make sure orig pointers are still valid
    REQUIRE(orig_node.type == DocNode::TYPE_INCLUDE);
    REQUIRE(orig_node.data_len == 0);
    REQUIRE(orig_node.attr_list.size() == 1);
    check_node_attr(orig_node.attr_list.front(), "src", "abc");

    // check new pointers
    REQUIRE(new_node_list.size() == 2);
    DocNodeList::iterator list_iter = new_node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("foo")));
    REQUIRE(strncmp(list_iter->data, "foo", list_iter->data_len) == 0);

    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_TRY);
    DocNodeList &child_nodes = list_iter->child_nodes;
    REQUIRE(child_nodes.size() == 2);
    REQUIRE(list_iter->attr_list.size() == 0);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_ATTEMPT);
    REQUIRE(list_iter2->data_len == 0);
    REQUIRE(list_iter2->child_nodes.size() == 0);
    REQUIRE(list_iter2->attr_list.size() == 0);
    ++list_iter2;
    REQUIRE(list_iter2->type == DocNode::TYPE_EXCEPT);
    REQUIRE(list_iter2->data_len == 0);
    REQUIRE(list_iter2->child_nodes.size() == 0);
    REQUIRE(list_iter2->attr_list.size() == 0);
    input_data[0] = 'b';
    input_data[1] = 'a';
    input_data[2] = 'r';
    list_iter     = new_node_list.begin();
    REQUIRE(strncmp(list_iter->data, "bar", 3) == 0);
  }

  SECTION("html comment tag")
  {
    string input_data = "foo <esi:comment text=\"blah\"/><!--esi <p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>-->"
                        "<esi:include src=url /> bar";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 5);
    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("foo ")));
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_COMMENT);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>")));
    REQUIRE(strncmp(list_iter->data, "<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen(" bar")));
    REQUIRE(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
  }

  SECTION("html comment tag - partial chunks")
  {
    const char *lines[] = {"foo ",
                           "<es",
                           "i:comment text=\"blah\"/><esi:include src=url1/>",
                           "<!--",
                           "esi <p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>-->",
                           "<esi:include src=url2 /><!--e",
                           "si foo--><!--esi bar-->",
                           "<!--esi blah--><esi:com",
                           "ment text=\"bleh\" /> <esi:remove> </esi:remove><!--esi bleh -->",
                           "<!--esi blooh--><esi:include src=url3/>",
                           nullptr};

    DocNodeList node_list;
    for (int i = 0; lines[i]; ++i) {
      REQUIRE(parser.parseChunk(lines[i], node_list) == true);
    }
    REQUIRE(parser.completeParse(node_list) == true);

    REQUIRE(node_list.size() == 14);

    DocNodeList::iterator list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("foo ")));
    REQUIRE(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_COMMENT);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url1");
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>")));
    REQUIRE(strncmp(list_iter->data, "<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("foo")));
    REQUIRE(strncmp(list_iter->data, "foo", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("bar")));
    REQUIRE(strncmp(list_iter->data, "bar", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("blah")));
    REQUIRE(strncmp(list_iter->data, "blah", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_COMMENT);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen(" ")));
    REQUIRE(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_REMOVE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("bleh ")));
    REQUIRE(strncmp(list_iter->data, "bleh ", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("blooh")));
    REQUIRE(strncmp(list_iter->data, "blooh", list_iter->data_len) == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    ++list_iter;

    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url3");
    ++list_iter;
  }

  SECTION("opening tag corner cases")
  {
    DocNodeList node_list;
    DocNodeList::iterator list_iter;

    REQUIRE(parser.parse(node_list, "<<esi:include src=url/>") == true);
    REQUIRE(node_list.size() == 2);
    list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 1);
    REQUIRE(list_iter->data[0] == '<');
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->data_len == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url");

    REQUIRE(parser.parse(node_list, "<<!--esi <esi:comment text=blah/>-->") == true);
    REQUIRE(node_list.size() == 4);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 1);
    REQUIRE(list_iter->data[0] == '<');
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("<esi:comment text=blah/>")));
    REQUIRE(strncmp(list_iter->data, "<esi:comment text=blah/>", list_iter->data_len) == 0);

    REQUIRE(parser.parse(node_list, "<!<esi:comment text=blah/>") == true);
    REQUIRE(node_list.size() == 6);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->data_len == 2);
    REQUIRE(strncmp(list_iter->data, "<!", list_iter->data_len) == 0);
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_COMMENT);

    REQUIRE(parser.parse(node_list, "<esi<!--esi <esi:comment text=blah/>") == false);
    REQUIRE(node_list.size() == 6);

    REQUIRE(parser.parse(node_list, "<esi:<!--esi <esi:comment text=blah/>-->/>") == false);
    REQUIRE(node_list.size() == 6);
  }

  SECTION("No handler attr")
  {
    string input_data = "<esi:special-include />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Empty handle")
  {
    string input_data = "<esi:special-include handler=/>";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("Valid special include")
  {
    string input_data = "<esi:special-include handler=ads pos=SKY />";

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNode &node = node_list.back();
    REQUIRE(node.type == DocNode::TYPE_SPECIAL_INCLUDE);
    REQUIRE(node.data_len == static_cast<int>(strlen("handler=ads pos=SKY ")));
    REQUIRE(strncmp(node.data, "handler=ads pos=SKY ", node.data_len) == 0);
    REQUIRE(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "handler", "ads");
  }

  SECTION("choose-when")
  {
    string input_data1("<esi:choose>"
                       "<esi:when test=cond1>"
                       "<esi:include src=foo />"
                       "</esi:when>"
                       "<esi:when test=cond2>"
                       "<esi:include src=bar />"),
      input_data2("</esi:when>"
                  "<esi:otherwise>"
                  "<esi:include src=otherwise />"
                  "</esi:otherwise>"
                  "</esi:choose>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data1, node_list) == true);
    REQUIRE(parser.parseChunk(input_data2, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNode *node = &(*(node_list.begin()));
    REQUIRE(node->type == DocNode::TYPE_CHOOSE);
    REQUIRE((node->child_nodes).size() == 3);
    DocNodeList::iterator list_iter = (node->child_nodes).begin();
    REQUIRE(list_iter->type == DocNode::TYPE_WHEN);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "test", "cond1");
    REQUIRE(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    REQUIRE(node->type == DocNode::TYPE_INCLUDE);
    REQUIRE(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "foo");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_WHEN);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "test", "cond2");
    REQUIRE(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    REQUIRE(node->type == DocNode::TYPE_INCLUDE);
    REQUIRE(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "bar");
    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_OTHERWISE);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    REQUIRE(node->type == DocNode::TYPE_INCLUDE);
    REQUIRE(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "otherwise");
  }

  SECTION("invalid choose; non when-otherwise node")
  {
    string input_data("<esi:choose>"
                      "<esi:comment text=\"blah\" />"
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
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid choose; multiple otherwise")
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
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "</esi:choose>");

    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("choose-when 2")
  {
    string input_data("<esi:choose>"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "</esi:choose>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 1);
    DocNode *node = &(*(node_list.begin()));
    REQUIRE(node->type == DocNode::TYPE_CHOOSE);
    REQUIRE((node->child_nodes).size() == 1);
    DocNodeList::iterator list_iter = (node->child_nodes).begin();
    REQUIRE(list_iter->type == DocNode::TYPE_OTHERWISE);
    REQUIRE(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    REQUIRE(node->type == DocNode::TYPE_INCLUDE);
    REQUIRE(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "otherwise");
  }

  SECTION("invalid try block")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "</esi:try>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid try block 2")
  {
    string input_data("<esi:try>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid try block 3")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:comment text=blah/>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid try block 4")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid try block 5")
  {
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid try block 6")
  {
    string input_data("<esi:include src=pre />"
                      "foo"
                      "<esi:try>"
                      "foo"
                      "<esi:attempt>"
                      "bar"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>"
                      "bar");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("try block")
  {
    string input_data1("<esi:include src=pre />"
                       "foo"
                       "<esi:try>\n\t  "
                       "<esi:attempt>"
                       "bar"
                       "<esi:include src=attempt />"
                       "</esi:attempt>"
                       "\n\n\t   "),
      input_data2("<esi:except>"
                  "<esi:include src=except />"
                  "</esi:except>"
                  "\n\t "
                  "</esi:try>"
                  "bar");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data1, node_list) == true);
    REQUIRE(parser.parseChunk(input_data2, node_list) == true);
    REQUIRE(parser.completeParse(node_list) == true);
    REQUIRE(node_list.size() == 4);
    DocNodeList::iterator list_iter, list_iter2, list_iter3;
    list_iter = node_list.begin();
    REQUIRE(list_iter->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter->child_nodes.size() == 0);
    REQUIRE(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "pre");

    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->child_nodes.size() == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("foo")));
    REQUIRE(strncmp(list_iter->data, "foo", list_iter->data_len) == 0);

    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_TRY);
    REQUIRE(list_iter->child_nodes.size() == 2);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->data_len == 0);

    list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_ATTEMPT);
    REQUIRE(list_iter2->child_nodes.size() == 2);
    REQUIRE(list_iter2->attr_list.size() == 0);
    REQUIRE(list_iter2->data_len == 0);

    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter3->child_nodes.size() == 0);
    REQUIRE(list_iter3->attr_list.size() == 0);
    REQUIRE(list_iter3->data_len == static_cast<int>(strlen("bar")));
    REQUIRE(strncmp(list_iter3->data, "bar", list_iter3->data_len) == 0);

    ++list_iter3;
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->child_nodes.size() == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    REQUIRE(list_iter3->data_len == 0);
    check_node_attr(list_iter3->attr_list.front(), "src", "attempt");

    ++list_iter2;
    REQUIRE(list_iter2->type == DocNode::TYPE_EXCEPT);
    REQUIRE(list_iter2->child_nodes.size() == 1);
    REQUIRE(list_iter2->attr_list.size() == 0);
    REQUIRE(list_iter2->data_len == 0);

    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->child_nodes.size() == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    REQUIRE(list_iter3->data_len == 0);
    check_node_attr(list_iter3->attr_list.front(), "src", "except");

    ++list_iter;
    REQUIRE(list_iter->type == DocNode::TYPE_PRE);
    REQUIRE(list_iter->child_nodes.size() == 0);
    REQUIRE(list_iter->attr_list.size() == 0);
    REQUIRE(list_iter->data_len == static_cast<int>(strlen("bar")));
    REQUIRE(strncmp(list_iter->data, "bar", list_iter->data_len) == 0);
  }

  SECTION("invalid choose-when")
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
                      "</esi:otherwise>foo"
                      "</esi:choose>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid choose; multiple otherwise")
  {
    string input_data("<esi:choose>\n"
                      "\t<esi:when test=foo>"
                      "<esi:include src=foo />"
                      "</esi:when>\n"
                      "\t<esi:when test=bar>"
                      "<esi:include src=bar />"
                      "</esi:when>\n"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "</esi:choose>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("invalid try block")
  {
    string input_data("<esi:try>"
                      "</esi:try>");
    DocNodeList node_list;
    REQUIRE(parser.parseChunk(input_data, node_list) == false);
    REQUIRE(parser.completeParse(node_list) == false);
    REQUIRE(node_list.size() == 0);
  }

  SECTION("choose/try combo")
  {
    string input_data("<esi:choose>"
                      "<esi:when test=c1>"
                      "<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=foo1 />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=bar1 />"
                      "</esi:except>"
                      "</esi:try>"
                      "</esi:when>"
                      "<esi:when test=c2>"
                      "<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=foo2 />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=bar2 />"
                      "</esi:except>"
                      "</esi:try>"
                      "</esi:when>"
                      "<esi:otherwise>"
                      "<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=foo3 />"
                      "</esi:attempt>"
                      "<esi:except>"
                      "<esi:include src=bar3 />"
                      "</esi:except>"
                      "</esi:try>"
                      "</esi:otherwise>"
                      "</esi:choose>");

    DocNodeList node_list;
    REQUIRE(parser.completeParse(node_list, input_data) == true);
    REQUIRE(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin(), list_iter2, list_iter3;
    REQUIRE(list_iter->type == DocNode::TYPE_CHOOSE);

    list_iter2 = list_iter->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_WHEN);
    REQUIRE(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "test", "c1");
    list_iter2 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_TRY);
    list_iter2 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_ATTEMPT);
    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->data_len == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "foo1");
    ++list_iter2;
    REQUIRE(list_iter2->type == DocNode::TYPE_EXCEPT);
    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->data_len == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "bar1");

    list_iter2 = list_iter->child_nodes.begin();
    ++list_iter2;
    REQUIRE(list_iter2->type == DocNode::TYPE_WHEN);
    REQUIRE(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "test", "c2");
    list_iter2 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_TRY);
    list_iter2 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_ATTEMPT);
    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->data_len == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "foo2");
    ++list_iter2;
    REQUIRE(list_iter2->type == DocNode::TYPE_EXCEPT);
    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->data_len == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "bar2");

    list_iter2 = list_iter->child_nodes.begin();
    ++list_iter2;
    ++list_iter2;
    REQUIRE(list_iter2->type == DocNode::TYPE_OTHERWISE);
    REQUIRE(list_iter2->attr_list.size() == 0);
    list_iter2 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_TRY);
    list_iter2 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter2->type == DocNode::TYPE_ATTEMPT);
    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->data_len == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "foo3");
    ++list_iter2;
    REQUIRE(list_iter2->type == DocNode::TYPE_EXCEPT);
    list_iter3 = list_iter2->child_nodes.begin();
    REQUIRE(list_iter3->type == DocNode::TYPE_INCLUDE);
    REQUIRE(list_iter3->data_len == 0);
    REQUIRE(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "bar3");
  }

  SECTION("'>' and '>=' operators")
  {
    string input_data("<esi:choose>"
                      "<esi:when test=\"a>b\">foo</esi:when>"
                      "<esi:when test=\"c >= d\">bar</esi:when>"
                      "</esi:choose>");
    DocNodeList node_list;
    REQUIRE(parser.completeParse(node_list, input_data) == true);
    DocNodeList::iterator list_iter   = node_list.begin()->child_nodes.begin();
    AttributeList::iterator attr_iter = list_iter->attr_list.begin();
    REQUIRE(attr_iter->value_len == 3);
    REQUIRE(strncmp(attr_iter->value, "a>b", attr_iter->value_len) == 0);
    ++list_iter;
    attr_iter = list_iter->attr_list.begin();
    REQUIRE(attr_iter->value_len == 6);
    REQUIRE(strncmp(attr_iter->value, "c >= d", attr_iter->value_len) == 0);

    node_list.clear();
    parser.clear();
    input_data.assign("<esi:choose>"
                      "<esi:when test=a>b>foo</esi:when>"
                      "<esi:when test=\"c >= d\">bar</esi:when>"
                      "</esi:choose>");
    REQUIRE(parser.completeParse(node_list, input_data) == true);
    list_iter = node_list.begin()->child_nodes.begin();
    attr_iter = list_iter->attr_list.begin();
    REQUIRE(attr_iter->value_len == 1);
    REQUIRE(strncmp(attr_iter->value, "a", attr_iter->value_len) == 0);
    ++list_iter;
    attr_iter = list_iter->attr_list.begin();
    REQUIRE(attr_iter->value_len == 6);
    REQUIRE(strncmp(attr_iter->value, "c >= d", attr_iter->value_len) == 0);
  }
}
