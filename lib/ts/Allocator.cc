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

#include "ink_unused.h"    /* MAGIC_EDITING_TAG */
/****************************************************************************

  Allocator.h


*****************************************************************************/

#include "inktomi++.h"
#include "Allocator.h"
#include "ink_align.h"

Allocator::Allocator(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment)
{
#ifdef USE_DALLOC
  da.init(name, element_size, alignment);
#else
  ink_freelist_init(&fl, name, element_size, chunk_size, 0, alignment);
#endif
}
