/** @file
 *
 *  Lua bindings REPL.
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

#include "ink_autoconf.h"
#include "bindings.h"
#include <stdlib.h>

#if HAVE_READLINE_H
#include <readline.h>
#endif

void
repl(BindingInstance &binding)
{
#if HAVE_READLINE_H
  for (;;) {
    char *line;

    line = readline("> ");
    if (line == NULL) {
      exit(0);
    }

    if (*line) {
      ::add_history(line);

      if (luaL_loadbuffer(this->lua, line, ::strlen(line), "@stdin" /* source */) != 0 ||
          lua_pcall(this->lua, 0, LUA_MULTRET, 0) != 0) {
        // Pop the error message off the top of the stack and show it ...
        error("%s\n", lua_tostring(this->lua, -1));
        lua_pop(this->lua, 1);
      }
    }

    ::free(line);
  }

#endif /*  HAVE_READLINE_H */
  ::exit(0);
}
