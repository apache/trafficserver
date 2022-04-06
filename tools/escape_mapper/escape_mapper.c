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

#include <stdio.h>
#include <string.h>

void
add_mapping(unsigned char *table, char c)
{
  int i = c / 8;
  int x = 7 - c % 8;
  int r = 1 << x;

  table[i] = table[i] | r;
}

int
main(int argc, char *argv[])
{
  // only support a single arg that contains all the chars we wish to escapify
  if (argc > 1 && argc != 2) {
    printf("Provide a single argument with a list of characters to add to the default encoding table\n");
    return (1);
  }

  // default characters supported by the codes_to_escape table found in LogUtils.cc
  unsigned char to_escape[16] = {
    ' ', '"', '#', '%', '<', '>', '[', ']', '\\', '^', '`', '{', '|', '}', '~', 0x7F,
  };

  unsigned char escape_codes[32];
  memset(&escape_codes[0], 0, sizeof(escape_codes));

  // indexes 0-3 are marked as "control"
  for (int i = 0; i < 4; i++) {
    escape_codes[i] = 0xFF;
  }

  for (unsigned long i = 0; i < sizeof(to_escape) / sizeof(to_escape[0]); i++) {
    add_mapping(&escape_codes[0], to_escape[i]);
  }

  // add the chars specified in argv
  if (argc > 1) {
    for (unsigned long i = 0; i < strlen(argv[1]); i++) {
      printf("Adding %c to escape mapping table\n", argv[1][i]);
      add_mapping(&escape_codes[0], argv[1][i]);
    }

    printf("\n");
  }

  printf("%s Escape Mapping Table:\n", (argc > 1) ? "New" : "Default");

  for (unsigned long i = 0; i < sizeof(escape_codes) / sizeof(escape_codes[0]); i++) {
    printf("  %2lu: %#04x\n", i, escape_codes[i]);
  }

  return (0);
}
