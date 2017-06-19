/** @file

  This file produces Unicorns and Rainbows for the ATS community

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

#ifndef _INK_SYS_CONTROL_H
#define _INK_SYS_CONTROL_H

#include <sys/resource.h>

rlim_t ink_max_out_rlimit(int which, bool unlim_it = true);
rlim_t ink_get_max_files();

#endif /*_INK_SYS_CONTROL_H*/
