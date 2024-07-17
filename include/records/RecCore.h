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

#pragma once

#include <functional>

#include "tscore/Diags.h"

#include "records/RecDefs.h"
#include "swoc/MemSpan.h"

struct RecRecord;

//-------------------------------------------------------------------------
// Diagnostic Output
//-------------------------------------------------------------------------
int RecSetDiags(Diags *diags);

//-------------------------------------------------------------------------
// Config File Parsing
//-------------------------------------------------------------------------
using RecConfigEntryCallback = void (*)(RecT, RecDataT, const char *, const char *, RecSourceT);

void RecConfigFileInit();
int  RecConfigFileParse(const char *path, RecConfigEntryCallback handler);

// Return a copy of the system's configuration directory.
std::string RecConfigReadConfigDir();

// Return a copy of the system's local state directory, taking proxy.config.local_state_dir into account.
std::string RecConfigReadRuntimeDir();

// Return a copy of the system's log directory, taking proxy.config.log.logfile_dir into account.
std::string RecConfigReadLogDir();

// Return a copy of the system's bin directory, taking proxy.config.bin_path into account.
std::string RecConfigReadBinDir();

// Return a copy of the system's plugin directory, taking proxy.config.plugin.plugin_dir into account.
std::string RecConfigReadPluginDir();

// Return a copy of a configuration file that is relative to sysconfdir. The relative path to the configuration
// file is specified in the configuration variable named by "file_variable". If the configuration variable has no
// value, nullptr is returned.
std::string RecConfigReadConfigPath(const char *file_variable, const char *default_value = nullptr);

// Return a copy of the persistent stats file. This is $RUNTIMEDIR/records.snap.
std::string RecConfigReadPersistentStatsPath();

// Test whether the named configuration value is overridden by an environment variable. Return either
// the overridden value, or the original value. Caller MUST NOT free the result.
const char *RecConfigOverrideFromEnvironment(const char *name, const char *value);

//-------------------------------------------------------------------------
// Stat Registration
//-------------------------------------------------------------------------
RecErrT _RecRegisterStatInt(RecT rec_type, const char *name, RecInt data_default, RecPersistT persist_type);
#define RecRegisterStatInt(rec_type, name, data_default, persist_type) \
  _RecRegisterStatInt((rec_type), (name), (data_default), REC_PERSISTENCE_TYPE(persist_type))

RecErrT _RecRegisterStatFloat(RecT rec_type, const char *name, RecFloat data_default, RecPersistT persist_type);
#define RecRegisterStatFloat(rec_type, name, data_default, persist_type) \
  _RecRegisterStatFloat((rec_type), (name), (data_default), REC_PERSISTENCE_TYPE(persist_type))

RecErrT _RecRegisterStatString(RecT rec_type, const char *name, RecStringConst data_default, RecPersistT persist_type);
#define RecRegisterStatString(rec_type, name, data_default, persist_type) \
  _RecRegisterStatString((rec_type), (name), (data_default), REC_PERSISTENCE_TYPE(persist_type))

RecErrT _RecRegisterStatCounter(RecT rec_type, const char *name, RecCounter data_default, RecPersistT persist_type);
#define RecRegisterStatCounter(rec_type, name, data_default, persist_type) \
  _RecRegisterStatCounter((rec_type), (name), (data_default), REC_PERSISTENCE_TYPE(persist_type))

//-------------------------------------------------------------------------
// Config Registration
//-------------------------------------------------------------------------

RecErrT RecRegisterConfigInt(RecT rec_type, const char *name, RecInt data_default, RecUpdateT update_type, RecCheckT check_type,
                             const char *ccheck_regex, RecSourceT source, RecAccessT access_type = RECA_NULL);

RecErrT RecRegisterConfigFloat(RecT rec_type, const char *name, RecFloat data_default, RecUpdateT update_type, RecCheckT check_type,
                               const char *check_regex, RecSourceT source, RecAccessT access_type = RECA_NULL);

RecErrT RecRegisterConfigString(RecT rec_type, const char *name, const char *data_default, RecUpdateT update_type,
                                RecCheckT check_type, const char *check_regex, RecSourceT source,
                                RecAccessT access_type = RECA_NULL);

RecErrT RecRegisterConfigCounter(RecT rec_type, const char *name, RecCounter data_default, RecUpdateT update_type,
                                 RecCheckT check_type, const char *check_regex, RecSourceT source,
                                 RecAccessT access_type = RECA_NULL);

