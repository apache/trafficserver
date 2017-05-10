/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "LogBindings.h"
#include "LogFormat.h"
#include "LogFilter.h"
#include "LogObject.h"
#include "LogConfig.h"

#include "ts/TestBox.h"

static int
refcount_object_new(lua_State *L, const char *type_name, RefCountObj *obj)
{
  RefCountObj **ptr = (RefCountObj **)lua_newuserdata(L, sizeof(RefCountObj *));

  // Hold a refcount in the Lua state until GC time.
  *ptr = obj;
  (*ptr)->refcount_inc();

  luaL_getmetatable(L, type_name);
  lua_setmetatable(L, -2);

  // Leave the userdata on the stack.
  return 1;
}

static int
refcount_object_gc(lua_State *L)
{
  RefCountObj **ptr = (RefCountObj **)lua_touserdata(L, -1);

  if ((*ptr) && (*ptr)->refcount_dec() == 0) {
    (*ptr)->free();
  }

  return 0;
}

template <typename T>
static T *
refcount_object_get(lua_State *L, int index, const char *type_name)
{
  RefCountObj **ptr;

  ptr = (RefCountObj **)luaL_checkudata(L, index, type_name);
  if (!ptr) {
    luaL_typerror(L, index, type_name);
    return nullptr; // Not reached, since luaL_typerror throws.
  }

  return dynamic_cast<T *>(*ptr);
}

static int
create_format_object(lua_State *L)
{
  const char *format;
  lua_Integer interval = 0;

  BindingInstance::typecheck(L, "format", LUA_TTABLE, LUA_TNONE);
  interval = lua_getfield<lua_Integer>(L, -1, "Interval", 0);
  format   = lua_getfield<const char *>(L, -1, "Format", nullptr);

  if (format == nullptr) {
    luaL_error(L, "missing 'Format' argument");
  }

  // TODO: Remove the name field from log formats. Since we can pass format
  // objects directly, we don't need filter names or a global format container.

  return refcount_object_new(L, "log.format", new LogFormat("lua", format, interval));
}

static int
create_filter_object(lua_State *L, const char *name, LogFilter::Action action)
{
  const char *condition;
  LogFilter *filter;

  BindingInstance::typecheck(L, name, LUA_TSTRING, LUA_TNONE);
  condition = lua_tostring(L, -1);

  // TODO: Remove the name field from log filters. Since we can pass filter objects
  // directly, we don't need filter names or a global filter container.

  filter = LogFilter::parse("lua", action, condition);
  if (filter == nullptr) {
    // NOTE: Not really a return since luaL_error throws.
    return (luaL_error(L, "invalid filter condition '%s'", condition));
  }

  return refcount_object_new(L, "log.filter", filter);
}

static int
create_accept_filter_object(lua_State *L)
{
  return create_filter_object(L, "filter.accept", LogFilter::ACCEPT);
}

static int
create_reject_filter_object(lua_State *L)
{
  return create_filter_object(L, "filter.reject", LogFilter::REJECT);
}

static int
create_wipe_filter_object(lua_State *L)
{
  return create_filter_object(L, "filter.wipe", LogFilter::WIPE_FIELD_VALUE);
}

static LogHost *
make_log_host(LogHost *parent, LogObject *log, const char *s)
{
  ats_scoped_obj<LogHost> lh;

  // set_name_or_ipstr() silently mmodifies its argument, so make
  // a copy to avoid writing into memory owned by the Lua VM.
  std::string spec(s);

  lh = new LogHost(log->get_full_filename(), log->get_signature());
  if (!lh->set_name_or_ipstr(spec.c_str())) {
    Error("invalid collation host specification '%s'", s);
    return nullptr;
  }

  if (parent) {
    // If we already have a LogHost, this is a failover host, so append
    // it to the end of the failover list.
    LogHost *last = parent;
    while (last->failover_link.next != nullptr) {
      last = last->failover_link.next;
    }

    Debug("lua", "added failover host %p to %p for %s", lh.get(), last, s);
    last->failover_link.next = lh.release();
    return parent;
  }

  return lh.release();
}

