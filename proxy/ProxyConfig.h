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

/****************************************************************************

  ProxyConfig.h


  ****************************************************************************/

#ifndef _Proxy_Config_h
#define _Proxy_Config_h

#include "libts.h"
#include "ProcessManager.h"
#include "Error.h"

void *config_int_cb(void *data, void *value);
void *config_long_long_cb(void *data, void *value);
void *config_float_cb(void *data, void *value);
void *config_string511_cb(void *data, void *value);
void *config_string_alloc_cb(void *data, void *value);

//
// Macros that spin waiting for the data to be bound
//
#define SignalManager(_n,_d) pmgmt->signalManager(_n,(char*)_d)
#define SignalWarning(_n,_s) { Warning("%s", _s); SignalManager(_n,_s); }

#define RegisterMgmtCallback(_signal,_fn,_data) \
pmgmt->registerMgmtCallback(_signal,_fn,_data)


#define MAX_CONFIGS  100


struct ConfigInfo
{
  volatile int m_refcount;

    virtual ~ ConfigInfo()
  {
  }
};


class ConfigProcessor
{
public:
  ConfigProcessor();

  unsigned int set(unsigned int id, ConfigInfo * info);
  ConfigInfo *get(unsigned int id);
  void release(unsigned int id, ConfigInfo * data);

public:
  volatile ConfigInfo *infos[MAX_CONFIGS];
  volatile int ninfos;
};


extern ConfigProcessor configProcessor;

#endif