//-------------------------------------------------------------------------
// Config Change Notification
//-------------------------------------------------------------------------

RecErrT RecLinkConfigInt(const char *name, RecInt *rec_int);
RecErrT RecLinkConfigInt32(const char *name, int32_t *p_int32);
RecErrT RecLinkConfigUInt32(const char *name, uint32_t *p_uint32);
RecErrT RecLinkConfigFloat(const char *name, RecFloat *rec_float);
RecErrT RecLinkConfigCounter(const char *name, RecCounter *rec_counter);
RecErrT RecLinkConfigString(const char *name, RecString *rec_string);
RecErrT RecLinkConfigByte(const char *name, RecByte *rec_byte);

RecErrT RecRegisterConfigUpdateCb(const char *name, RecConfigUpdateCb const &update_cb, void *cookie);

/** Enable a dynamic configuration variable.
 *
 * @param name Configuration var name.
 * @param record_cb Callback to do the actual update of the master record.
 * @param config_cb Callback to invoke when the configuration variable is updated.
 * @param cookie Extra data for @a cb
 *
 * The purpose of this is to unite the different ways and times a configuration variable needs
 * to be loaded. These are
 * - Process start.
 * - Dynamic update.
 * - Plugin API update.
 *
 * @a record_cb is expected to perform the update. It must return a @c bool which is
 * - @c true if the value was changed.
 * - @c false if the value was not changed.
 *
 * Based on that, a run time configuration update is triggered or not.
 *
 * In addition, this invokes @a record_cb and passes it the information in the configuration variable
 * global table in order to perform the initial loading of the value. No update is triggered for
 * that call as it is not needed.
 */
void Enable_Config_Var(std::string_view const &name, RecContextCb record_cb, RecConfigUpdateCb const &config_cb, void *cookie);

//-------------------------------------------------------------------------
// Record Reading/Writing
//-------------------------------------------------------------------------

// WARNING!  Avoid deadlocks by calling the following set/get calls
// with the appropriate locking conventions.  If you're calling these
// functions from a configuration update callback (RecConfigUpdateCb),
// be sure to set 'lock' to 'false' as the hash-table rwlock has
// already been taken out for the callback.

RecErrT RecSetRecordInt(const char *name, RecInt rec_int, RecSourceT source, bool lock = true);
RecErrT RecSetRecordFloat(const char *name, RecFloat rec_float, RecSourceT source, bool lock = true);
RecErrT RecSetRecordString(const char *name, const RecString rec_string, RecSourceT source, bool lock = true);
RecErrT RecSetRecordCounter(const char *name, RecCounter rec_counter, RecSourceT source, bool lock = true);

RecErrT RecGetRecordInt(const char *name, RecInt *rec_int, bool lock = true);
RecErrT RecGetRecordFloat(const char *name, RecFloat *rec_float, bool lock = true);
RecErrT RecGetRecordString(const char *name, char *buf, int buf_len, bool lock = true);
RecErrT RecGetRecordString_Xmalloc(const char *name, RecString *rec_string, bool lock = true);
RecErrT RecGetRecordCounter(const char *name, RecCounter *rec_counter, bool lock = true);
// Convenience to allow us to treat the RecInt as a single byte internally
RecErrT RecGetRecordByte(const char *name, RecByte *rec_byte, bool lock = true);
// Convenience to allow us to treat the RecInt as a bool internally
RecErrT RecGetRecordBool(const char *name, RecBool *rec_byte, bool lock = true);

//------------------------------------------------------------------------
// Record Attributes Reading
//------------------------------------------------------------------------
using RecLookupCallback = void (*)(const RecRecord *, void *);

RecErrT RecLookupRecord(const char *name, RecLookupCallback callback, void *data, bool lock = true);
RecErrT RecLookupMatchingRecords(unsigned rec_type, const char *match, RecLookupCallback callback, void *data, bool lock = true);

RecErrT RecGetRecordType(const char *name, RecT *rec_type, bool lock = true);
RecErrT RecGetRecordDataType(const char *name, RecDataT *data_type, bool lock = true);
RecErrT RecGetRecordPersistenceType(const char *name, RecPersistT *persist_type, bool lock = true);
RecErrT RecGetRecordSource(const char *name, RecSourceT *source, bool lock = true);

