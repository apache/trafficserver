/*
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

#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/ink_defs.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>

static const char PLUGIN_NAME[] = "conf_remap";

// This makes the plugin depend on the version of traffic server installed, but that's
// OK, since this plugin is distributed only with the "core" (it's a core piece).
#define MAX_OVERRIDABLE_CONFIGS TS_CONFIG_LAST_ENTRY

// Class to hold a set of configurations (one for each remap rule instance)
struct RemapConfigs {
  struct Item {
    TSOverridableConfigKey _name;
    TSRecordDataType _type;
    TSRecordData _data;
    int _data_len; // Used when data is a string
  };

  RemapConfigs() : _current(0) { memset(_items, 0, sizeof(_items)); };
  bool parse_file(const char *filename);
  bool parse_inline(const char *arg);

  Item _items[MAX_OVERRIDABLE_CONFIGS];
  int _current;
};

// Helper functionfor the parser
inline TSRecordDataType
str_to_datatype(const char *str)
{
  TSRecordDataType type = TS_RECORDDATATYPE_NULL;

  if (!str || !*str) {
    return TS_RECORDDATATYPE_NULL;
  }

  if (!strcmp(str, "INT")) {
    type = TS_RECORDDATATYPE_INT;
  } else if (!strcmp(str, "STRING")) {
    type = TS_RECORDDATATYPE_STRING;
  }

  return type;
}

// Parse an inline key=value config pair.
bool
RemapConfigs::parse_inline(const char *arg)
{
  const char *sep;
  std::string key;
  std::string value;

  TSOverridableConfigKey name;
  TSRecordDataType type;

  // Each token should be a status code then a URL, separated by '='.
  sep = strchr(arg, '=');
  if (sep == nullptr) {
    return false;
  }

  key   = std::string(arg, std::distance(arg, sep));
  value = std::string(sep + 1, std::distance(sep + 1, arg + strlen(arg)));

  if (TSHttpTxnConfigFind(key.c_str(), -1 /* len */, &name, &type) != TS_SUCCESS) {
    TSError("[%s] Invalid configuration variable '%s'", PLUGIN_NAME, key.c_str());
    return false;
  }

  switch (type) {
  case TS_RECORDDATATYPE_INT:
    _items[_current]._data.rec_int = strtoll(value.c_str(), nullptr, 10);
    break;
  case TS_RECORDDATATYPE_STRING:
    if (strcmp(value.c_str(), "NULL") == 0) {
      _items[_current]._data.rec_string = nullptr;
      _items[_current]._data_len        = 0;
    } else {
      _items[_current]._data.rec_string = TSstrdup(value.c_str());
      _items[_current]._data_len        = value.size();
    }
    break;
  default:
    TSError("[%s] Configuration variable '%s' is of an unsupported type", PLUGIN_NAME, key.c_str());
    return false;
  }

  _items[_current]._name = name;
  _items[_current]._type = type;
  ++_current;
  return true;
}

