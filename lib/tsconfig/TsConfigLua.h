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
 * File:   TSConfigLua.h
 * Author: persia
 *
 * Created on September 21, 2017, 4:04 PM
 */


#ifndef TSCONFIGLUA_H
#define TSCONFIGLUA_H

#include <iostream>
#include <string_view>
#include <unordered_map>

#include "tsconfig/Errata.h"
#include "luajit/src/lua.hpp"
#include <ts/HashFNV.h>

/// Hash functor for @c string_view
inline size_t TsLuaConfigSVHash(std::string_view const& sv)
{
  ATSHash64FNV1a h;
  h.update(sv.data(), sv.size());
  return h.get();
}

/** Static schema data for a configuration value.

    This is a base class for data about a configuration value. This is intended to be a singleton
    static instance that contains schema data that is the same for all instances of the
    configuration value.
*/
struct TsConfigDescriptor {
  /// Type of the configuration value.
  enum class Type {
    ARRAY, ///< A homogenous array of nested values.
    OBJECT, ///< A set of fields, each a name / value pair.
    INT, ///< Integer value.
    FLOAT, ///< Floating point value.
    STRING, ///< String.
    BOOL,
    ENUM ///< Enumeration (specialized).
  };
/*  TsConfigDescriptor() : type_name(nullptr),name(nullptr),description(nullptr) {}
  TsConfigDescriptor(Type typ,std::initializer_list<std::string> str_list): type(typ)
  {
      for (auto str :str_list) {
                std::cout << str << std::endl;
            }
  }
 * */
  Type type; ///< Value type.
  std::string_view type_name; ///< Literal type name used in the schema.
  std::string_view name; ///< Name of the configuration value.
  std::string_view description; ///< Description of the  value.
};

/** Configuration item instance data.

    This is an abstract base class for data about an instance of the value in a configuration
    struct. Actual instances will be a subclass for a supported configuration item type. This holds
    data that is per instance and therefore must be dynamically constructed as part of the
    configuration struct construction. The related description classes in contrast are data that is
    schema based and therefore can be static and shared among instances of the configuration struct.
*/
class TsConfigBase {
public:
  /// Source of the value in the config struct.
  enum class Source {
    NONE, ///< No source, the value is default constructed.
    SCHEMA, ///< Value set in schema.
    CONFIG ///< Value set in configuration file.
  };
  /// Constructor - need the static descriptor.
  TsConfigBase(TsConfigDescriptor const& d) : descriptor(d) {}
  TsConfigDescriptor const& descriptor; ///< Static schema data.
  Source source = Source::NONE; ///< Where the instance data came from.
  virtual ~TsConfigBase()
  {}
  /// Load the instance data from the Lua stack.
  virtual ts::Errata loader(lua_State* s) = 0;
};

class TsConfigInt : public TsConfigBase {
public:
   TsConfigInt(TsConfigDescriptor const& d, int& i):TsConfigBase(d),ref(i){}
   ts::Errata loader(lua_State* s) override;
private:
   int & ref;
};

class TsConfigBool : public TsConfigBase {
public:
    TsConfigBool(TsConfigDescriptor const& d, bool& i):TsConfigBase(d), ref(i) {}
    ts::Errata loader(lua_State* s) override;
private:
    bool &ref;

};

class TsConfigString : public TsConfigBase {
public:
   TsConfigString(TsConfigDescriptor const& d, std::string& str) : TsConfigBase(d), ref(str) {}
//    TsConfigString& operator= (const TsConfigString& other)
//    {
//        ref = other.ref;
//        return *this;
//    }
   ts::Errata loader(lua_State* s) override;
private:
   std::string& ref;
};



class TsConfigArrayDescriptor : public TsConfigDescriptor {
public:
   TsConfigArrayDescriptor(TsConfigDescriptor const& d) : item(d) {}
   const TsConfigDescriptor& item;
};

class TsConfigEnumDescriptor : public TsConfigDescriptor {
  using self_type = TsConfigEnumDescriptor;
  using super_type = TsConfigDescriptor;
public:
  struct Pair { std::string_view key; int value; };
 TsConfigEnumDescriptor(Type t, std::string_view t_name, std::string_view n, std::string_view d, std::initializer_list<Pair> pairs)
    : super_type{t, t_name, n, d}, values{pairs.size(), &TsLuaConfigSVHash}, keys{pairs.size()}
  {
    for ( auto& p : pairs ) {
      values[p.key] = p.value;
      keys[p.value] = p.key;
    }
  }
  std::unordered_map<std::string_view, int, size_t(*)(std::string_view const&) > values;
  std::unordered_map<int, std::string_view> keys;
  int get(std::string_view key)
  {
      return values[key];
  }
};

class TsConfigObjectDescriptor : public TsConfigDescriptor {
   std::unordered_map<std::string, TsConfigDescriptor const*> fields;
};

template < typename E >
class TsConfigEnum : public TsConfigBase {
public:
   TsConfigEnum(TsConfigEnumDescriptor const& d, int& i) : TsConfigBase(d),edescriptor(d), ref(i) {}
   TsConfigEnumDescriptor edescriptor;
   int& ref;
   ts::Errata loader(lua_State* L) override
   {
    ts::Errata zret;
    std::string key(lua_tostring(L,-1));
    ref = edescriptor.get(std::string_view(key));
    return zret;
    }
};

#endif /* TSCONFIGLUA_H */