static bool
log_object_add_hosts(lua_State *L, LogObject *log, int value, bool top)
{
  // No hosts.
  if (lua_isnil(L, value)) {
    return true;
  }

  // A single host.
  if (lua_isstring(L, value)) {
    log->add_loghost(make_log_host(nullptr, log, lua_tostring(L, value)), false /* take ownership */);
    return true;
  }

  if (lua_istable(L, value)) {
    lua_scoped_stack saved(L);

    int count   = luaL_getn(L, value);
    LogHost *lh = nullptr;

    saved.push_value(value); // Push the table to -1.

    for (int i = 1; i <= count; ++i) {
      lua_rawgeti(L, -1, i); // Push the i-th element of the array.

      // We allow one level of array nesting to represent failover hosts. Puke if
      // a nested array contains anything other than strings.
      if (!top && !lua_isstring(L, -1)) {
        luaL_error(L, "bad type, expected 'string' but found '%s'", lua_typename(L, lua_type(L, -1)));
      }

      switch (lua_type(L, -1)) {
      case LUA_TSTRING:
        // This is a collation host address. Add it as a peer host if
        // we are on the top level, or as a failover host if we are
        // in a nested array.
        lh = make_log_host(top ? nullptr : lh, log, lua_tostring(L, -1));
        break;

      case LUA_TTABLE:
        // Recurse to construct a failover group from a nested array.
        if (!log_object_add_hosts(L, log, -1, false /* nested */)) {
          lua_pop(L, 1); // Pop the element.
          return false;
        }

        break;

      default:
        luaL_error(L, "bad type, expected 'string' or 'array' but found '%s'", lua_typename(L, lua_type(L, -1)));
      }

      // If this is the top level array, then each entry is a LogHost. For nested arrays, we aggregate
      // the hosts into a flattened failover group.
      if (top) {
        log->add_loghost(lh, false /* take ownership */);
        lh = nullptr;
      }

      lua_pop(L, 1); // Pop the element.
    }

    // Attach the log host to this log object. lh will only be non-null if we
    // are dealing with a nested array of failover hosts.
    log->add_loghost(lh, false /* take ownership */);
    return true;
  }

  return false;
}

static bool
log_object_add_filters(lua_State *L, LogObject *log, int value)
{
  // No filters.
  if (lua_isnil(L, value)) {
    return true;
  }

  // A single filter.
  if (lua_isuserdata(L, value)) {
    LogFilter *filter = refcount_object_get<LogFilter>(L, value, "log.filter");

    if (filter) {
      // TODO: We copy the filter for nowm but later we can refactor
      // so that the LogObject just holds a refcount on the filter.
      log->add_filter(filter, true /* copy */);
      return true;
    }
  }

  // An array of filters.
  if (lua_istable(L, value)) {
    lua_scoped_stack saved(L);
    LogFilter *filter;
    int count = luaL_getn(L, value);

    saved.push_value(value); // Push the table to -1.

    for (int i = 1; i <= count; ++i) {
      lua_rawgeti(L, -1, i); // Push the i-th element of the array.
      filter = refcount_object_get<LogFilter>(L, -1, "log.filter");
      if (filter) {
        log->add_filter(filter, true /* copy */);
      }

      lua_pop(L, 1); // Pop the element.

      if (filter == nullptr) {
        return false;
      }
    }

    return true;
  }

  return false;
}

