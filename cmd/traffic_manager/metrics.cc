/** @file
 *
 *  Traffic Manager custom metrics.
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

#include <cmath>

#include "ts/ink_config.h"
#include "ts/ink_memory.h"
#include "ts/Ptr.h"
#include "ts/Vec.h"
#include "ts/I_Layout.h"
#include "bindings/bindings.h"
#include "bindings/metrics.h"
#include "I_RecCore.h"
#include "MgmtDefs.h"
#include "MgmtUtils.h"
#include "metrics.h"

struct Evaluator {
  Evaluator() : rec_name(nullptr), data_type(RECD_NULL), ref(LUA_NOREF) {}

  ~Evaluator() { ink_release_assert(this->ref == LUA_NOREF); }

  Evaluator(const Evaluator &) = delete;
  Evaluator &operator=(const Evaluator &) = delete;

  bool
  bind(lua_State *L, const char *metric, const char *expression)
  {
    const char *err = nullptr;
    if (RecGetRecordDataType(metric, &this->data_type) != REC_ERR_OKAY) {
      return false;
    }

    this->rec_name = ats_strdup(metric);

    switch (luaL_loadstring(L, expression)) {
    case LUA_ERRSYNTAX:
    case LUA_ERRMEM:
      err = luaL_checkstring(L, -1);
      Debug("lua", "loadstring failed for %s: %s", metric, err);
      luaL_error(L, "invalid expression for %s: %s", metric, err);
      return false;
    case 0:
      break; // success
    }

    // The loaded chunk is now on the top of the stack. Stuff it into the registry
    // so we can evaluate it later.
    this->ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return true;
  }

  void
  unbind(lua_State *L)
  {
    if (this->ref != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, this->ref);
    }

    ats_free(this->rec_name);

    this->ref       = LUA_NOREF;
    this->rec_name  = nullptr;
    this->data_type = RECD_NULL;
  }

  void
  eval(lua_State *L) const
  {
    // Push the stashed expression chunk onto the stack.
    lua_rawgeti(L, LUA_REGISTRYINDEX, this->ref);

    // Evaluate it. Note that we don't emit a warning for
    // evaluation errors. This is because not all metrics (eg.
    // cache metrics) are available early in startup so we don't
    // want to log spurious warning. Unfortunately it means that
    // to check your config for errors you need to enable
    // diagnostic tags.
    lua_pushstring(L, this->rec_name);
    if (lua_pcall(L, 1 /* nargs */, 1 /* nresults */, 0) != 0) {
      Debug("lua", "failed to evaluate %s: %s", this->rec_name, lua_tostring(L, -1));
      lua_pop(L, 1);
      return;
    }

    // If we got a return value, set it on the record. Records can return nil to
    // indicate they don't want to be set on this round.
    if (!lua_isnil(L, -1)) {
      RecData rec_value;

      switch (this->data_type) {
      case RECD_INT:
        rec_value.rec_int = lua_tointeger(L, -1);
        break;
      case RECD_COUNTER:
        rec_value.rec_counter = lua_tointeger(L, -1);
        break;
      case RECD_FLOAT:
        // Lua will eval 0/0 to NaN rather than 0.
        rec_value.rec_float = lua_tonumber(L, -1);
        if (std::isnan(rec_value.rec_float)) {
          rec_value.rec_float = 0.0;
        }
        break;
      default:
        goto done;
      }

      RecSetRecord(RECT_NULL, this->rec_name, this->data_type, &rec_value, nullptr, REC_SOURCE_EXPLICIT);
    }

  done:
    // Pop the return value.
    lua_pop(L, 1);
  }

private:
  char *rec_name;
  RecDataT data_type;

  int ref;
};

struct EvaluatorList {
  EvaluatorList() : update(true), passes(0) {}
  ~EvaluatorList()
  {
    forv_Vec (Evaluator, e, this->evaluators) {
      delete e;
    }
  }

  EvaluatorList(const EvaluatorList &) = delete;
  EvaluatorList &operator=(const EvaluatorList &) = delete;

  void
  push_back(Evaluator *e)
  {
    evaluators.push_back(e);
  }

  void
  unbind(lua_State *L) const
  {
    forv_Vec (Evaluator, e, this->evaluators) {
      e->unbind(L);
    }
  }

  void
  evaluate(lua_State *L) const
  {
    ink_hrtime start = ink_get_hrtime_internal();
    ink_hrtime elapsed;

    forv_Vec (Evaluator, e, this->evaluators) {
      e->eval(L);
    }

    elapsed = ink_hrtime_diff(ink_get_hrtime_internal(), start);
    Debug("lua", "evaluated %u metrics in %fmsec", evaluators.length(), ink_hrtime_to_usec(elapsed) / 1000.0);
  }

  bool update;
  int64_t passes;
  Vec<Evaluator *> evaluators;
};

static int
update_metrics_namespace(lua_State *L)
{
  lua_Integer count;

  lua_metrics_install(L);
  count = lua_tointeger(L, 1);
  lua_pop(L, 1);

  return count;
}

