/** @file

  Public RecCore declarations

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

#ifndef _I_REC_CORE_H_
#define _I_REC_CORE_H_

#include "ink_bool.h"
#include "Diags.h"

#include "I_RecDefs.h"
#include "I_RecAlarms.h"
#include "I_RecSignals.h"
#include "I_RecEvents.h"

//-------------------------------------------------------------------------
// Diagnostic Output
//-------------------------------------------------------------------------

int RecSetDiags(Diags * diags);

//-------------------------------------------------------------------------
// Stat Registration
//-------------------------------------------------------------------------

int RecRegisterStatInt(RecT rec_type, char *name, RecInt data_default, RecPersistT persist_type);

int RecRegisterStatLLong(RecT rec_type, char *name, RecLLong data_default, RecPersistT persist_type);

int RecRegisterStatFloat(RecT rec_type, char *name, RecFloat data_default, RecPersistT persist_type);

int RecRegisterStatString(RecT rec_type, char *name, RecString data_default, RecPersistT persist_type);

int RecRegisterStatCounter(RecT rec_type, char *name, RecCounter data_default, RecPersistT persist_type);

//-------------------------------------------------------------------------
// Config Registration
//-------------------------------------------------------------------------

int RecRegisterConfigInt(RecT rec_type, char *name,
                         RecInt data_default, RecUpdateT update_type,
                         RecCheckT check_type, char *check_regex, RecAccessT access_type = RECA_NULL);

int RecRegisterConfigLLong(RecT rec_type, char *name,
                           RecLLong data_default, RecUpdateT update_type,
                           RecCheckT check_type, char *check_regex, RecAccessT access_type = RECA_NULL);

int RecRegisterConfigFloat(RecT rec_type, char *name,
                           RecFloat data_default, RecUpdateT update_type,
                           RecCheckT check_type, char *check_regex, RecAccessT access_type = RECA_NULL);

int RecRegisterConfigString(RecT rec_type, char *name,
                            RecString data_default, RecUpdateT update_type,
                            RecCheckT check_type, char *check_regex, RecAccessT access_type = RECA_NULL);

int RecRegisterConfigCounter(RecT rec_type, char *name,
                             RecCounter data_default, RecUpdateT update_type,
                             RecCheckT check_type, char *check_regex, RecAccessT access_type = RECA_NULL);

//-------------------------------------------------------------------------
// Config Change Notification
//-------------------------------------------------------------------------

int RecLinkConfigInt(char *name, RecInt * rec_int);
int RecLinkConfigLLong(char *name, RecLLong * rec_llong);
int RecLinkConfigInk32(char *name, ink32 * p_ink32);
int RecLinkConfigInkU32(char *name, inku32 * p_inku32);
int RecLinkConfigFloat(char *name, RecFloat * rec_float);
int RecLinkConfigCounter(char *name, RecCounter * rec_counter);
int RecLinkConfigString(char *name, RecString * rec_string);

int RecRegisterConfigUpdateCb(char *name, RecConfigUpdateCb update_cb, void *cookie);
int RecRegisterStatUpdateFunc(char *name, RecStatUpdateFunc update_func, void *cookie);
int RecRegisterRawStatUpdateFunc(char *name, RecRawStatBlock * rsb,
                                 int id, RecStatUpdateFunc update_func, void *cookie);

//-------------------------------------------------------------------------
// Record Reading/Writing
//-------------------------------------------------------------------------

// WARNING!  Avoid deadlocks by calling the following set/get calls
// with the appropiate locking conventions.  If you're calling these
// functions from a configuration update callback (RecConfigUpdateCb),
// be sure to set 'lock' to 'false' as the hash-table rwlock has
// already been taken out for the callback.

// RecSetRecordConvert -> WebMgmtUtils.cc::varSetFromStr()
int RecSetRecordConvert(char *name, RecString rec_string, bool lock = true);
int RecSetRecordInt(char *name, RecInt rec_int, bool lock = true);
int RecSetRecordLLong(char *name, RecLLong rec_llong, bool lock = true);
int RecSetRecordFloat(char *name, RecFloat rec_float, bool lock = true);
int RecSetRecordString(char *name, RecString rec_string, bool lock = true);
int RecSetRecordCounter(char *name, RecCounter rec_counter, bool lock = true);

int RecGetRecordInt(char *name, RecInt * rec_int, bool lock = true);
int RecGetRecordLLong(char *name, RecLLong * rec_llong, bool lock = true);
int RecGetRecordFloat(char *name, RecFloat * rec_float, bool lock = true);
int RecGetRecordString(char *name, char *buf, int buf_len, bool lock = true);
int RecGetRecordString_Xmalloc(char *name, RecString * rec_string, bool lock = true);
int RecGetRecordCounter(char *name, RecCounter * rec_counter, bool lock = true);
int RecGetRecordGeneric_Xmalloc(char *name, RecString * rec_string, bool lock = true);

//------------------------------------------------------------------------
// Record Attributes Reading
//------------------------------------------------------------------------
int RecGetRecordType(char *name, RecT * rec_type, bool lock = true);
int RecGetRecordDataType(char *name, RecDataT * data_type, bool lock = true);
int RecGetRecordUpdateCount(RecT data_type);
int RecGetRecordRelativeOrder(char *name, int *order, bool lock = true);

int RecGetRecordUpdateType(char *name, RecUpdateT * update_type, bool lock = true);
int RecGetRecordCheckType(char *name, RecCheckT * check_type, bool lock = true);
int RecGetRecordCheckExpr(char *name, char **check_expr, bool lock = true);
int RecGetRecordDefaultDataString_Xmalloc(char *name, char **buf, bool lock = true);

int RecGetRecordAccessType(char *name, RecAccessT * secure, bool lock = true);
int RecSetRecordAccessType(char *name, RecAccessT secure, bool lock = true);

void RecGetRecordTree(char *subtree = NULL);
void RecGetRecordList(char *, char ***, int *);
int RecGetRecordPrefix_Xmalloc(char *prefix, char **result, int *result_len);

//------------------------------------------------------------------------
// Signal and Alarms
//------------------------------------------------------------------------
void RecSignalManager(int, char *);
void RecSignalAlarm(int, char *);

//-------------------------------------------------------------------------
// Backwards Compatibility Items (REC_ prefix)
//-------------------------------------------------------------------------

#define REC_RegisterConfigInteger RecRegisterConfigInt
#define REC_RegisterConfigLLong   RecRegisterConfigLLong
#define REC_RegisterConfigString  RecRegisterConfigString

#define REC_ReadConfigInt32(_var,_config_var_name) do { \
  RecInt tmp = 0; \
  RecGetRecordInt(_config_var_name, (RecInt*) &tmp); \
  _var = (ink32)tmp; \
} while (0)

#define REC_ReadConfigInteger(_var,_config_var_name) do { \
  RecInt tmp = 0; \
  RecGetRecordInt(_config_var_name, &tmp); \
  _var = tmp; \
} while (0)

#define REC_ReadConfigLLong(_var,_config_var_name) do { \
  RecLLong tmp = 0; \
  RecGetRecordLLong(_config_var_name, &tmp); \
  _var = tmp; \
} while (0)

#define REC_ReadConfigFloat(_var,_config_var_name) do { \
  RecFloat tmp = 0; \
  RecGetRecordFloat(_config_var_name, &tmp); \
  _var = tmp; \
} while (0)

#define REC_ReadConfigStringAlloc(_var,_config_var_name) \
  RecGetRecordString_Xmalloc(_config_var_name, (RecString*)&_var)

#define REC_ReadConfigString(_var, _config_var_name, _len) \
  RecGetRecordString(_config_var_name, _var, _len)

#define REC_RegisterConfigUpdateFunc(_config_var_name, func, flag) \
  RecRegisterConfigUpdateCb(_config_var_name, func, flag)

#define REC_EstablishStaticConfigInteger(_var, _config_var_name) do{ \
  RecLinkConfigInt(_config_var_name, &_var); \
  _var = (int)REC_ConfigReadInteger(_config_var_name); \
} while (0)

#define REC_EstablishStaticConfigLLong(_var, _config_var_name) do{ \
  RecLinkConfigLLong(_config_var_name, &_var); \
  _var = (RecLLong)REC_ConfigReadLLong(_config_var_name); \
} while (0)

#define REC_EstablishStaticConfigInt32(_var, _config_var_name) do { \
  RecLinkConfigInk32(_config_var_name, &_var); \
  _var = (ink32)REC_ConfigReadInteger(_config_var_name); \
} while (0)

#define REC_EstablishStaticConfigInt32U(_var, _config_var_name) do { \
  RecLinkConfigInkU32(_config_var_name, &_var); \
  _var = (ink32)REC_ConfigReadInteger(_config_var_name); \
} while (0)

#define REC_EstablishStaticConfigStringAlloc(_var, _config_var_name) do { \
  RecLinkConfigString(_config_var_name, &_var); \
  _var = (RecString)REC_ConfigReadString(_config_var_name); \
} while (0)

#define REC_EstablishStaticConfigLongLong(_var, _config_var_name) do { \
  RecLinkConfigCounter(_config_var_name, &_var); \
  _var = (RecCounter)REC_ConfigReadCounter(_config_var_name); \
} while (0)

#define REC_EstablishStaticConfigFloat(_var, _config_var_name) do { \
  RecLinkConfigFloat(_config_var_name, &_var); \
  _var = (RecFloat)REC_ConfigReadFloat(_config_var_name); \
} while (0)

RecInt REC_ConfigReadInteger(char *name);
RecLLong REC_ConfigReadLLong(char *name);
char *REC_ConfigReadString(char *name);
RecFloat REC_ConfigReadFloat(char *name);
RecCounter REC_ConfigReadCounter(char *name);

// MGMT2 Marco's -- converting lmgmt->record_data->readXXX
RecInt REC_readInteger(char *name, bool * found, bool lock = true);
RecLLong REC_readLLong(char *name, bool * found, bool lock = true);
RecFloat REC_readFloat(char *name, bool * found, bool lock = true);
RecCounter REC_readCounter(char *name, bool * found, bool lock = true);
RecString REC_readString(char *name, bool * found, bool lock = true);

bool REC_setInteger(char *name, int value, bool dirty = true);
bool REC_setLLong(char *name, RecLLong value, bool dirty = true);
bool REC_setFloat(char *name, float value, bool dirty = true);
bool REC_setCounter(char *name, ink64 value, bool dirty = true);
bool REC_setString(char *name, char *value, bool dirty = true);

//------------------------------------------------------------------------
// Clear Statistics
//------------------------------------------------------------------------
int RecResetStatRecord(char *name);
int RecResetStatRecord(RecT type = RECT_NULL);

//------------------------------------------------------------------------
// Set RecRecord attributes 
//------------------------------------------------------------------------
int RecSetSyncRequired(char *name, bool lock = true);

//------------------------------------------------------------------------
// Signal Alarm/Warning
//------------------------------------------------------------------------
#define REC_SignalManager        RecSignalManager
#define REC_SignalAlarm          RecSignalAlarm
#define REC_SignalWarning(_n,_d) { Warning(_d); RecSignalManager(_n,_d); }

//------------------------------------------------------------------------
// Manager Callback
//------------------------------------------------------------------------
typedef void *(*RecManagerCb) (void *opaque_cb_data, char *data_raw, int data_len);
int RecRegisterManagerCb(int _signal, RecManagerCb _fn, void *_data = NULL);

#endif
