/** @file

  A class that deals with plugin Dynamic Shared Objects (DSO)

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

  @section details Details

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#include "PluginDso.h"
#ifdef PLUGIN_DSO_TESTS
#include "unit-tests/plugin_testing_common.h"
#else
#include "tscore/Diags.h"
#endif

PluginDso::PluginDso(const fs::path &configPath, const fs::path &effectivePath, const fs::path &runtimePath)
  : _configPath(configPath), _effectivePath(effectivePath), _runtimePath(runtimePath)
{
}

PluginDso::~PluginDso()
{
  std::string error;
  (void)unload(error);
}

bool
PluginDso::load(std::string &error)
{
  /* Clear all errors */
  error.clear();
  _errorCode.clear();
  bool result = true;

  if (isLoaded()) {
    error.append("plugin already loaded");
    return false;
  }

  Debug(_tag, "plugin '%s' started loading DSO", _configPath.c_str());

  /* Find plugin DSO looking through the search dirs */
  if (_effectivePath.empty()) {
    error.append("empty effective path");
    result = false;
  } else {
    Debug(_tag, "plugin '%s' effective path: %s", _configPath.c_str(), _effectivePath.c_str());

    /* Copy the installed plugin DSO to a runtime directory */
    std::error_code ec;
    if (!copy(_effectivePath, _runtimePath, ec)) {
      std::string temp_error;
      temp_error.append("failed to create a copy: ").append(strerror(ec.value()));
      error.assign(temp_error);
      result = false;
    } else {
      Debug(_tag, "plugin '%s' runtime path: %s", _configPath.c_str(), _runtimePath.c_str());

      /* Save the time for later checking if DSO got modified in consecutive DSO reloads */
      std::error_code ec;
      fs::file_status fs = fs::status(_effectivePath, ec);
      _mtime             = fs::modification_time(fs);
      Debug(_tag, "plugin '%s' mоdification time %ld", _configPath.c_str(), _mtime);

      /* Now attemt to load the plugin DSO */
      if ((_dlh = dlopen(_runtimePath.c_str(), RTLD_NOW)) == nullptr) {
#if defined(freebsd) || defined(openbsd)
        char *err = (char *)dlerror();
#else
        char *err = dlerror();
#endif
        error.append(err ? err : "Unknown dlopen() error");
        _dlh = nullptr; /* mark that the constructor failed. */

        clean(error);
        result = false;

        Error("plugin '%s' failed to load: %s", _configPath.c_str(), error.c_str());
      }
    }

    /* Remove the runtime DSO copy even if we succeed loading to avoid leftovers after crashes */
    if (_preventiveCleaning) {
      clean(error);
    }
  }
  Debug(_tag, "plugin '%s' finished loading DSO", _configPath.c_str());

  return result;
}

/**
 * @brief unload plugin DSO
 *
 * @param error - error messages in case of failure.
 * @return true - success, false - failure during unload.
 */
bool
PluginDso::unload(std::string &error)
{
  /* clean errors */
  error.clear();
  bool result = false;

  if (isLoaded()) {
    result = (0 == dlclose(_dlh));
    _dlh   = nullptr;
    if (true == result) {
      clean(error);
    } else {
      error.append("failed to unload plugin");
    }
  } else {
    error.append("no plugin loaded");
    result = false;
  }

  return result;
}

/**
 * @brief returns the address of a symbol in the plugin DSO
 *
 * @param symbol symbol name
 * @param address reference to the address to be returned to the caller
 * @param error error messages in case of symbol is not found
 * @return true if success, false could not find the symbol (symbol can be nullptr itself)
 */
bool
PluginDso::getSymbol(const char *symbol, void *&address, std::string &error) const
{
  /* Clear the errors */
  dlerror();
  error.clear();

  address   = dlsym(_dlh, symbol);
  char *err = dlerror();

  if (nullptr == address && nullptr != err) {
    /* symbol really cannot be found */
    error.assign(err);
    return false;
  }

  return true;
}

/**
 * @brief shows if the DSO corresponding to this effective path has already been loaded.
 * @return true - loaded, false - not loaded
 */
bool
PluginDso::isLoaded()
{
  return nullptr != _dlh;
}

/**
 * @brief full path to the first plugin found in the search path which will be used to be loaded.
 *
 * @return full path to the plugin DSO.
 */
const fs::path &
PluginDso::effectivePath() const
{
  return _effectivePath;
}

/**
 * @brief full path to the runtime location of the plugin DSO actually loaded.
 *
 * @return full path to the runtime plugin DSO.
 */

const fs::path &
PluginDso::runtimePath() const
{
  return _runtimePath;
}

/**
 * @brief DSO modification time at the moment of DSO load.
 *
 * @return modification time.
 */

time_t
PluginDso::modTime() const
{
  return _mtime;
}

/**
 * @brief clean files created by the plugin instance and handle errors
 *
 * @param error a human readable error message if something goes wrong
 * @ return void
 */
void
PluginDso::clean(std::string &error)
{
  if (false == remove(_runtimePath, _errorCode)) {
    error.append("failed to remove runtime copy: ").append(_errorCode.message());
  }
}

void
PluginDso::acquire()
{
  this->refcount_inc();
  Debug(_tag, "plugin DSO acquire (ref-count:%d, dso-addr:%p)", this->refcount(), this);
}

void
PluginDso::release()
{
  Debug(_tag, "plugin DSO release (ref-count:%d, dso-addr:%p)", this->refcount() - 1, this);
  if (0 == this->refcount_dec()) {
    Debug(_tag, "unloading plugin DSO '%s' (dso-addr:%p)", _configPath.c_str(), this);
    _list.erase(this);
    delete this;
  }
}

void
PluginDso::incInstanceCount()
{
  _instanceCount.refcount_inc();
  Debug(_tag, "instance count (inst-count:%d, dso-addr:%p)", _instanceCount.refcount(), this);
}

void
PluginDso::decInstanceCount()
{
  _instanceCount.refcount_dec();
  Debug(_tag, "instance count (inst-count:%d, dso-addr:%p)", _instanceCount.refcount(), this);
}

int
PluginDso::instanceCount()
{
  return _instanceCount.refcount();
}

PluginDso::PluginList PluginDso::_list;
