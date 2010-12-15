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
// Syntactic sugar for management record access
//
#define LIBRECORDS_WARN \
Warning("** MUST MIGRATE TO LIBRECORDS ** file:%s line:%d\n", __FILE__, __LINE__)

#define ConfigReadCounter(_s) pmgmt->record_data->readConfigCounter(_s); LIBRECORDS_WARN
#define ConfigReadInteger(_s) pmgmt->record_data->readConfigInteger(_s); LIBRECORDS_WARN
#define ConfigReadFloat(_s)   pmgmt->record_data->readConfigFloat(_s);   LIBRECORDS_WARN
#define ConfigReadString(_s)  pmgmt->record_data->readConfigString(_s);  LIBRECORDS_WARN
#define LocalReadInteger(_s)  pmgmt->record_data->readLocalInteger(_s);  LIBRECORDS_WARN

//
// Macros that spin waiting for the data to be bound
//

#define ReadConfigStringAlloc(_s,_sc) do { \
  LIBRECORDS_WARN; \
  bool _found = 0; int _len = -1; \
  char * _ss = pmgmt->record_data->readConfigString(_sc, &_found); \
  if (_found) { \
    if (_ss) { _len = strlen(_ss); \
               _s = (char *)xmalloc(_len+1); \
               memcpy(_s, _ss, _len+1); xfree(_ss); } \
  } else { Warning("Cannot read config %s",_sc); } \
} while(0)

#define ReadConfigString(_s,_sc,_len) do { \
  LIBRECORDS_WARN; \
  bool _found = 0; \
  char * _ss = pmgmt->record_data->readConfigString(_sc, &_found); \
  if (_found && _ss) { strncpy(_s,_ss,_len);  xfree(_ss); } \
  else Warning("Cannot read config %s",_sc); \
} while(0)

#define ReadConfigInteger(_i,_s) do { \
  LIBRECORDS_WARN; \
  bool _found = 0; \
  int64_t _ii = pmgmt->record_data->readConfigInteger(_s, &_found); \
  if (_found) _i = _ii; else Warning("Cannot read config %s",_s); \
} while(0)

#define ReadConfigInt32(_i,_s) do { \
  LIBRECORDS_WARN; \
  bool _found = 0; \
  int64_t _ii = pmgmt->record_data->readConfigInteger(_s, &_found); \
  if (_found) _i = (int32_t)_ii; else Warning("Cannot read config %s",_s); \
} while(0)

#define ReadConfigInt32U(_i,_s) do { \
  LIBRECORDS_WARN; \
  bool _found = 0; \
  int64_t _ii = pmgmt->record_data->readConfigInteger(_s, &_found); \
  if (_found) _i = (uint32_t)_ii; else Warning("Cannot read config %s",_s); \
} while(0)

#define ReadConfigFloat(_f,_s) do { \
  LIBRECORDS_WARN; \
  bool _found = 0; \
  float _ff = pmgmt->record_data->readConfigFloat(_s, &_found); \
  if (_found) _f = _ff; else Warning("Cannot read config %s",_s); \
} while(0)

#define ReadLocalInteger(_i,_s) do { \
  LIBRECORDS_WARN; \
  bool _found = 0; \
  int64_t _ii = pmgmt->record_data->readLocalInteger(_s, &_found); \
  if (_found) _i = _ii; else Warning("Cannot read config %s",_s); \
} while(0)

//
// Syntactic sugar for management record access
//
#ifdef DEBUG
#define RegisterStatUpdateFunc(_n,_cb,_odata) \
do { \
  LIBRECORDS_WARN; \
  ink_assert(pmgmt->record_data->registerStatUpdateFunc(_n,_cb,_odata)); \
} while(0)

#define RegisterConfigUpdateFunc(_n,_cb,_odata) \
do { \
  LIBRECORDS_WARN; \
  ink_assert(pmgmt->record_data->registerConfigUpdateFunc(_n,_cb,_odata)); \
} while(0)

#define RegisterConfigUpdateFuncInteger(_i,_n) \
do { \
  LIBRECORDS_WARN; \
   int* tmp_ptr = &_i; \
  ink_assert(pmgmt->record_data->registerConfigUpdateFunc(_n,config_int_cb, (void*)tmp_ptr)); \
} while(0)

#define RegisterConfigUpdateFuncIntegerU(_i,_n) \
do { \
  LIBRECORDS_WARN; \
  unsigned* tmp_ptr = &_i; \
  ink_assert(pmgmt->record_data->registerConfigUpdateFunc(_n,config_int_cb, (void*)tmp_ptr)); \
} while(0)

#define RegisterConfigUpdateFuncFloat(_i,_n) \
do { \
  LIBRECORDS_WARN; \
  ink_assert(pmgmt->record_data->registerConfigUpdateFunc(_n,config_float_cb, (void*)&_i));\
} while(0)

#define RegisterConfigUpdateFuncLongLong(_ll,_n) \
do { \
  LIBRECORDS_WARN; \
  int64_t* tmp_ptr = &_ll; \
  ink_assert(pmgmt->record_data->registerConfigUpdateFunc(_n,config_long_long_cb, (void*)tmp_ptr));\
} while(0)

#define RegisterConfigUpdateFuncInt64(_ll,_n) \
RegisterConfigUpdateFuncLongLong(_ll,_n)

