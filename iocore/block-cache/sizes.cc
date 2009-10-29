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

#include "sizes.h"
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <assert.h>

int *sizeArray;
int offset = 0;
int nsizes = 0;

void
openSizes()
{
  FILE *fp = fopen("sizes", "r");
  assert(fp);
  int line = 0;
  char buf[32];
  while (fgets(buf, sizeof(buf), fp) != NULL) {
    nsizes++;
  }
  printf("%d in trace\n", nsizes);
  rewind(fp);
  sizeArray = (int *) calloc(sizeof(int), nsizes);
  if (!sizeArray) {
    perror("calloc");
    exit(1);
  }
  while (fgets(buf, sizeof(buf), fp) != NULL) {
    sizeArray[offset] = atoi(buf);
    offset++;
  }
  offset = 0;
  fclose(fp);
  printf("%d read\n", nsizes);
}

int
nextSize()
{
  int o = offset;
  offset++;
  if (offset == nsizes) {
    offset = 0;
  }
  return sizeArray[o];
}

void
rewindSizes()
{
  offset = 0;
}
