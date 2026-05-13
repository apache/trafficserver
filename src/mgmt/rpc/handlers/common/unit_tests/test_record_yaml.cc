/**
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

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <yaml-cpp/yaml.h>

#include "../convert.h"

namespace
{
//
// Build minimal RecRecord fixtures aimed at the YAML encoder under test in
// place.  RecRecord embeds a RecMutex (with an atomic) and is therefore not
// copyable, so the helpers operate on an output reference and only populate
// the fields the encoder actually reads.  The lock and the unused half of the
// stat_meta / config_meta union stay zero-initialized.
//
void
fill_string_config(RecRecord &r, const char *name, const char *current, const char *def, RecAccessT access)
{
  r.rec_type                = RECT_CONFIG;
  r.data_type               = RECD_STRING;
  r.name                    = name;
  r.data.rec_string         = const_cast<char *>(current);
  r.data_default.rec_string = const_cast<char *>(def);
  r.config_meta.access_type = access;
}

void
fill_int_config(RecRecord &r, const char *name, RecInt current, RecInt def, RecAccessT access)
{
  r.rec_type                = RECT_CONFIG;
  r.data_type               = RECD_INT;
  r.name                    = name;
  r.data.rec_int            = current;
  r.data_default.rec_int    = def;
  r.config_meta.access_type = access;
}

void
fill_int_stat(RecRecord &r, const char *name, RecInt current)
{
  r.rec_type             = RECT_PROCESS; // STAT category
  r.data_type            = RECD_INT;
  r.name                 = name;
  r.data.rec_int         = current;
  r.data_default.rec_int = 0;

  // stat_meta is the active union member after value-initialization of
  // RecRecord; explicitly re-establish that to keep the contract clear
  // and to give the encoder a well-defined object to read.
  r.stat_meta = RecStatMeta{};

  // Plant a RECA_NO_ACCESS bit pattern at the storage location that the
  // overlaid RecConfigMeta would expose as access_type, without making
  // config_meta the active union member.  A hypothetical encoder that
  // reads record.config_meta.access_type without a REC_TYPE_IS_CONFIG
  // guard would observe RECA_NO_ACCESS and incorrectly suppress the
  // value fields below; the well-defined encoder path only reads
  // stat_meta for STAT records.
  static_assert(sizeof(RecStatMeta) >= offsetof(RecConfigMeta, access_type) + sizeof(RecAccessT),
                "RecStatMeta must fully overlap RecConfigMeta::access_type");
  const RecAccessT bad_access = RECA_NO_ACCESS;
  std::memcpy(reinterpret_cast<char *>(&r.stat_meta) + offsetof(RecConfigMeta, access_type), &bad_access, sizeof(bad_access));
}

YAML::Node
encode_record_node(const RecRecord &record)
{
  // The encoder wraps the actual record fields in a {"record": ...} envelope.
  return YAML::convert<RecRecord>::encode(record)[constants_rec::REC];
}
} // namespace

TEST_CASE("Record YAML encoder exposes values for default-access config records", "[mgmt][rpc][record_yaml]")
{
  RecRecord rec{};
  fill_string_config(rec, "proxy.config.example.normal", "current", "default", RECA_NULL);
  const YAML::Node node = encode_record_node(rec);

  REQUIRE(node[constants_rec::DATA_TYPE].as<std::string>() == "STRING");
  REQUIRE(node[constants_rec::CURRENT_VALUE].as<std::string>() == "current");
  REQUIRE(node[constants_rec::DEFAULT_VALUE].as<std::string>() == "default");
}

TEST_CASE("Record YAML encoder withholds values for RECA_NO_ACCESS string records", "[mgmt][rpc][record_yaml][no_access]")
{
  RecRecord rec{};
  fill_string_config(rec, "proxy.config.example.secret", "supersecret", "secret-default", RECA_NO_ACCESS);
  const YAML::Node node = encode_record_node(rec);

  // Type label and metadata are still expected so callers can enumerate the
  // record's existence and tier.
  REQUIRE(node[constants_rec::DATA_TYPE].as<std::string>() == "STRING");
  REQUIRE(node[constants_rec::NAME].as<std::string>() == "proxy.config.example.secret");
  REQUIRE(node[constants_rec::CONFIG_META][constants_rec::ACCESS_TYPE].as<int>() == RECA_NO_ACCESS);

  // The protected value fields must not appear in the encoded output.
  REQUIRE_FALSE(node[constants_rec::CURRENT_VALUE]);
  REQUIRE_FALSE(node[constants_rec::DEFAULT_VALUE]);
}

TEST_CASE("Record YAML encoder withholds values for RECA_NO_ACCESS int records", "[mgmt][rpc][record_yaml][no_access]")
{
  RecRecord rec{};
  fill_int_config(rec, "proxy.config.example.token", 42, 0, RECA_NO_ACCESS);
  const YAML::Node node = encode_record_node(rec);

  REQUIRE(node[constants_rec::DATA_TYPE].as<std::string>() == "INT");
  REQUIRE_FALSE(node[constants_rec::CURRENT_VALUE]);
  REQUIRE_FALSE(node[constants_rec::DEFAULT_VALUE]);
}

TEST_CASE("Record YAML encoder ignores access_type on STAT records", "[mgmt][rpc][record_yaml][union_safety]")
{
  // STAT records do not carry config_meta and must not be filtered by an
  // access_type read out of the wrong half of the union.  This guards the
  // REC_TYPE_IS_CONFIG check that fences the new no-access logic.
  RecRecord rec{};
  fill_int_stat(rec, "proxy.process.example.counter", 7);
  const YAML::Node node = encode_record_node(rec);

  REQUIRE(node[constants_rec::DATA_TYPE].as<std::string>() == "INT");
  REQUIRE(node[constants_rec::CURRENT_VALUE].as<int>() == 7);
  REQUIRE(node[constants_rec::DEFAULT_VALUE].as<int>() == 0);
}