static int64_t
timestamp_now_msec()
{
  ink_hrtime now = ink_get_hrtime_internal();
  return ink_hrtime_to_msec(now);
}

static int
metrics_register_evaluator(lua_State *L)
{
  const char *metric;
  const char *chunk;
  Evaluator *eval;
  EvaluatorList *evaluators;
  BindingInstance *binding;

  // The metric name is the first upvalue (from the record creation closure).
  metric = lua_tostring(L, lua_upvalueindex(1));
  // The evaluation chunk is the (only) argument.
  chunk = lua_tostring(L, -1);

  binding    = BindingInstance::self(L);
  evaluators = (EvaluatorList *)binding->retrieve_ptr("evaluators");

  ink_release_assert(evaluators != nullptr);

  eval = new Evaluator();
  eval->bind(L, metric, chunk);

  evaluators->push_back(eval);
  return 0;
}

static int
metrics_create_record(lua_State *L, RecDataT data_type)
{
  const char *name;
  RecT rec_type = RECT_NULL;
  int error     = REC_ERR_FAIL;

  BindingInstance::typecheck(L, "record.create", LUA_TSTRING, LUA_TNONE);

  // Get the name of the record to create.
  name = lua_tostring(L, -1);

  if (strncmp(name, "proxy.process.", sizeof("proxy.process.") - 1) == 0) {
    rec_type = RECT_PROCESS;
  } else if (strncmp(name, "proxy.node.", sizeof("proxy.node.") - 1) == 0) {
    rec_type = RECT_NODE;
  }

  // You have to follow the naming convention.
  if (rec_type == RECT_NULL) {
    luaL_error(L, "invalid metric name '%s'", name);
  }

  switch (data_type) {
  case RECD_INT:
    error = RecRegisterStatInt(rec_type, name, 0, RECP_NON_PERSISTENT);
    break;
  case RECD_FLOAT:
    error = RecRegisterStatFloat(rec_type, name, 0, RECP_NON_PERSISTENT);
    break;
  case RECD_COUNTER:
    error = RecRegisterStatCounter(rec_type, name, 0, RECP_NON_PERSISTENT);
    break;
  default:
    break;
  }

  if (error != REC_ERR_OKAY) {
    luaL_error(L, "failed to register metric '%s'", name);
  }

  // Push a copy of the metric name onto the stack.
  lua_pushvalue(L, -1);
  // Push the Evaluator as a closure with the metric name as an upvalue.
  lua_pushcclosure(L, metrics_register_evaluator, 1);

  Debug("lua", "registered %s as record type %d", name, rec_type);
  return 1;
}

static int
metrics_create_integer(lua_State *L)
{
  return metrics_create_record(L, RECD_INT);
}

static int
metrics_create_counter(lua_State *L)
{
  return metrics_create_record(L, RECD_COUNTER);
}

static int
metrics_create_float(lua_State *L)
{
  return metrics_create_record(L, RECD_FLOAT);
}

bool
metrics_binding_initialize(BindingInstance &binding)
{
  if (!binding.construct()) {
    mgmt_fatal(0, "failed to initialize Lua runtime\n");
  }

  // Register the metrics userdata type.
  lua_metrics_register(binding.lua);
  update_metrics_namespace(binding.lua);

  // Register our own API.
  binding.bind_function("integer", metrics_create_integer);
  binding.bind_function("counter", metrics_create_counter);
  binding.bind_function("float", metrics_create_float);

  binding.bind_constant("metrics.now.msec", timestamp_now_msec());
  binding.bind_constant("metrics.update.pass", lua_Integer(0));

  // Stash a backpointer to the evaluators.
  binding.attach_ptr("evaluators", new EvaluatorList());

  return true;
}

void
metrics_binding_destroy(BindingInstance &binding)
{
  EvaluatorList *evaluators;

  evaluators = (EvaluatorList *)binding.retrieve_ptr("evaluators");
  binding.attach_ptr("evaluators", nullptr);

  evaluators->unbind(binding.lua);
  delete evaluators;
}

bool
metrics_binding_configure(BindingInstance &binding)
{
  ats_scoped_str sysconfdir(RecConfigReadConfigDir());
  ats_scoped_str config(Layout::get()->relative_to(sysconfdir, "metrics.config"));

  return binding.require(config.get());
}

void
metrics_binding_evaluate(BindingInstance &binding)
{
  EvaluatorList *evaluators;

  evaluators = (EvaluatorList *)binding.retrieve_ptr("evaluators");
  ink_release_assert(evaluators != nullptr);

  // Keep updating the namespace until it settles (ie. we make 0 updates).
  if (evaluators->update) {
    evaluators->update = update_metrics_namespace(binding.lua) ? true : false;
  }

  binding.bind_constant("metrics.now.msec", timestamp_now_msec());
  binding.bind_constant("metrics.update.pass", ++evaluators->passes);
  evaluators->evaluate(binding.lua);

  // Periodically refresh the namespace to catch newly added metrics.
  if (evaluators->passes % 10 == 0) {
    evaluators->update = true;
  }
}
