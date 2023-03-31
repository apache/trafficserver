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

#pragma once

#include "tscore/ink_config.h"

#ifndef HAVE_BIO_SET_DATA
#define HAVE_BIO_SET_DATA     1
#define BIO_set_data(a, _ptr) ((a)->ptr = (_ptr))
#endif

#ifndef HAVE_BIO_GET_DATA
#define HAVE_BIO_GET_DATA 1
#define BIO_get_data(a)   ((a)->ptr)
#endif

#ifndef HAVE_BIO_GET_SHUTDOWN
#define HAVE_BIO_GET_SHUTDOWN 1
#define BIO_get_shutdown(a)   ((a)->shutdown)
#endif

#ifndef HAVE_BIO_METH_GET_CTRL
#define HAVE_BIO_METH_GET_CTRL  1
#define BIO_meth_get_ctrl(biom) ((biom)->ctrl)
#endif

#ifndef HAVE_BIO_METH_GET_CREATE
#define HAVE_BIO_METH_GET_CREATE  1
#define BIO_meth_get_create(biom) ((biom)->create)
#endif

#ifndef HAVE_BIO_METH_GET_DESTROY
#define HAVE_BIO_METH_GET_DESTROY  1
#define BIO_meth_get_destroy(biom) ((biom)->destroy)
#endif