static int
create_log_object(lua_State *L, const char *name, LogFileFormat which)
{
  LogConfig *conf = (LogConfig *)BindingInstance::self(L)->retrieve_ptr("log.config");
  Ptr<LogObject> log;
  Ptr<LogFormat> fmt;

  const char *filename;
  const char *header;
  lua_Integer rolling;
  lua_Integer interval;
  lua_Integer offset;
  lua_Integer size;

  BindingInstance::typecheck(L, name, LUA_TTABLE, LUA_TNONE);

  filename = lua_getfield<const char *>(L, -1, "Filename", nullptr);
  header   = lua_getfield<const char *>(L, -1, "Header", nullptr);
  rolling  = lua_getfield<lua_Integer>(L, -1, "RollingEnabled", conf->rolling_enabled);
  interval = lua_getfield<lua_Integer>(L, -1, "RollingIntervalSec", conf->rolling_interval_sec);
  offset   = lua_getfield<lua_Integer>(L, -1, "RollingOffsetHr", conf->rolling_offset_hr);
  size     = lua_getfield<lua_Integer>(L, -1, "RollingSizeMb", conf->rolling_size_mb);

  lua_pushstring(L, "Format"); // Now key is at -1 and table is at -2.
  lua_gettable(L, -2);         // Now the result is at -1.

  // We support both strings and log.format arguments for the "Format" key. Since
  // LogObject copies the format, we only have to keep a local refcount.
  if (lua_isstring(L, -1)) {
    fmt = new LogFormat("lua", lua_tostring(L, -1));
  } else {
    fmt = refcount_object_get<LogFormat>(L, -1, "log.format");
  }

  lua_pop(L, 1); // Pop the userdata at -1.

  if (!fmt) {
    luaL_error(L, "missing or invalid 'Format' argument");
  }

  if (filename == nullptr) {
    luaL_error(L, "missing 'Filename' argument");
  }

  switch (rolling) {
  case Log::NO_ROLLING:
  case Log::ROLL_ON_TIME_ONLY:
  case Log::ROLL_ON_SIZE_ONLY:
  case Log::ROLL_ON_TIME_OR_SIZE:
  case Log::ROLL_ON_TIME_AND_SIZE:
    break;
  case Log::INVALID_ROLLING_VALUE:
  default:
    luaL_error(L, "invalid 'RollingEnabled' argument");
  }

  log = new LogObject(fmt.get(), conf->logfile_dir, filename, which, header, (Log::RollingEnabledValues)rolling,
                      conf->collation_preproc_threads, interval, offset, size);

  lua_pushstring(L, "Filters"); // Now key is at -1 and table is at -2.
  lua_gettable(L, -2);          // Now the result is at -1.

  if (!log_object_add_filters(L, log.get(), -1)) {
    luaL_error(L, "invalid 'Filters' argument");
  }

  lua_pop(L, 1);

  lua_pushstring(L, "CollationHosts"); // Now key is at -1 and table is at -2.
  lua_gettable(L, -2);                 // Now the result is at -1.

  if (!log_object_add_hosts(L, log.get(), -1, true /* top level */)) {
    luaL_error(L, "invalid 'CollationHosts' argument");
  }

  lua_pop(L, 1);

  if (is_debug_tag_set("log-config")) {
    log->display(stderr);
  }

  // Now the object is complete, give it to the object manager.
  conf->log_object_manager.manage_object(log.get());

  // Return nil.
  lua_pushnil(L);
  return 1;
}

static int
create_binary_log_object(lua_State *L)
{
  return create_log_object(L, "log.binary", LOG_FILE_BINARY);
}

static int
create_ascii_log_object(lua_State *L)
{
  return create_log_object(L, "log.ascii", LOG_FILE_ASCII);
}

static int
create_pipe_log_object(lua_State *L)
{
  return create_log_object(L, "log.pipe", LOG_FILE_PIPE);
}

