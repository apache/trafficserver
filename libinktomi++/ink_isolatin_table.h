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

/*
 *
 * ink_isolatin_table.h
 *   The eight bit table used by the isolatin macros.
 *
 * $Date: 2003-06-01 18:36:44 $
 *
 *
 */

#ifndef _INK_ISOLATIN_TABLE_H
#define _INK_ISOLATIN_TABLE_H

#include "ink_apidefs.h"

#define UNDEF  0
#define DIGIT  1
#define ALPHL  2
#define ALPHU  3
#define PUNCT  4
#define WHSPC  5

/*
 * The eight bit table.
 */
extern inkcoreapi int eight_bit_table[];

#endif /* _INK_ISOLATIN_TABLE_H */
