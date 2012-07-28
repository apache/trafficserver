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

#include "libts.h"
#include "ProxyConfig.h"
#include "P_EventSystem.h"

ConfigProcessor configProcessor;


void *
config_int_cb(void *data, void *value)
{
  *(int *) data = *(int64_t *) value;
  return NULL;
}

void *
config_float_cb(void *data, void *value)
{
  *(float *) data = *(float *) value;
  return NULL;
}

void *
config_long_long_cb(void *data, void *value)
{
  *(int64_t *) data = *(int64_t *) value;
  return NULL;
}

/////////////////////////////////////////////////////////////
//
//  config_string_alloc_cb()
//
//  configuration callback function. The function is called
//  by the manager when a string configuration variable
//  changed. It allocates new memory for the new data.
//  the old variable is scheduled to be freed using
//  ConfigFreerContinuation which will free the memory
//  used for this variable after long time, assuming that
//  during all this time all the users of this memory will
//  disappear.
/////////////////////////////////////////////////////////////
void *
config_string_alloc_cb(void *data, void *value)
{
  char *_ss = (char *) value;
  char *_new_value = 0;

//#define DEBUG_CONFIG_STRING_UPDATE
#if defined (DEBUG_CONFIG_STRING_UPDATE)
  printf("config callback [new, old] = [%s : %s]\n",
         (_ss) ? (_ss) : (""), (*(char **) data) ? (*(char **) data) : (""));
#endif
  int len = -1;
  if (_ss) {
    len = strlen(_ss);
    _new_value = (char *)ats_malloc(len + 1);
    memcpy(_new_value, _ss, len + 1);
  }

  char *_temp2 = *(char **) data;
  *(char **) data = _new_value;

  // free old data
  if (_temp2 != 0)
    new_Freer(_temp2, HRTIME_DAY);

  return NULL;
}


class ConfigInfoReleaser:public Continuation
{
public:
  ConfigInfoReleaser(unsigned int id, ConfigInfo * info)
    : Continuation(new_ProxyMutex()), m_id(id), m_info(info)
  {
    SET_HANDLER(&ConfigInfoReleaser::handle_event);
  }

  int handle_event(int event, void *edata)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(edata);
    configProcessor.release(m_id, m_info);
    delete this;
    return 0;
  }

public:
  unsigned int m_id;
  ConfigInfo *m_info;
};


ConfigProcessor::ConfigProcessor()
  : ninfos(0)
{
  int i;

  for (i = 0; i < MAX_CONFIGS; i++) {
    infos[i] = NULL;
  }
}

unsigned int
ConfigProcessor::set(unsigned int id, ConfigInfo * info)
{
  ConfigInfo *old_info;
  int idx;

  if (id == 0) {
    id = ink_atomic_increment((int *) &ninfos, 1) + 1;
    ink_assert(id != 0);
    ink_assert(id <= MAX_CONFIGS);
  }

  info->m_refcount = 1;

  if (id > MAX_CONFIGS) {
    // invalid index
    Error("[ConfigProcessor::set] invalid index");
    return 0;
  }

  idx = id - 1;

  do {
    old_info = (ConfigInfo *) infos[idx];
  } while (!ink_atomic_cas( & infos[idx], old_info, info));

  if (old_info) {
    eventProcessor.schedule_in(NEW(new ConfigInfoReleaser(id, old_info)), HRTIME_SECONDS(60));
  }

  return id;
}

ConfigInfo *
ConfigProcessor::get(unsigned int id)
{
  ConfigInfo *info;
  int idx;

  ink_assert(id != 0);
  ink_assert(id <= MAX_CONFIGS);

  if (id == 0 || id > MAX_CONFIGS) {
    // return NULL, because we of an invalid index
    return NULL;
  }

  idx = id - 1;
  info = (ConfigInfo *) infos[idx];
  if (ink_atomic_increment((int *) &info->m_refcount, 1) < 0) {
    ink_assert(!"not reached");
  }

  return info;
}

void
ConfigProcessor::release(unsigned int id, ConfigInfo * info)
{
  int val;
  int idx;

  ink_assert(id != 0);
  ink_assert(id <= MAX_CONFIGS);

  if (id == 0 || id > MAX_CONFIGS) {
    // nothing to delete since we have an invalid index
    return;
  }

  idx = id - 1;
  val = ink_atomic_increment((int *) &info->m_refcount, -1);

  if ((infos[idx] != info) && (val == 1)) {
    delete info;
  }
}