#define RegisterConfigUpdateFuncStringAlloc(_ll,_n) \
do { \
  LIBRECORDS_WARN; \
  ink_assert(pmgmt->record_data->registerConfigUpdateFunc(_n,config_string_alloc_cb, (void*)&_ll)); \
} while(0)

#define RegisterLocalUpdateFunc(_n,_cb,_odata) \
do { \
  LIBRECORDS_WARN; \
  ink_assert(pmgmt->record_data->registerLocalUpdateFunc(_n,_cb,_odata)); \
} while (0)

#define RegisterLocalUpdateFuncInteger(_i,_n) \
do { \
  LIBRECORDS_WARN; \
  int* tmp_ptr = &_i; \
  ink_assert(pmgmt->record_data->registerLocalUpdateFunc(_n,config_int_cb, (void*)tmp_ptr)); \
} while(0)

#else

#define RegisterStatUpdateFunc(_n,_cb,_odata) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerStatUpdateFunc(_n,_cb,_odata); \
} while(0)

#define RegisterConfigUpdateFunc(_n,_cb,_odata) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerConfigUpdateFunc(_n,_cb,_odata); \
} while(0)

#define RegisterConfigUpdateFuncInteger(_i,_n) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerConfigUpdateFunc(_n,config_int_cb, (void*)&_i); \
} while(0)

#define RegisterConfigUpdateFuncIntegerU(_i,_n) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerConfigUpdateFunc(_n,config_int_cb, (void*)&_i); \
} while(0)

#define RegisterConfigUpdateFuncFloat(_i,_n) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerConfigUpdateFunc(_n,config_float_cb, (void*)&_i); \
} while(0)

#define RegisterConfigUpdateFuncLongLong(_ll,_n) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerConfigUpdateFunc(_n,config_long_long_cb, (void*)&_ll); \
} while(0)

#define RegisterConfigUpdateFuncInt64(_ll,_n) \
RegisterConfigUpdateFuncLongLong(_ll,_n)

#define RegisterConfigUpdateFuncStringAlloc(_ll,_n) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerConfigUpdateFunc(_n,config_string_alloc_cb, (void*)&_ll); \
} while(0)

#define RegisterLocalUpdateFunc(_n,_cb,_odata) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerLocalUpdateFunc(_n,_cb,_odata); \
} while(0)

#define RegisterLocalUpdateFuncInteger(_i,_n) \
do { \
  LIBRECORDS_WARN; \
  pmgmt->record_data->registerLocalUpdateFunc(_n,config_int_cb, (void*)&_i); \
} while(0)

#endif

#define EstablishStaticConfigInteger(_ix,_n) \
ReadConfigInteger(_ix,_n); \
RegisterConfigUpdateFuncInteger(_ix,_n)

#define EstablishStaticConfigInt32(_ix,_n) \
ReadConfigInt32(_ix,_n); \
RegisterConfigUpdateFuncInteger(_ix,_n)

#define EstablishStaticConfigIntegerU(_ix,_n) \
ReadConfigInteger(_ix,_n); \
RegisterConfigUpdateFuncIntegerU(_ix,_n)

#define EstablishStaticConfigInt32U(_ix,_n) \
ReadConfigInt32U(_ix,_n); \
RegisterConfigUpdateFuncIntegerU(_ix,_n)

#define EstablishStaticConfigFloat(_ix,_n) \
ReadConfigFloat(_ix,_n); \
RegisterConfigUpdateFuncFloat(_ix,_n)

#define EstablishStaticConfigLongLong(_ix,_n) \
ReadConfigInteger(_ix,_n); \
RegisterConfigUpdateFuncLongLong(_ix,_n)

#define EstablishStaticConfigInt64(_ix,_n) \
ReadConfigInteger(_ix,_n); \
RegisterConfigUpdateFuncInt64(_ix,_n)

#define EstablishStaticConfigStringAlloc(_ix,_n) \
ReadConfigStringAlloc(_ix,_n); \
RegisterConfigUpdateFuncStringAlloc(_ix,_n)

#define EstablishStaticLocalInteger(_ix,_n) \
ReadLocalInteger(_ix,_n); \
RegisterLocalUpdateFuncInteger(_ix,_n)

#define RegisterStatCountSum(_n) \
  RegisterStatUpdateFunc( "proxy.process." #_n ".sum", stats_sum_cb, \
                          (void*)_n##_Stat); \
  RegisterStatUpdateFunc( "proxy.process." #_n ".count", stats_count_cb, \
                          (void*)_n##_Stat)

#define RegisterStatCountFSum(_n) \
  RegisterStatUpdateFunc( "proxy.process." #_n ".sum", stats_fsum_cb, \
                          (void*)_n##_Stat); \
  RegisterStatUpdateFunc( "proxy.process." #_n ".count", stats_count_cb, \
                          (void*)_n##_Stat)

#define RegisterStatSum(_n) \
  RegisterStatUpdateFunc( "proxy.process." #_n, stats_sum_cb, \
                          (void*)_n##_Stat)

#define RegisterStatCount(_n) \
  RegisterStatUpdateFunc( "proxy.process." #_n, stats_count_cb, \
                          (void*)_n##_Stat)

#define RegisterStatAvg(_n) \
  RegisterStatUpdateFunc( "proxy.process." #_n, stats_avg_cb, \
                          (void*)_n##_Stat)

#define SignalManager(_n,_d) pmgmt->signalManager(_n,(char*)_d)
#define SignalWarning(_n,_s) { Warning(_s); SignalManager(_n,_s); }

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
