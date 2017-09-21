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

class TsConfigDescriptor {
   enum class Type { ARRAY, OBJECT, INT, FLOAT, STRING, ENUM };
   std::string name;
   std::string description;
   Type type;
   std::string type_name;
};

class TsConfigBase {
    TsConfigBase() {}
   TsConfigBase(TsConfigDescriptor const& d) : descriptor(d) {}
   TsConfigDescriptor const& descriptor;

   virtual ts::Errata loader(lua_State* s) = 0;
};

template < typename T >
class TsConfigInt : public TsConfigBase {
   TsConfigInt(TsConfigDescriptor const& d, (T::int)& i) : TsConfigBase(d), ref(i) {}
   T::int & ref;
   ts::Errata loader(lua_State* s) override;
}

template < typename T, typename E >
class TsConfigEnum : public TsConfigBase {
   TsConfigInt(TsConfigDescriptor const& d, (T::E)& i) : TsConfigBase(d), ref(i) {}
   T::E & ref;
   ts::Errata loader(lua_State* s) override;
}

#endif /* TSCONFIGLUA_H */