bool
MakeLogBindings(BindingInstance &binding, LogConfig *conf)
{
  static const luaL_reg metatable[] = {
    {"__gc", refcount_object_gc}, {nullptr, nullptr},
  };

  // Register the logging object API.
  binding.bind_function("log.ascii", create_ascii_log_object);
  binding.bind_function("log.pipe", create_pipe_log_object);
  binding.bind_function("log.binary", create_binary_log_object);

  binding.bind_function("format", create_format_object);

  binding.bind_function("filter.accept", create_accept_filter_object);
  binding.bind_function("filter.reject", create_reject_filter_object);
  binding.bind_function("filter.wipe", create_wipe_filter_object);

  // 0: Do not automatically roll.
  binding.bind_constant("log.roll.none", lua_Integer(Log::NO_ROLLING));

  // 1: Roll at a certain time frequency, specified by
  //    RollingIntervalSec, and RollingOffsetHr.
  binding.bind_constant("log.roll.time", lua_Integer(Log::ROLL_ON_TIME_ONLY));

  // 2: roll when the size exceeds RollingSizeMb.
  binding.bind_constant("log.roll.size", lua_Integer(Log::ROLL_ON_SIZE_ONLY));

  // 3: Roll when either the specified rolling time is reached or the
  //    specified file size is reached.
  binding.bind_constant("log.roll.any", lua_Integer(Log::ROLL_ON_TIME_OR_SIZE));

  // 4: Roll the log file when the specified rolling time is reached
  //    if the size of the file equals or exceeds the specified size.
  binding.bind_constant("log.roll.both", lua_Integer(Log::ROLL_ON_TIME_AND_SIZE));

  // Constants for the log object "Protocol" field.
  binding.bind_constant("log.protocol.http", lua_Integer(LOG_ENTRY_HTTP));

  // We register the same metatable for each logging object, since none
  // of them have any real API; they are just handles to internal logging
  // objects.
  BindingInstance::register_metatable(binding.lua, "log.filter", metatable);
  BindingInstance::register_metatable(binding.lua, "log.object", metatable);
  BindingInstance::register_metatable(binding.lua, "log.format", metatable);

  // Attach the LogConfig backpointer.
  binding.attach_ptr("log.config", conf);

  return true;
}

EXCLUSIVE_REGRESSION_TEST(LogConfig_CollationHosts)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);

  LogConfig config;
  BindingInstance binding;

  const char single[] = R"LUA(
    log.ascii {
      Format = "%<chi>",
      Filename = "one-collation-host",
      CollationHosts = "127.0.0.1:8080",
    }
  )LUA";

  const char multi[] = R"LUA(
    log.ascii {
      Format = "%<chi>",
      Filename = "many-collation-hosts",
      CollationHosts = { "127.0.0.1:8080", "127.0.0.1:8081" },
    }
  )LUA";

  const char failover[] = R"LUA(
    log.ascii {
      Format = "%<chi>",
      Filename = "many-collation-failover",
      CollationHosts =  {
        { '127.0.0.1:8080', '127.0.0.1:8081' },
        { '127.0.0.2:8080', '127.0.0.2:8081' },
        { '127.0.0.3:8080', '127.0.0.3:8081' },
      }
    }
  )LUA";

  const char combined[] = R"LUA(
    log.ascii {
      Format = "%<chi>",
      Filename = "mixed-collation-failover",
      CollationHosts =  {
        { '127.0.0.1:8080', '127.0.0.1:8081' },
        { '127.0.0.2:8080', '127.0.0.2:8081' },
        { '127.0.0.3:8080', '127.0.0.3:8081' },
        '127.0.0.4:8080',
        '127.0.0.5:8080',
      }
    }
  )LUA";

  (void)single;
  (void)multi;
  (void)failover;
  (void)combined;

  box = REGRESSION_TEST_PASSED;

  box.check(binding.construct(), "construct Lua binding instance");
  box.check(MakeLogBindings(binding, &config), "load Lua log configuration API");

  box.check(binding.eval(single), "configuring a single log host");
  box.check(binding.eval(multi), "configuring multiple log hosts");
  box.check(binding.eval(failover), "configuring a multiple hosts with failover");
  box.check(binding.eval(combined), "configuring a multiple hosts some with failover");

  config.display(stderr);
}