/// Generate a warning if any configuration name/value is not registered.
void RecConfigWarnIfUnregistered();

//-------------------------------------------------------------------------
// Backwards Compatibility Items (REC_ prefix)
//-------------------------------------------------------------------------
#define REC_ReadConfigInt32(_var, _config_var_name)    \
  do {                                                 \
    RecInt tmp = 0;                                    \
    RecGetRecordInt(_config_var_name, (RecInt *)&tmp); \
    _var = (int32_t)tmp;                               \
  } while (0)

#define REC_ReadConfigInteger(_var, _config_var_name) \
  do {                                                \
    RecInt tmp = 0;                                   \
    RecGetRecordInt(_config_var_name, &tmp);          \
    _var = tmp;                                       \
  } while (0)

#define REC_ReadConfigFloat(_var, _config_var_name) \
  do {                                              \
    RecFloat tmp = 0;                               \
    RecGetRecordFloat(_config_var_name, &tmp);      \
    _var = tmp;                                     \
  } while (0)

#define REC_ReadConfigStringAlloc(_var, _config_var_name) RecGetRecordString_Xmalloc(_config_var_name, (RecString *)&_var)

#define REC_ReadConfigString(_var, _config_var_name, _len) RecGetRecordString(_config_var_name, _var, _len)

#define REC_RegisterConfigUpdateFunc(_config_var_name, func, flag) RecRegisterConfigUpdateCb(_config_var_name, func, flag)

#define REC_EstablishStaticConfigInteger(_var, _config_var_name) \
  do {                                                           \
    RecLinkConfigInt(_config_var_name, &_var);                   \
    _var = (int64_t)REC_ConfigReadInteger(_config_var_name);     \
  } while (0)

#define REC_EstablishStaticConfigInt32(_var, _config_var_name) \
  do {                                                         \
    RecLinkConfigInt32(_config_var_name, &_var);               \
    _var = (int32_t)REC_ConfigReadInteger(_config_var_name);   \
  } while (0)

#define REC_EstablishStaticConfigInt32U(_var, _config_var_name) \
  do {                                                          \
    RecLinkConfigUInt32(_config_var_name, &_var);               \
    _var = (int32_t)REC_ConfigReadInteger(_config_var_name);    \
  } while (0)

/*
 * RecLinkConfigString allocates the RecString and stores the ptr to it (&var).
 * So before changing _var (the RecString) we have to free the original one.
 * Really, we somehow need to know whether RecLinkConfigString allocated _var.
 * For now, we're using the return value to indicate this, even though it's
 * not always the case.  If we're wrong, we'll leak the RecString.
 */
#define REC_EstablishStaticConfigStringAlloc(_var, _config_var_name)  \
  do {                                                                \
    if (RecLinkConfigString(_config_var_name, &_var) == REC_ERR_OKAY) \
      ats_free(_var);                                                 \
    _var = (RecString)REC_ConfigReadString(_config_var_name);         \
  } while (0)

#define REC_EstablishStaticConfigFloat(_var, _config_var_name) \
  do {                                                         \
    RecLinkConfigFloat(_config_var_name, &_var);               \
    _var = (RecFloat)REC_ConfigReadFloat(_config_var_name);    \
  } while (0)

// Allow to treat our "INT" configs as a byte type internally. Note
// that the byte type is just a wrapper around RECD_INT.
#define REC_EstablishStaticConfigByte(_var, _config_var_name) \
  do {                                                        \
    RecLinkConfigByte(_config_var_name, &_var);               \
    _var = (RecByte)REC_ConfigReadInteger(_config_var_name);  \
  } while (0)

RecInt   REC_ConfigReadInteger(const char *name);
char    *REC_ConfigReadString(const char *name);
RecFloat REC_ConfigReadFloat(const char *name);

// MGMT2 Marco's -- converting lmgmt->record_data->readXXX
RecInt     REC_readInteger(const char *name, bool *found, bool lock = true);
RecFloat   REC_readFloat(char *name, bool *found, bool lock = true);
RecCounter REC_readCounter(char *name, bool *found, bool lock = true);
RecString  REC_readString(const char *name, bool *found, bool lock = true);

//------------------------------------------------------------------------
// Set RecRecord attributes
//------------------------------------------------------------------------
RecErrT RecSetSyncRequired(char *name, bool lock = true);
