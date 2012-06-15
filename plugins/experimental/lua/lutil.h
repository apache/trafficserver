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

// Return the type name string for the given index.
#define LTYPEOF(L, index) lua_typename(L, lua_type(L, index))

void * LuaAllocate(void * ud, void * ptr, size_t osize, size_t nsize);
void LuaPushMetatable(lua_State * lua, const char * name, const luaL_Reg * exports);
void LuaLoadLibraries(lua_State * lua);
void LuaRegisterLibrary(lua_State * lua, const char * name, lua_CFunction loader);
