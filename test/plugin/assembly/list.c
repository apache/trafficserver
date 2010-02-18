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



#include <ts/ts.h>
#include <strings.h>


#include "common.h"

#include "list.h"

/*-------------------------------------------------------------------------
  getNextValue

  Extract commas separated items from a string "elem1, elem2, ..., elemN"
  Returns pointer and size of the next item.
  -------------------------------------------------------------------------*/
char *
getNextValue(const char *list, char **offset)
{
  char *begin;
  char *end;
  char *ret;
  int len;

  if (list == NULL) {
    return NULL;
  }

  /* start after the last return item */
  begin = (char *) *offset;

  /* skip blanks before the item */
  while ((*begin == ' ') && (*begin != '\0')) {
    begin++;
  }

  if (*begin == '\0') {
    return NULL;
  }

  end = begin;
  len = 0;

  while ((*end != ',') && (*end != ' ') && (*end != '\0')) {
    end++;
    len++;
  }

  /* Make offset point after the item last character */
  if (*end != '\0') {
    *offset = (char *) (end + 1);
  } else {
    *offset = end;
  }

  ret = INKmalloc(len + 1);
  memcpy(ret, begin, len);
  ret[len] = '\0';

  return ret;
}



/*-------------------------------------------------------------------------
  pairList manipulation routines

  -------------------------------------------------------------------------*/

void
pairListInit(PairList * l)
{
  l->nbelem = 0;
}


void
pairListFree(PairList * l)
{
  int i;
  for (i = 0; i < l->nbelem; i++) {
    if (l->name[i]) {
      free(l->name[i]);
      l->name[i] = NULL;
    }
    if (l->value[i]) {
      free(l->value[i]);
      l->value[i] = NULL;
    }
  }
}

void
pairListAdd(PairList * l, const char *n, const char *v)
{
  l->name[l->nbelem] = strdup(n);
  l->value[l->nbelem] = strdup(v);
  (l->nbelem)++;
}

const char *
pairListGetValue(PairList * l, const char *name)
{
  int i;
  for (i = 0; i < l->nbelem; i++) {
    if (l->name[i]) {
      if (strcmp(l->name[i], name) == 0) {
        return (l->value[i]);
      }
    }
  }
  return NULL;
}

int
pairListContains(PairList * l, const char *name)
{
  int i;
  for (i = 0; i < l->nbelem; i++) {
    if (l->name[i]) {
      if (strcmp(l->name[i], name) == 0) {
        return 1;
      }
    }
  }
  return 0;
}
