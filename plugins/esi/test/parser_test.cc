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

#include "EsiParser.h"
#include "print_funcs.h"
#include "Utils.h"

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using namespace EsiLib;

void
check_node_attr(const Attribute &attr, const char *name, const char *value)
{
  int name_len  = strlen(name);
  int value_len = strlen(value);
  assert(attr.name_len == name_len);
  assert(attr.value_len == value_len);
  assert(strncmp(attr.name, name, name_len) == 0);
  assert(strncmp(attr.value, value, value_len) == 0);
}

pthread_key_t threadKey;

int
main()
{
  pthread_key_create(&threadKey, NULL);
  Utils::init(&Debug, &Error);

  {
    cout << endl << "==================== Test 1: No src attr test " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 2: Empty src test " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=/>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 3: Valid src test " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=abc />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNode &node = node_list.back();
    assert(node.type == DocNode::TYPE_INCLUDE);
    assert(node.data_len == 0);
    assert(node.attr_list.size() == 1);
    assert(node.child_nodes.size() == 0);
    check_node_attr(node.attr_list.front(), "src", "abc");
  }

  {
    cout << endl << "==================== Test 4: Invalid Quoted URL test " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=\"abc def />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 5: Invalid Quoted URL test " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=abcdef\" />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 6: Invalid Quoted URL test " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=abc\"\"de\"f />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 7: Quoted URL test " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=\"abc def\" />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNode &node = node_list.back();
    assert(node.type == DocNode::TYPE_INCLUDE);
    assert(node.data_len == 0);
    assert(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "src", "abc def");
    assert(node.child_nodes.size() == 0);
  }

  {
    cout << endl << "==================== Test 8: Invalid tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "bleh <esi:blah /> flah";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 9: Invalid Comment tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:comment></esi:comment>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 10: Valid Comment tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:comment text=\"blah\"/>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    assert(node_list.begin()->child_nodes.size() == 0);
  }

  {
    cout << endl << "==================== Test 11: Invalid remove tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:remove />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 12: Valid remove tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:remove> </esi:remove>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
  }

  {
    cout << endl << "==================== Test 13: Interleaving raw text " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "foo <esi:remove> </esi:remove> bar";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 3);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_REMOVE);
    assert(list_iter->data_len == 0);
    assert(list_iter->data == 0);
    assert((list_iter->child_nodes).size() == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
  }

  {
    cout << endl << "==================== Test 14: Interleaving different nodes" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "foo <esi:remove> </esi:remove> bar <esi:include src=blah /><esi:vars>bleh</esi:vars>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 5);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_REMOVE);
    assert(list_iter->data_len == 0);
    assert(list_iter->data == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 5);
    assert(strncmp(list_iter->data, " bar ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_VARS);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "bleh", list_iter->data_len) == 0);
  }

  {
    cout << endl << "==================== Test 15: empty parse" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    assert(parser.completeParse(node_list) == true);
  }

  {
    cout << endl << "==================== Test 16: clear() test" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    string input_data = "foo <esi:remove> </esi:remove> bar <esi:include src=blah />";

    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 4);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_REMOVE);
    assert(list_iter->data_len == 0);
    assert(list_iter->data == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 5);
    assert(strncmp(list_iter->data, " bar ", list_iter->data_len) == 0);
    assert((list_iter->child_nodes).size() == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");

    parser.clear();
    node_list.clear();

    input_data = "foo <esi:remove> </esi:remove> bar";

    assert(parser.parseChunk(input_data.c_str(), node_list, -1) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 3);
    list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    assert((list_iter->child_nodes).size() == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_REMOVE);
    assert(list_iter->data_len == 0);
    assert(list_iter->data == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
  }

  {
    cout << endl << "==================== Test 17: multi-chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <esi:include src=url1/> bar1\n";
    char line2[] = "foo2 <esi:include src=url2/> bar2\n";
    char line3[] = "<esi:include src=\"blah bleh\"/>";
    char line4[] = "<esi:comment text=\"bleh\"/>";
    char line5[] = "<esi:remove> <a href=> </esi:remove>";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.parseChunk(line2, node_list) == true);
    assert(node_list.size() == 4);
    assert(parser.parseChunk(line3, node_list) == true);
    assert(node_list.size() == 6);
    assert(parser.parseChunk(line4, node_list) == true);
    assert(node_list.size() == 7);
    assert(parser.parseChunk(line5, node_list) == true);
    assert(node_list.size() == 8);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 8);
  }

  {
    cout << endl << "==================== Test 18: multi-chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <esi:include";
    char line2[] = "src=url2/>";
    char line3[] = "bar3";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 1);
    assert(parser.parseChunk(line2, node_list) == false);
    assert(node_list.size() == 1);
    assert(parser.parseChunk(line3, node_list) == false);
    assert(node_list.size() == 1);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 19: multi-chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <esi:include ";
    char line2[] = "src=url2/>";
    char line3[] = "bar3";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 1);
    assert(parser.parseChunk(line2, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.parseChunk(line3, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 3);
  }

  {
    cout << endl << "==================== Test 20: multi-chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=url2/>";
    char line3[] = "bar3";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 0);
    assert(parser.parseChunk(line2, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.parseChunk(line3, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 3);
  }

  {
    cout << endl << "==================== Test 21: multi-chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "xsi:include src=url2/>";
    char line3[] = "bar3";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 0);
    assert(parser.parseChunk(line2, node_list) == true);
    assert(node_list.size() == 0);
    assert(parser.parseChunk(line3, node_list) == true);
    assert(node_list.size() == 0);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
  }

  {
    cout << endl << "==================== Test 22: multi-chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=ur";
    char line3[] = "l2/>bar3";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 0);
    assert(parser.parseChunk(line2, node_list) == true);
    assert(node_list.size() == 1);
    assert(parser.parseChunk(line3, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 3);

    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 5);
    assert(strncmp(list_iter->data, "foo1 ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "bar3", list_iter->data_len) == 0);
  }

  {
    cout << endl << "==================== Test 23: multi-chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=ur";
    char line3[] = "l2/>bar3";
    char line4[] = "<esi:remove>blah</esi:remove> <esi:comment";
    char line5[] = " text=\"foo\"/>";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 0);
    assert(parser.parseChunk(line2, node_list) == true);
    assert(node_list.size() == 1);
    assert(parser.parseChunk(line3, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.parseChunk(line4, node_list) == true);
    assert(node_list.size() == 5);
    assert(parser.parseChunk(line5, node_list) == true);
    assert(node_list.size() == 6);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 6);

    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 5);
    assert(strncmp(list_iter->data, "foo1 ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "bar3", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_REMOVE);
    assert(list_iter->data_len == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 1);
    assert(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_COMMENT);
    assert(list_iter->data_len == 0);
  }

  {
    cout << endl << "==================== Test 24: one-shot parse" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "foo <esi:include src=blah /> bar";

    DocNodeList node_list;
    assert(parser.completeParse(node_list, input_data) == true);
    assert(node_list.size() == 3);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
    assert((list_iter->child_nodes).size() == 0);
  }

  {
    cout << endl << "==================== Test 25: final chunk" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    char line1[] = "foo1 <e";
    char line2[] = "si:include src=ur";
    char line3[] = "l2/>bar3";
    char line4[] = "<esi:remove>blah</esi:remove> <esi:comment";
    char line5[] = " bar/>";
    assert(parser.parseChunk(line1, node_list) == true);
    assert(node_list.size() == 0);
    assert(parser.parseChunk(line2, node_list) == true);
    assert(node_list.size() == 1);
    assert(parser.parseChunk(line3, node_list) == true);
    assert(node_list.size() == 2);
    assert(parser.parseChunk(line4, node_list) == true);
    assert(node_list.size() == 5);
    assert(parser.completeParse(node_list, line5, sizeof(line5) - 1) == true);
    assert(node_list.size() == 6);

    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 5);
    assert(strncmp(list_iter->data, "foo1 ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "bar3", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_REMOVE);
    assert(list_iter->data_len == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 1);
    assert(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_COMMENT);
    assert(list_iter->data_len == 0);
  }

  {
    cout << endl << "==================== Test 26: partial trailing tag" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "foo <esi:include src=blah /> <esi";

    DocNodeList node_list;
    assert(parser.completeParse(node_list, input_data) == true);
    assert(node_list.size() == 3);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 5);
    assert(strncmp(list_iter->data, " <esi", list_iter->data_len) == 0);
  }

  {
    cout << endl << "==================== Test 27: partial trailing tag" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "foo <esi:include src=blah /> <esi:remov";

    DocNodeList node_list;
    assert(parser.completeParse(node_list, input_data) == true);
    assert(node_list.size() == 4);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 4);
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "blah");
    ++list_iter;
    // parsing code adds the space and partial tag as two separate nodes
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 1);
    assert(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 10);
    assert(strncmp(list_iter->data, "<esi:remov", list_iter->data_len) == 0);
  }

  {
    cout << endl << "==================== Test 28: empty vars tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:vars></esi:vars>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_VARS);
    assert(list_iter->data_len == 0);
  }

  {
    cout << endl << "==================== Test 29: non-empty vars tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:vars>$(HTTP_COOKIE)</esi:vars>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_VARS);
    assert(list_iter->data_len == 14);
    assert(strncmp(list_iter->data, "$(HTTP_COOKIE)", list_iter->data_len) == 0);
  }

  {
    cout << endl << "==================== Test 30: choose tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:choose>"
                        "<esi:when test=blah><esi:include src=url /></esi:when>"
                        "</esi:choose>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_CHOOSE);
    assert(list_iter->data_len == 0);
    assert(list_iter->child_nodes.size() == 1);
    assert(list_iter->attr_list.size() == 0);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_WHEN);
    assert(list_iter2->data_len == 0);
    assert(list_iter2->child_nodes.size() == 1);
    assert(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "test", "blah");
    DocNodeList::iterator list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->data_len == 0);
    assert(list_iter3->child_nodes.size() == 0);
    assert(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "url");
  }

  {
    cout << endl << "==================== Test 31: when tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:when test=blah><esi:include src=url /></esi:when>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_WHEN);
    assert(list_iter->data_len == 0);
    assert(list_iter->child_nodes.size() == 1);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "test", "blah");
  }

  {
    cout << endl << "==================== Test 32: otherwise tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:otherwise><esi:include src=url /></esi:otherwise>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_OTHERWISE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->child_nodes.size() == 1);
  }

  {
    cout << endl << "==================== Test 33: try tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:try>"
                        "<esi:attempt><esi:include src=url1 /></esi:attempt>"
                        "<esi:except><esi:include src=url2 /></esi:except>"
                        "</esi:try>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_TRY);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->child_nodes.size() == 2);
    DocNodeList &child_nodes = list_iter->child_nodes;
    assert(child_nodes.size() == 2);
    list_iter = child_nodes.begin();
    assert(list_iter->type == DocNode::TYPE_ATTEMPT);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->child_nodes.size() == 1);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_INCLUDE);
    assert(list_iter2->data_len == 0);
    assert(list_iter2->child_nodes.size() == 0);
    assert(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url1");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_EXCEPT);
    assert(list_iter->data_len == 0);
    assert(list_iter->child_nodes.size() == 1);
    assert(list_iter->attr_list.size() == 0);
    list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_INCLUDE);
    assert(list_iter2->data_len == 0);
    assert(list_iter2->child_nodes.size() == 0);
    assert(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url2");
  }

  {
    cout << endl << "==================== Test 34: attempt/except tags " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:attempt><esi:include src=url1 /></esi:attempt>"
                        "<esi:except><esi:include src=url2 /></esi:except>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 2);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_ATTEMPT);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->child_nodes.size() == 1);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_INCLUDE);
    assert(list_iter2->data_len == 0);
    assert(list_iter2->child_nodes.size() == 0);
    assert(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url1");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_EXCEPT);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->child_nodes.size() == 1);
    list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_INCLUDE);
    assert(list_iter2->data_len == 0);
    assert(list_iter2->child_nodes.size() == 0);
    assert(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "src", "url2");
  }

  {
    cout << endl << "==================== Test 35: internal data pointer " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=abc />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNode &node = node_list.back();
    assert(node.type == DocNode::TYPE_INCLUDE);
    assert(node.data_len == 0);
    assert(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "src", "abc");
    input_data = "blah";
    assert(node.type == DocNode::TYPE_INCLUDE);
    assert(node.data_len == 0);
    assert(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "src", "abc");
  }

  {
    cout << endl << "==================== Test 36: external data pointer " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:include src=abc />";

    DocNodeList orig_node_list;
    assert(parser.parseChunk(input_data, orig_node_list) == true);
    assert(parser.completeParse(orig_node_list) == true);
    assert(orig_node_list.size() == 1);
    DocNode &orig_node = orig_node_list.back();
    assert(orig_node.type == DocNode::TYPE_INCLUDE);
    assert(orig_node.data_len == 0);
    assert(orig_node.attr_list.size() == 1);
    check_node_attr(orig_node.attr_list.front(), "src", "abc");

    DocNodeList new_node_list;
    input_data = "foo<esi:try><esi:attempt></esi:attempt><esi:except></esi:except></esi:try>";
    assert(parser.parse(new_node_list, input_data) == true);

    // make sure orig pointers are still valid
    assert(orig_node.type == DocNode::TYPE_INCLUDE);
    assert(orig_node.data_len == 0);
    assert(orig_node.attr_list.size() == 1);
    check_node_attr(orig_node.attr_list.front(), "src", "abc");

    // check new pointers
    assert(new_node_list.size() == 2);
    DocNodeList::iterator list_iter = new_node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == static_cast<int>(strlen("foo")));
    assert(strncmp(list_iter->data, "foo", list_iter->data_len) == 0);

    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_TRY);
    DocNodeList &child_nodes = list_iter->child_nodes;
    assert(child_nodes.size() == 2);
    assert(list_iter->attr_list.size() == 0);
    DocNodeList::iterator list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_ATTEMPT);
    assert(list_iter2->data_len == 0);
    assert(list_iter2->child_nodes.size() == 0);
    assert(list_iter2->attr_list.size() == 0);
    ++list_iter2;
    assert(list_iter2->type == DocNode::TYPE_EXCEPT);
    assert(list_iter2->data_len == 0);
    assert(list_iter2->child_nodes.size() == 0);
    assert(list_iter2->attr_list.size() == 0);
    input_data[0] = 'b';
    input_data[1] = 'a';
    input_data[2] = 'r';
    list_iter     = new_node_list.begin();
    assert(strncmp(list_iter->data, "bar", 3) == 0);
  }

  {
    cout << endl << "==================== Test 37: html comment tag " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "foo <esi:comment text=\"blah\"/><!--esi <p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>-->"
                        "<esi:include src=url /> bar";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 5);
    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == static_cast<int>(strlen("foo ")));
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_COMMENT);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>")));
    assert(strncmp(list_iter->data, "<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == static_cast<int>(strlen(" bar")));
    assert(strncmp(list_iter->data, " bar", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 38: html comment tag - partial chunks " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
                           0};

    DocNodeList node_list;
    for (int i = 0; lines[i]; ++i) {
      assert(parser.parseChunk(lines[i], node_list) == true);
    }
    assert(parser.completeParse(node_list) == true);

    assert(node_list.size() == 14);

    DocNodeList::iterator list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == static_cast<int>(strlen("foo ")));
    assert(strncmp(list_iter->data, "foo ", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_COMMENT);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url1");
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>")));
    assert(strncmp(list_iter->data, "<p><esi:vars>Hello, $(HTTP_COOKIE{name})!</esi:vars></p>", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url2");
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("foo")));
    assert(strncmp(list_iter->data, "foo", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("bar")));
    assert(strncmp(list_iter->data, "bar", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("blah")));
    assert(strncmp(list_iter->data, "blah", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_COMMENT);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == static_cast<int>(strlen(" ")));
    assert(strncmp(list_iter->data, " ", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_REMOVE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("bleh ")));
    assert(strncmp(list_iter->data, "bleh ", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("blooh")));
    assert(strncmp(list_iter->data, "blooh", list_iter->data_len) == 0);
    assert(list_iter->attr_list.size() == 0);
    ++list_iter;

    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url3");
    ++list_iter;
  }

  {
    cout << endl << "==================== Test 39: opening tag corner cases" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    DocNodeList node_list;
    DocNodeList::iterator list_iter;

    assert(parser.parse(node_list, "<<esi:include src=url/>") == true);
    assert(node_list.size() == 2);
    list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 1);
    assert(list_iter->data[0] == '<');
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->data_len == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "url");

    assert(parser.parse(node_list, "<<!--esi <esi:comment text=blah/>-->") == true);
    assert(node_list.size() == 4);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 1);
    assert(list_iter->data[0] == '<');
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_HTML_COMMENT);
    assert(list_iter->data_len == static_cast<int>(strlen("<esi:comment text=blah/>")));
    assert(strncmp(list_iter->data, "<esi:comment text=blah/>", list_iter->data_len) == 0);

    assert(parser.parse(node_list, "<!<esi:comment text=blah/>") == true);
    assert(node_list.size() == 6);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->data_len == 2);
    assert(strncmp(list_iter->data, "<!", list_iter->data_len) == 0);
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_COMMENT);

    assert(parser.parse(node_list, "<esi<!--esi <esi:comment text=blah/>") == false);
    assert(node_list.size() == 6);

    assert(parser.parse(node_list, "<esi:<!--esi <esi:comment text=blah/>-->/>") == false);
    assert(node_list.size() == 6);
  }

  {
    cout << endl << "==================== Test 40: No handler attr " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:special-include />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 41: Empty handle " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:special-include handler=/>";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "==================== Test 42: Valid special include " << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "<esi:special-include handler=ads pos=SKY />";

    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNode &node = node_list.back();
    assert(node.type == DocNode::TYPE_SPECIAL_INCLUDE);
    assert(node.data_len == static_cast<int>(strlen("handler=ads pos=SKY ")));
    assert(strncmp(node.data, "handler=ads pos=SKY ", node.data_len) == 0);
    assert(node.attr_list.size() == 1);
    check_node_attr(node.attr_list.front(), "handler", "ads");
  }

  {
    cout << endl << "===================== Test 43) choose-when" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data1, node_list) == true);
    assert(parser.parseChunk(input_data2, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNode *node = &(*(node_list.begin()));
    assert(node->type == DocNode::TYPE_CHOOSE);
    assert((node->child_nodes).size() == 3);
    DocNodeList::iterator list_iter = (node->child_nodes).begin();
    assert(list_iter->type == DocNode::TYPE_WHEN);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "test", "cond1");
    assert(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    assert(node->type == DocNode::TYPE_INCLUDE);
    assert(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "foo");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_WHEN);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "test", "cond2");
    assert(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    assert(node->type == DocNode::TYPE_INCLUDE);
    assert(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "bar");
    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_OTHERWISE);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    assert(node->type == DocNode::TYPE_INCLUDE);
    assert(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "otherwise");
  }

  {
    cout << endl << "===================== Test 44) invalid choose; non when/otherwise node" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 45) invalid choose; multiple otherwise" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 46) choose-when" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data("<esi:choose>"
                      "<esi:otherwise>"
                      "<esi:include src=otherwise />"
                      "</esi:otherwise>"
                      "</esi:choose>");
    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 1);
    DocNode *node = &(*(node_list.begin()));
    assert(node->type == DocNode::TYPE_CHOOSE);
    assert((node->child_nodes).size() == 1);
    DocNodeList::iterator list_iter = (node->child_nodes).begin();
    assert(list_iter->type == DocNode::TYPE_OTHERWISE);
    assert(list_iter->child_nodes.size() == 1);
    node = &(*(list_iter->child_nodes.begin()));
    assert(node->type == DocNode::TYPE_INCLUDE);
    assert(node->attr_list.size() == 1);
    check_node_attr(node->attr_list.front(), "src", "otherwise");
  }

  {
    cout << endl << "===================== Test 47) invalid try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data("<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=attempt />"
                      "</esi:attempt>"
                      "</esi:try>");
    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 48) invalid try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data("<esi:try>"
                      "<esi:except>"
                      "<esi:include src=except />"
                      "</esi:except>"
                      "</esi:try>");
    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 49) invalid try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 50) invalid try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 51) invalid try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 52) invalid try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 53) try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data1, node_list) == true);
    assert(parser.parseChunk(input_data2, node_list) == true);
    assert(parser.completeParse(node_list) == true);
    assert(node_list.size() == 4);
    DocNodeList::iterator list_iter, list_iter2, list_iter3;
    list_iter = node_list.begin();
    assert(list_iter->type == DocNode::TYPE_INCLUDE);
    assert(list_iter->child_nodes.size() == 0);
    assert(list_iter->attr_list.size() == 1);
    check_node_attr(list_iter->attr_list.front(), "src", "pre");

    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->child_nodes.size() == 0);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->data_len == static_cast<int>(strlen("foo")));
    assert(strncmp(list_iter->data, "foo", list_iter->data_len) == 0);

    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_TRY);
    assert(list_iter->child_nodes.size() == 2);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->data_len == 0);

    list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_ATTEMPT);
    assert(list_iter2->child_nodes.size() == 2);
    assert(list_iter2->attr_list.size() == 0);
    assert(list_iter2->data_len == 0);

    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_PRE);
    assert(list_iter3->child_nodes.size() == 0);
    assert(list_iter3->attr_list.size() == 0);
    assert(list_iter3->data_len == static_cast<int>(strlen("bar")));
    assert(strncmp(list_iter3->data, "bar", list_iter3->data_len) == 0);

    ++list_iter3;
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->child_nodes.size() == 0);
    assert(list_iter3->attr_list.size() == 1);
    assert(list_iter3->data_len == 0);
    check_node_attr(list_iter3->attr_list.front(), "src", "attempt");

    ++list_iter2;
    assert(list_iter2->type == DocNode::TYPE_EXCEPT);
    assert(list_iter2->child_nodes.size() == 1);
    assert(list_iter2->attr_list.size() == 0);
    assert(list_iter2->data_len == 0);

    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->child_nodes.size() == 0);
    assert(list_iter3->attr_list.size() == 1);
    assert(list_iter3->data_len == 0);
    check_node_attr(list_iter3->attr_list.front(), "src", "except");

    ++list_iter;
    assert(list_iter->type == DocNode::TYPE_PRE);
    assert(list_iter->child_nodes.size() == 0);
    assert(list_iter->attr_list.size() == 0);
    assert(list_iter->data_len == static_cast<int>(strlen("bar")));
    assert(strncmp(list_iter->data, "bar", list_iter->data_len) == 0);
  }

  {
    cout << endl << "===================== Test 54) invalid choose-when" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 55) invalid choose; multiple otherwise" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 56) invalid try block" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data("<esi:try>"
                      "</esi:try>");
    DocNodeList node_list;
    assert(parser.parseChunk(input_data, node_list) == false);
    assert(parser.completeParse(node_list) == false);
    assert(node_list.size() == 0);
  }

  {
    cout << endl << "===================== Test 57) choose/try combo" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
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
    assert(parser.completeParse(node_list, input_data) == true);
    assert(node_list.size() == 1);
    DocNodeList::iterator list_iter = node_list.begin(), list_iter2, list_iter3;
    assert(list_iter->type == DocNode::TYPE_CHOOSE);

    list_iter2 = list_iter->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_WHEN);
    assert(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "test", "c1");
    list_iter2 = list_iter2->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_TRY);
    list_iter2 = list_iter2->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_ATTEMPT);
    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->data_len == 0);
    assert(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "foo1");
    ++list_iter2;
    assert(list_iter2->type == DocNode::TYPE_EXCEPT);
    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->data_len == 0);
    assert(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "bar1");

    list_iter2 = list_iter->child_nodes.begin();
    ++list_iter2;
    assert(list_iter2->type == DocNode::TYPE_WHEN);
    assert(list_iter2->attr_list.size() == 1);
    check_node_attr(list_iter2->attr_list.front(), "test", "c2");
    list_iter2 = list_iter2->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_TRY);
    list_iter2 = list_iter2->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_ATTEMPT);
    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->data_len == 0);
    assert(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "foo2");
    ++list_iter2;
    assert(list_iter2->type == DocNode::TYPE_EXCEPT);
    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->data_len == 0);
    assert(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "bar2");

    list_iter2 = list_iter->child_nodes.begin();
    ++list_iter2;
    ++list_iter2;
    assert(list_iter2->type == DocNode::TYPE_OTHERWISE);
    assert(list_iter2->attr_list.size() == 0);
    list_iter2 = list_iter2->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_TRY);
    list_iter2 = list_iter2->child_nodes.begin();
    assert(list_iter2->type == DocNode::TYPE_ATTEMPT);
    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->data_len == 0);
    assert(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "foo3");
    ++list_iter2;
    assert(list_iter2->type == DocNode::TYPE_EXCEPT);
    list_iter3 = list_iter2->child_nodes.begin();
    assert(list_iter3->type == DocNode::TYPE_INCLUDE);
    assert(list_iter3->data_len == 0);
    assert(list_iter3->attr_list.size() == 1);
    check_node_attr(list_iter3->attr_list.front(), "src", "bar3");
  }

  {
    cout << endl << "===================== Test 58) '>' and '>=' operators" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data("<esi:choose>"
                      "<esi:when test=\"a>b\">foo</esi:when>"
                      "<esi:when test=\"c >= d\">bar</esi:when>"
                      "</esi:choose>");
    DocNodeList node_list;
    assert(parser.completeParse(node_list, input_data) == true);
    DocNodeList::iterator list_iter   = node_list.begin()->child_nodes.begin();
    AttributeList::iterator attr_iter = list_iter->attr_list.begin();
    assert(attr_iter->value_len == 3);
    assert(strncmp(attr_iter->value, "a>b", attr_iter->value_len) == 0);
    ++list_iter;
    attr_iter = list_iter->attr_list.begin();
    assert(attr_iter->value_len == 6);
    assert(strncmp(attr_iter->value, "c >= d", attr_iter->value_len) == 0);

    node_list.clear();
    parser.clear();
    input_data.assign("<esi:choose>"
                      "<esi:when test=a>b>foo</esi:when>"
                      "<esi:when test=\"c >= d\">bar</esi:when>"
                      "</esi:choose>");
    assert(parser.completeParse(node_list, input_data) == true);
    list_iter = node_list.begin()->child_nodes.begin();
    attr_iter = list_iter->attr_list.begin();
    assert(attr_iter->value_len == 1);
    assert(strncmp(attr_iter->value, "a", attr_iter->value_len) == 0);
    ++list_iter;
    attr_iter = list_iter->attr_list.begin();
    assert(attr_iter->value_len == 6);
    assert(strncmp(attr_iter->value, "c >= d", attr_iter->value_len) == 0);
  }

  cout << endl << "All tests passed!" << endl;
  return 0;
}
