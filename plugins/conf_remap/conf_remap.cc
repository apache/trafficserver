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
#include "tscore/ink_defs.h"
#include "tscpp/util/YamlCfg.h"
#include <swoc/bwf_base.h>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>

namespace
{
const char PLUGIN_NAME[] = "conf_remap";

DbgCtl dbg_ctl{PLUGIN_NAME};
} // namespace

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

  RemapConfigs() { memset(_items, 0, sizeof(_items)); };
  bool parse_file(const char *filename);
  bool parse_inline(const char *arg);

  Item _items[MAX_OVERRIDABLE_CONFIGS];
  int _current = 0;
};

// Helper function for the parser
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
  } else if (!strcmp(str, "FLOAT")) {
    type = TS_RECORDDATATYPE_FLOAT;
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

  // Each token should be a configuration variable then a value, separated by '='.
  sep = strchr(arg, '=');
  if (sep == nullptr) {
    return false;
  }

  key   = std::string(arg, std::distance(arg, sep));
  value = std::string(sep + 1, std::distance(sep + 1, arg + strlen(arg)));

  if (TSHttpTxnConfigFind(key.c_str(), -1 /* len */, &name, &type) != TS_SUCCESS) {
    TSWarning("[%s] Invalid configuration variable '%s'", PLUGIN_NAME, key.c_str());
    return true;
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
  case TS_RECORDDATATYPE_FLOAT:
    _items[_current]._data.rec_float = strtof(value.c_str(), nullptr);
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

namespace
{
TSRecordDataType
try_deduce_type(YAML::Node node)
{
  std::string_view tag = node.Tag();
  if (tag == ts::Yaml::YAML_FLOAT_TAG_URI) {
    return TS_RECORDDATATYPE_FLOAT;
  } else if (tag == ts::Yaml::YAML_INT_TAG_URI) {
    return TS_RECORDDATATYPE_INT;
  } else if (tag == ts::Yaml::YAML_STR_TAG_URI) {
    return TS_RECORDDATATYPE_STRING;
  }
  // we only care about string, int and float.
  return TS_RECORDDATATYPE_NULL;
}
/// @brief Context class used to pass information to the TSYAMLRecNodeHandler as the data obj.
struct Context {
  RemapConfigs::Item *items;
  int *current;
};

TSReturnCode
scalar_node_handler(const TSYAMLRecCfgFieldData *cfg, void *data)
{
  TSOverridableConfigKey name;
  TSRecordDataType expected_type;
  std::string text;

  auto &ctx        = *static_cast<Context *>(data);
  YAML::Node value = *reinterpret_cast<YAML::Node *>(cfg->value_node);

  if (TSHttpTxnConfigFind(cfg->record_name, strlen(cfg->record_name), &name, &expected_type) != TS_SUCCESS) {
    TSError("[%s] '%s' is not a configuration variable or cannot be overridden", PLUGIN_NAME, cfg->record_name);
    return TS_ERROR;
  }

  RemapConfigs::Item *item = &ctx.items[*ctx.current];

  auto type = try_deduce_type(value);
  Dbg(dbg_ctl, "### deduced type %d for %s", type, cfg->record_name);
  // If we detected a type but it's different from the one registered with the in ATS, then we ignore it.
  if (type != TS_RECORDDATATYPE_NULL && expected_type != type) {
    TSError("%s", swoc::bwprint(text, "[{}] '{}' variable type mismatch, expected {}, got {}", PLUGIN_NAME, cfg->record_name,
                                static_cast<int>(expected_type), static_cast<int>(type))
                    .c_str());
    return TS_ERROR; // Ignore the field
  }

  // If no type set or the type did match, then we assume it's safe to use the
  // expected type.
  try {
    switch (expected_type) {
    case TS_RECORDDATATYPE_INT:
      item->_data.rec_int = value.as<int64_t>();
      break;
    case TS_RECORDDATATYPE_STRING: {
      std::string str = value.as<std::string>();
      if (value.IsNull() || str == "NULL") {
        item->_data.rec_string = nullptr;
        item->_data_len        = 0;
      } else {
        item->_data.rec_string = TSstrdup(str.c_str());
        item->_data_len        = str.size();
      }
    } break;
    case TS_RECORDDATATYPE_FLOAT:
      item->_data.rec_float = value.as<float>();
      break;
    default:
      TSError("[%s] field %s: type(%d) not support (unheard of)", PLUGIN_NAME, cfg->field_name, expected_type);
      return TS_ERROR;

      ;
    }
  } catch (YAML::BadConversion const &e) {
    TSError("%s", swoc::bwprint(text, "[{}] We couldn't convert the passed field({}) value({}) to the expected type {}. {}",
                                PLUGIN_NAME, cfg->field_name, value.as<std::string>(), static_cast<int>(expected_type), e.what())
                    .c_str());
    return TS_ERROR;
  }

  item->_name = name;
  item->_type = expected_type;
  ++*ctx.current;

  return TS_SUCCESS;
}
} // namespace

bool
RemapConfigs::parse_file(const char *filename)
{
  std::string path;

  if (!filename || ('\0' == *filename)) {
    return false;
  }

  if (*filename == '/') {
    // Absolute path, just use it.
    path = filename;
  } else {
    // Relative path. Make it relative to the configuration directory.
    path  = TSConfigDirGet();
    path += "/";
    path += filename;
  }

  Dbg(dbg_ctl, "loading configuration file %s", path.c_str());

  YAML::Node root;
  try {
    root = YAML::LoadFile(path);

  } catch (std::exception const &ex) {
    std::string text;
    TSError("[%s] We found an error while parsing '%s'.", PLUGIN_NAME, swoc::bwprint(text, "{}. {}", path, ex.what()).c_str());
    return false;
  }

  Context ctx{&*_items, &_current}; // Context object will be passed on every callback so the handler can fill in
                                    // the details.
  auto ret = TSRecYAMLConfigParse(reinterpret_cast<TSYaml>(&root), scalar_node_handler, &ctx);

  if (ret != TS_SUCCESS) {
    TSError("[%s] We found an error while parsing '%s'.", PLUGIN_NAME, path.c_str());
    return false;
  }

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

  Dbg(dbg_ctl, "remap plugin is successfully initialized");
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
        Dbg(dbg_ctl, "Setting config id %d to %" PRId64 "", conf->_items[ix]._name, conf->_items[ix]._data.rec_int);
        break;
      case TS_RECORDDATATYPE_STRING:
        TSHttpTxnConfigStringSet(txnp, conf->_items[ix]._name, conf->_items[ix]._data.rec_string, conf->_items[ix]._data_len);
        Dbg(dbg_ctl, "Setting config id %d to %s", conf->_items[ix]._name, conf->_items[ix]._data.rec_string);
        break;
      case TS_RECORDDATATYPE_FLOAT:
        TSHttpTxnConfigFloatSet(txnp, conf->_items[ix]._name, conf->_items[ix]._data.rec_int);
        Dbg(dbg_ctl, "Setting config id %d to %f", conf->_items[ix]._name, conf->_items[ix]._data.rec_float);
        break;
      default:
        break; // Error ?
      }
    }
  }

  return TSREMAP_NO_REMAP; // This plugin never rewrites anything.
}
