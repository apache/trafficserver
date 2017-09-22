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
   LuaSNIConfig() : TsConfigBase(DESCRIPTOR) {}

   struct Item : public TsConfigBase {
      Item() : TsConfigBase(DESCRIPTOR)
         {
          Item::FQDN_CONFIG = TsConfigString<Item>(FQDN_DESCRIPTOR, fqdn);
         // Item::LEVEL_CONFIG = TsConfigInt<Item>(LEVEL_DESCRIPTOR, self::Item::level);
          Item::ACTION_CONFIG = TsConfigEnum<Item,self::Action>(ACTION_DESCRIPTOR,action);
      }
      ts::Errata loader(lua_State* s) override;

      std::string fqdn;
      int level;
      Action action;

      // These need to be initialized statically.
      static TsConfigObjectDescriptor OBJ_DESCRIPTOR;
      static TsConfigDescriptor FQDN_DESCRIPTOR;
      static TsConfigString<Item> FQDN_CONFIG;
      //static TsConfigDescriptor LEVEL_DESCRIPTOR;
      //static TsConfigInt<Item> LEVEL_CONFIG;
      static TsConfigEnumDescriptor ACTION_DESCRIPTOR;
      static TsConfigEnum<Item, Action> ACTION_CONFIG;
   };
   static std::vector<Item> items;
   ts::Errata loader(lua_State* s) override;
};

#endif /* LUASNICONFIG_H */

