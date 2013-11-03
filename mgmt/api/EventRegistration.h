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

/*****************************************************************************
 * Filename: EventRegistration.h
 * Purpose: This file contains sturctures intended for event notification
 * Created: 8/8/00
 * Created by: Stephanie Song
 *
 ***************************************************************************/

#ifndef _EVENT_REGISTRATION_H
#define _EVENT_REGISTRATION_H

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

void *event_callback_thread(void *arg);
void *event_poll_thread_main(void *arg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _EVENT_REGISTRATION_H */