// Config file parser, somewhat borrowed from P_RecCore.i
bool
RemapConfigs::parse_file(const char *filename)
{
  int line_num = 0;
  TSFile file;
  char buf[8192];
  TSOverridableConfigKey name;
  TSRecordDataType type, expected_type;

  std::string path;

  if (!filename || ('\0' == *filename)) {
    return false;
  }

  if (*filename == '/') {
    // Absolute path, just use it.
    path = filename;
  } else {
    // Relative path. Make it relative to the configuration directory.
    path = TSConfigDirGet();
    path += "/";
    path += filename;
  }

  if (nullptr == (file = TSfopen(path.c_str(), "r"))) {
    TSError("[%s] Could not open config file %s", PLUGIN_NAME, path.c_str());
    return false;
  }

  TSDebug(PLUGIN_NAME, "loading configuration file %s", path.c_str());

  while (nullptr != TSfgets(file, buf, sizeof(buf))) {
    char *ln, *tok;
    char *s = buf;

    ++line_num; // First line is #1 ...
    while (isspace(*s)) {
      ++s;
    }
    tok = strtok_r(s, " \t", &ln);

    // check for blank lines and comments
    if ((!tok) || (tok && ('#' == *tok))) {
      continue;
    }

    if (strncmp(tok, "CONFIG", 6)) {
      TSError("[%s] File %s, line %d: non-CONFIG line encountered", PLUGIN_NAME, path.c_str(), line_num);
      continue;
    }

    // Find the configuration name
    tok = strtok_r(nullptr, " \t", &ln);
    if (TSHttpTxnConfigFind(tok, -1, &name, &expected_type) != TS_SUCCESS) {
      TSError("[%s] File %s, line %d: %s is not a configuration variable or cannot be overridden", PLUGIN_NAME, path.c_str(),
              line_num, tok);
      continue;
    }

    // Find the type (INT or STRING only)
    tok = strtok_r(nullptr, " \t", &ln);
    if (TS_RECORDDATATYPE_NULL == (type = str_to_datatype(tok))) {
      TSError("[%s] file %s, line %d: only INT and STRING types supported", PLUGIN_NAME, path.c_str(), line_num);
      continue;
    }

    if (type != expected_type) {
      TSError("[%s] file %s, line %d: mismatch between provide data type, and expected type", PLUGIN_NAME, path.c_str(), line_num);
      continue;
    }

    // Find the value (which depends on the type above)
    if (ln) {
      while (isspace(*ln)) {
        ++ln;
      }
      if ('\0' == *ln) {
        tok = nullptr;
      } else {
        tok = ln;
        while (*ln != '\0') {
          ++ln;
        }
        --ln;
        while (isspace(*ln) && (ln > tok)) {
          --ln;
        }
        ++ln;
        *ln = '\0';
      }
    } else {
      tok = nullptr;
    }
    if (!tok) {
      TSError("[%s] file %s, line %d: the configuration must provide a value", PLUGIN_NAME, path.c_str(), line_num);
      continue;
    }

    // Now store the new config
    switch (type) {
    case TS_RECORDDATATYPE_INT:
      _items[_current]._data.rec_int = strtoll(tok, nullptr, 10);
      break;
    case TS_RECORDDATATYPE_STRING:
      if (strcmp(tok, "NULL") == 0) {
        _items[_current]._data.rec_string = nullptr;
        _items[_current]._data_len        = 0;
      } else {
        _items[_current]._data.rec_string = TSstrdup(tok);
        _items[_current]._data_len        = strlen(tok);
      }
      break;
    default:
      TSError("[%s] file %s, line %d: type not support (unheard of)", PLUGIN_NAME, path.c_str(), line_num);
      continue;
      break;
    }
    _items[_current]._name = name;
    _items[_current]._type = type;
    ++_current;
  }

  TSfclose(file);
  return (_current > 0);
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    TSstrlcpy(errbuf, "[TSRemapInit] - Invalid TSRemapInterface argument", errbuf_size);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    TSstrlcpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size);
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "remap plugin is successfully initialized");
  return TS_SUCCESS; /* success */
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  if (argc < 3) {
    TSError("[%s] Unable to create remap instance, need configuration file", PLUGIN_NAME);
    return TS_ERROR;
  }

  RemapConfigs *conf = new (RemapConfigs);
  for (int i = 2; i < argc; ++i) {
    if (strchr(argv[i], '=') != nullptr) {
      // Parse as an inline key=value pair ...
      if (!conf->parse_inline(argv[i])) {
        goto fail;
      }
    } else {
      // Parse as a config file ...
      if (!conf->parse_file(argv[i])) {
        goto fail;
      }
    }
  }

  *ih = static_cast<void *>(conf);
  return TS_SUCCESS;

fail:
  delete conf;
  return TS_ERROR;
}

void
TSRemapDeleteInstance(void *ih)
{
  RemapConfigs *conf = static_cast<RemapConfigs *>(ih);

  for (int ix = 0; ix < conf->_current; ++ix) {
    if (TS_RECORDDATATYPE_STRING == conf->_items[ix]._type) {
      TSfree(conf->_items[ix]._data.rec_string);
    }
  }

  delete conf;
}

///////////////////////////////////////////////////////////////////////////////
// Main entry point when used as a remap plugin.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo * /* rri ATS_UNUSED */)
{
  if (nullptr != ih) {
    RemapConfigs *conf = static_cast<RemapConfigs *>(ih);
    TSHttpTxn txnp     = static_cast<TSHttpTxn>(rh);

    for (int ix = 0; ix < conf->_current; ++ix) {
      switch (conf->_items[ix]._type) {
      case TS_RECORDDATATYPE_INT:
        TSHttpTxnConfigIntSet(txnp, conf->_items[ix]._name, conf->_items[ix]._data.rec_int);
        TSDebug(PLUGIN_NAME, "Setting config id %d to %" PRId64 "", conf->_items[ix]._name, conf->_items[ix]._data.rec_int);
        break;
      case TS_RECORDDATATYPE_STRING:
        TSHttpTxnConfigStringSet(txnp, conf->_items[ix]._name, conf->_items[ix]._data.rec_string, conf->_items[ix]._data_len);
        TSDebug(PLUGIN_NAME, "Setting config id %d to %s", conf->_items[ix]._name, conf->_items[ix]._data.rec_string);
        break;
      default:
        break; // Error ?
      }
    }
  }

  return TSREMAP_NO_REMAP; // This plugin never rewrites anything.
}
