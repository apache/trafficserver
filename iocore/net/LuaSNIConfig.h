/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
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
#include "tsconfig/Errata.h"
using namespace ts;
class Errata;
struct LuaSNIConfig : public TsConfigBase {
   using self = LuaSNIConfig;
   enum class Action { CLOSE, TUNNEL };

   static TsConfigArrayDescriptor DESCRIPTOR;
   LuaSNIConfig() : TsConfigBase(DESCRIPTOR), DESCRIPTOR(Item::DESCRIPTOR) {}

   struct Item : public TsConfigBase {
      Item() : TsConfigBase(DESCRIPTOR),
         FQDN_CONFIG(FQDN_DESCRIPTOR, &self::Item::fqdn),
         LEVEL_CONFIG(LEVEL_DESCRIPTOR, &self::Item::level),
         ACTION_CONFIG(ACTION_DESCRIPTOR, &self::Item::action)
         { }
      Errata loader(lua_State* s) override;

      std::string fqdn;
      int level;
      Action action;

      // These need to be initialized statically.
      static TsConfigObjectDescriptor DESCRIPTOR;
      static TsConfigDescriptor FQDN_DESCRIPTOR;
      static TsConfigString<Item> FQDN_CONFIG;
      static TsConfigDescriptor LEVEL_DESCRIPTOR;
      static TsConfigInt<Item> LEVEL_CONFIG;
      static TsConfigEnumDescriptor ACTION_DESCRIPTOR;
      static TsConfigEnum<Item, Action> ACTION_CONFIG;
   };
   std::vector<Item> items;
   ts::Errata loader(lua_State* s) override;
}

#endif /* LUASNICONFIG_H */

