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
/*
 * File:   LuaSNIConfig.h
 * Author: persia
 *
 * Created on September 21, 2017, 4:25 PM
 */

#ifndef LUASNICONFIG_H
#define LUASNICONFIG_H

#include "tsconfig/TsConfigLua.h"

#include <vector>

using ts::Errata;
#define LUA_ENUM(L, name, val)                  \
  lua_pushlstring(L, #name, sizeof(#name) - 1); \
  lua_pushnumber(L, val);                       \
  lua_settable(L, -3);

constexpr char TS_fqdn[]                 = "fqdn";
constexpr char TS_disable_H2[]           = "disable_h2";
constexpr char TS_verify_client[]        = "verify_client";
constexpr char TS_tunnel_route[]         = "tunnel_route";
constexpr char TS_verify_origin_server[] = "verify_origin_server";
constexpr char TS_client_cert[]          = "client_cert";

const int start = 0;
struct LuaSNIConfig : public TsConfigBase {
  using self = LuaSNIConfig;
  enum class Action {
    disable_h2 = start,
    verify_client,
    tunnel_route,         // blind tunnel action
    verify_origin_server, // this applies to server side vc only
    client_cert

  };
  enum class Level { NONE = 0, MODERATE, STRICT };
  static TsConfigDescriptor desc;
  static TsConfigArrayDescriptor DESCRIPTOR;

  LuaSNIConfig() : TsConfigBase(this->DESCRIPTOR) { self::Item::Initialize(); }

  struct Item : public TsConfigBase {
    Item()
      : TsConfigBase(DESCRIPTOR),
        FQDN_CONFIG(FQDN_DESCRIPTOR, fqdn),
        DISABLEH2_CONFIG(DISABLE_h2_DESCRIPTOR, disable_h2),
        VERIFYCLIENT_CONFIG(LEVEL_DESCRIPTOR, (int &)verify_client_level),
        TUNNEL_DEST_CONFIG(TUNNEL_DEST_DESCRIPTOR, tunnel_destination),
        CLIENT_CERT_CONFIG(CLIENT_CERT_DESCRIPTOR, client_cert),
        VERIFY_NEXT_SERVER_CONFIG(VERIFY_NEXT_SERVER_DESCRIPTOR, (int &)verify_origin_server)
    {
    }
    ts::Errata loader(lua_State *s) override;
    static void
    Initialize()
    {
      //      ACTION_DESCRIPTOR.values = {
      //        {TS_disable_H2, 0}, {TS_verify_client, 1}, {TS_tunnel_route, 2}, {TS_verify_origin_server, 3}, {TS_client_cert, 4}};
      //
      //      ACTION_DESCRIPTOR.keys = {
      //        {0, TS_disable_H2}, {1, TS_verify_client}, {2, TS_tunnel_route}, {3, TS_verify_origin_server}, {4, TS_client_cert}};
    }

    std::string fqdn;
    bool disable_h2             = false;
    uint8_t verify_client_level = 0;
    std::string tunnel_destination;
    uint8_t verify_origin_server = 0;
    std::string client_cert;

    // These need to be initialized statically.
    static TsConfigObjectDescriptor OBJ_DESCRIPTOR;
    static TsConfigDescriptor FQDN_DESCRIPTOR;
    TsConfigString FQDN_CONFIG;
    static TsConfigDescriptor DISABLE_h2_DESCRIPTOR;
    TsConfigBool DISABLEH2_CONFIG;
    static TsConfigEnumDescriptor LEVEL_DESCRIPTOR;
    TsConfigEnum<self::Level> VERIFYCLIENT_CONFIG;
    static TsConfigDescriptor TUNNEL_DEST_DESCRIPTOR;
    TsConfigString TUNNEL_DEST_CONFIG;
    static TsConfigDescriptor CLIENT_CERT_DESCRIPTOR;
    TsConfigString CLIENT_CERT_CONFIG;
    static TsConfigDescriptor VERIFY_NEXT_SERVER_DESCRIPTOR;
    TsConfigInt VERIFY_NEXT_SERVER_CONFIG;
    ~Item() {}
  };
  std::vector<self::Item> items;
  ts::Errata loader(lua_State *s) override;
  ts::Errata registerEnum(lua_State *L);
};
#endif /* LUASNICONFIG_H */
