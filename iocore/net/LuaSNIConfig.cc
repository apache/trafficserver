/** @file

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

#include "LuaSNIConfig.h"
#include <cstring>
#include "ts/Diags.h"
#include "P_SNIActionPerformer.h"
#include "tsconfig/Errata.h"
#include "tsconfig/TsConfigLua.h"

TsConfigDescriptor LuaSNIConfig::desc = {TsConfigDescriptor::Type::ARRAY, "Array", "Item vector", "Vector"};
TsConfigArrayDescriptor LuaSNIConfig::DESCRIPTOR(LuaSNIConfig::desc);
TsConfigDescriptor LuaSNIConfig::Item::FQDN_DESCRIPTOR = {TsConfigDescriptor::Type::STRING, "String", TS_fqdn,
                                                          "Fully Qualified Domain Name"};
TsConfigDescriptor LuaSNIConfig::Item::DISABLE_h2_DESCRIPTOR = {TsConfigDescriptor::Type::BOOL, "Boolean", TS_disable_H2,
                                                                "Disable H2"};
TsConfigEnumDescriptor LuaSNIConfig::Item::LEVEL_DESCRIPTOR = {TsConfigDescriptor::Type::ENUM,
                                                               "enum",
                                                               "Level",
                                                               "Level for client verification",
                                                               {{"NONE", 0}, {"MODERATE", 1}, {"STRICT", 2}}};
TsConfigDescriptor LuaSNIConfig::Item::TUNNEL_DEST_DESCRIPTOR = {TsConfigDescriptor::Type::STRING, "String", TS_tunnel_route,
                                                                 "tunnel route destination"};
TsConfigDescriptor LuaSNIConfig::Item::CLIENT_CERT_DESCRIPTOR = {TsConfigDescriptor::Type::STRING, "String", TS_client_cert,
                                                                 "Client certificate to present to the next hop server"};
TsConfigDescriptor LuaSNIConfig::Item::VERIFY_NEXT_SERVER_DESCRIPTOR = {TsConfigDescriptor::Type::INT, "Int",
                                                                        TS_verify_origin_server, "Next hop verification level"};

ts::Errata
LuaSNIConfig::loader(lua_State *L)
{
  ts::Errata zret;
  //  char buff[256];
  //  int error;

  lua_getfield(L, LUA_GLOBALSINDEX, "server_config");
  int l_type = lua_type(L, -1);

  switch (l_type) {
  case LUA_TTABLE: // this has to be a multidimensional table
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      l_type = lua_type(L, -1);
      if (l_type == LUA_TTABLE) { // the item should be table
        // new Item
        LuaSNIConfig::Item item;
        item.loader(L);
        items.push_back(item);
      } else {
        zret.push(ts::Errata::Message(0, 0, "Invalid Entry at SNI config"));
      }
      lua_pop(L, 1);
    }
    break;
  case LUA_TSTRING:
    Debug("ssl", "string value %s", lua_tostring(L, -1));
    break;
  default:
    zret.push(ts::Errata::Message(0, 0, "Invalid Lua SNI Config"));
    Debug("ssl", "Please check your SNI config");
    break;
  }

  return zret;
}

ts::Errata
LuaSNIConfig::Item::loader(lua_State *L)
{
  ts::Errata zret;
  //-1 will contain the subarray now (since it is a value in the main table))
  lua_pushnil(L);
  while (lua_next(L, -2)) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      Debug("ssl", "string keys expected for entries in %s", SSL_SERVER_NAME_CONFIG);
    }
    const char *name = lua_tostring(L, -2);
    if (!strncmp(name, TS_fqdn, strlen(TS_fqdn))) {
      FQDN_CONFIG.loader(L);
    } else if (!strncmp(name, TS_disable_H2, strlen(TS_disable_H2))) {
      DISABLEH2_CONFIG.loader(L);
    } else if (!strncmp(name, TS_verify_client, strlen(TS_verify_client))) {
      VERIFYCLIENT_CONFIG.loader(L);
    } else if (!strncmp(name, TS_verify_origin_server, strlen(TS_verify_origin_server))) {
      VERIFY_NEXT_SERVER_CONFIG.loader(L);
    } else if (!strncmp(name, TS_client_cert, strlen(TS_client_cert))) {
      CLIENT_CERT_CONFIG.loader(L);
    } else if (!strncmp(name, TS_tunnel_route, strlen(TS_tunnel_route))) {
      TUNNEL_DEST_CONFIG.loader(L);
    } else {
      zret.push(ts::Errata::Message(0, 0, "Invalid Entry at SNI config"));
    }
    lua_pop(L, 1);
  }
  return zret;
}

ts::Errata
LuaSNIConfig::registerEnum(lua_State *L)
{
  ts::Errata zret;
  lua_newtable(L);
  lua_setglobal(L, "LevelTable");
  int i = start;
  LUA_ENUM(L, "NONE", i++);
  LUA_ENUM(L, "MODERATE", i++);
  LUA_ENUM(L, "STRICT", i++);
  return zret;
}
