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
#include <cassert>
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

void
checkNodeList1(const DocNodeList &node_list)
{
  DocNodeList::const_iterator list_iter = node_list.begin();
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

void
checkNodeList2(const DocNodeList &node_list)
{
  assert(node_list.size() == 1);
  DocNodeList::const_iterator list_iter = node_list.begin(), list_iter2, list_iter3;
  assert(list_iter->type == DocNode::TYPE_CHOOSE);

  list_iter2 = list_iter->child_nodes.begin();
  assert(list_iter2->type == DocNode::TYPE_WHEN);
  assert(list_iter2->attr_list.size() == 1);
  check_node_attr(list_iter2->attr_list.front(), "test", "c1");
  list_iter2 = list_iter2->child_nodes.begin();
  assert(list_iter2->type == DocNode::TYPE_TRY);
  list_iter2 = list_iter2->child_nodes.begin();
  assert(list_iter2->type == DocNode::TYPE_ATTEMPT);
  assert(list_iter2->child_nodes.size() == 2);
  list_iter3 = list_iter2->child_nodes.begin();
  assert(list_iter3->type == DocNode::TYPE_INCLUDE);
  assert(list_iter3->data_len == 0);
  assert(list_iter3->attr_list.size() == 1);
  check_node_attr(list_iter3->attr_list.front(), "src", "foo1");
  ++list_iter3;
  assert(list_iter3->type == DocNode::TYPE_PRE);
  assert(list_iter3->data_len == static_cast<int>(strlen("raw1")));
  assert(strncmp(list_iter3->data, "raw1", list_iter3->data_len) == 0);
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
  assert(list_iter2->child_nodes.size() == 2);
  list_iter3 = list_iter2->child_nodes.begin();
  assert(list_iter3->type == DocNode::TYPE_PRE);
  assert(list_iter3->data_len == static_cast<int>(strlen("raw2")));
  assert(strncmp(list_iter3->data, "raw2", list_iter3->data_len) == 0);
  ++list_iter3;
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

int
main()
{
  Utils::init(&Debug, &Error);

  {
    cout << endl << "==================== Test 1" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data = "foo <esi:include src=blah /> bar";

    DocNodeList node_list;
    assert(parser.completeParse(node_list, input_data) == true);
    checkNodeList1(node_list);
    assert(node_list.size() == 3);
    string packed = node_list.pack();
    node_list.clear();

    DocNodeList node_list2;
    assert(node_list2.unpack(packed) == true);
    assert(node_list2.size() == 3);
    checkNodeList1(node_list2);

    DocNodeList node_list3;
    assert(node_list3.unpack(nullptr, 90) == false);
    assert(node_list3.unpack(packed.data(), 3) == false);
    *(reinterpret_cast<int *>(&packed[0])) = -1;
    assert(node_list3.unpack(packed) == true);
    assert(node_list3.size() == 0);
    *(reinterpret_cast<int *>(&packed[0])) = 3;

    DocNodeList node_list4;
    assert(node_list4.unpack(packed) == true);
    assert(node_list4.size() == 3);
    checkNodeList1(node_list4);
  }

  {
    cout << endl << "==================== Test 2" << endl;
    EsiParser parser("parser_test", &Debug, &Error);
    string input_data("<esi:choose>"
                      "<esi:when test=c1>"
                      "<esi:try>"
                      "<esi:attempt>"
                      "<esi:include src=foo1 />"
                      "raw1"
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
                      "raw2"
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
    checkNodeList2(node_list);

    string packed = node_list.pack();
    DocNodeList node_list2;
    assert(node_list2.unpack(packed) == true);
    checkNodeList2(node_list2);

    string packed2;
    node_list.pack(packed2);
    assert(packed == packed2);
    node_list2.clear();
    assert(node_list2.unpack(packed2) == true);
    checkNodeList2(node_list2);

    string packed3("hello");
    node_list.pack(packed3, true);
    assert(packed3.size() == (packed.size() + 5));
    node_list2.clear();
    assert(node_list2.unpack(packed3) == false);
    assert(node_list2.unpack(packed3.data() + 5, packed3.size() - 5) == true);
    checkNodeList2(node_list2);
  }

  cout << "All tests passed" << endl;
  return 0;
}
