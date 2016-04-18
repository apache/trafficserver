/** @file

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

int
main()
{
  unsigned char codes[32];
  unsigned char hex;
  int c;

  memset(codes, 0, sizeof(codes));

  for (c = 0; c <= 255; ++c) {
    if (((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c == '_')) || ((c == '-')) ||
        ((c == '.'))) {
    } else {
      codes[c / 8] |= (1 << (7 - c % 8));
    }
  }

  for (hex = 0; hex < 32; ++hex) {
    printf("0x%02lX, ", codes[hex]);
    if (!((hex + 1) % 4))
      printf("\n");
  }
}
