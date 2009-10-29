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



#ifndef _LIST_H_
#define _LIST_H_


/* TODO change this list implementation */
/* This structure is used to store pairs Query string parameters name/value
   and cookies name/value */
#define PAIR_LIST_MAX_ELEM 32

typedef struct
{
  int nbelem;
  char *name[PAIR_LIST_MAX_ELEM];
  char *value[PAIR_LIST_MAX_ELEM];
} PairList;

char *getNextValue(const char *list, char **offset);

void pairListInit(PairList * list);

void pairListFree(PairList * list);

void pairListAdd(PairList * list, const char *name, const char *value);

const char *pairListGetValue(PairList * list, const char *name);

int pairListContains(PairList * list, const char *name);


#endif
