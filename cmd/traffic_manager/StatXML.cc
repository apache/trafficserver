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

#include "ts/ink_config.h"
#include "StatXML.h"
#include <stdlib.h>
#include <ctype.h>

//
// Extract the text between a pair of XML tag and returns the length
// of the extracted text.
//
unsigned short
XML_extractContent(const char *name, char *content, size_t result_len)
{
  char c;
  int contentIndex = 0;

  memset(content, 0, result_len);
  for (unsigned short nameIndex = 0; name[nameIndex] != '<'; nameIndex += 1) {
    c = name[nameIndex];

    if (isspace(c)) {
      continue;
    }

    if (isOperator(c)) {
      content[contentIndex++] = ' ';
      content[contentIndex++] = c;
      content[contentIndex++] = ' ';
    } else {
      content[contentIndex++] = c;
    }
  }

  return (strlen(content));
}

//
// Returns true  if 'c'is an operator (in our definition),
//         false otherwise
//
bool
isOperator(char c)
{
  switch (c) {
  case '+':
  case '-':
  case '*':
  case '/':
  case '(':
  case ')':
    return true;
  default:
    return false;
  }
}
