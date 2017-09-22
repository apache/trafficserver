/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   TSConfigLua.h
 * Author: persia
 *
 * Created on September 21, 2017, 4:04 PM
 */


#ifndef TSCONFIGLUA_H
#define TSCONFIGLUA_H


#include "Errata.h"
#include "lua.h"
#include <unordered_map>

class TsConfigDescriptor {
   enum class Type { ARRAY, OBJECT, INT, FLOAT, STRING, ENUM };
   std::string name;
   std::string description;
   Type type;
   std::string type_name;
};

class TsConfigBase {
   TsConfigBase(TsConfigDescriptor const& d) : descriptor(d) {}
   TsConfigDescriptor const& descriptor;

   virtual ts::Errata loader(lua_State* s) = 0;
};

template < typename T >
class TsConfigInt : public TsConfigBase {
   TsConfigInt(TsConfigDescriptor const& d, int& i) : TsConfigBase(d), ref(i) {}
   int & ref;
   ts::Errata loader(lua_State* s) override;
};

template < typename T >
class TsConfigString : public TsConfigBase {
   TsConfigString(TsConfigDescriptor const& d, std::string& str) : TsConfigBase(d), ref(str) {}
    std::string& ref;
   ts::Errata loader(lua_State* s) override;
};

template < typename T, typename E >
class TsConfigEnum : public TsConfigBase {
   TsConfigEnum(TsConfigDescriptor const& d, typename T::E& i) : TsConfigBase(d), ref(i) {}
   typename T::E & ref;
   ts::Errata loader(lua_State* s) override;
};

class TsConfigArrayDescriptor : public TsConfigDescriptor {
   TsConfigArrayDescriptor(TsConfigDescriptor const& d) : item(d) {}
   TsConfigDescriptor const& item;
};

class TsConfigEnumDescriptor : public TsConfigDescriptor {
   std::unordered_map<std::string, int> values;
   std::unordered_map<int, std::string> keys;
};

class TsConfigObjectDescriptor : public TsConfigDescriptor {
   std::unordered_map<std::string, TsConfigDescriptor const*> fields;
};

#endif /* TSCONFIGLUA_H */

