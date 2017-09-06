/** @file
 *
 *  A brief file description
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

#pragma once

#include <map>
// #include "ts/Map.h"

#include <openssl/ssl.h>
#include "QUICTypes.h"
#include <cstddef>

class QUICTransportParameterId
{
public:
  enum {
    INITIAL_MAX_STREAM_DATA = 0,
    INITIAL_MAX_DATA,
    INITIAL_MAX_STREAM_ID,
    IDLE_TIMEOUT,
    TRUNCATE_CONNECTION_ID,
    MAX_PACKET_SIZE,
    STATELESS_RETRY_TOKEN,
  };

  explicit operator bool() const { return true; }
  bool
  operator==(const QUICTransportParameterId &x) const
  {
    return this->_id == x._id;
  }

  bool
  operator==(uint16_t &x) const
  {
    return this->_id == x;
  }

  operator uint16_t() const { return _id; };
  QUICTransportParameterId() : _id(0){};
  QUICTransportParameterId(uint16_t id) : _id(id){};

private:
  uint16_t _id = 0;
};

class QUICTransportParameterValue
{
public:
  QUICTransportParameterValue(ats_unique_buf d, uint16_t l);
  QUICTransportParameterValue(uint64_t raw_data, uint16_t l);
  QUICTransportParameterValue(uint64_t raw_data[2], uint16_t l);

  const uint8_t *data() const;
  uint16_t len() const;

private:
  ats_unique_buf _data = {nullptr, [](void *p) { ats_free(p); }};
  uint16_t _len        = 0;
};

class QUICTransportParameters
{
public:
  QUICTransportParameters(const uint8_t *buf, size_t len);
  const uint8_t *get(QUICTransportParameterId id, uint16_t &len) const;
  uint32_t initial_max_stream_data() const;
  uint32_t initial_max_data() const;
  void add(QUICTransportParameterId id, std::unique_ptr<QUICTransportParameterValue> value);
  void store(uint8_t *buf, uint16_t *len) const;

protected:
  QUICTransportParameters(){};
  virtual std::ptrdiff_t _parameters_offset() const = 0;
  virtual void _store(uint8_t *buf, uint16_t *len) const = 0;
  ats_unique_buf _buf = {nullptr, [](void *p) { ats_free(p); }};

  std::map<QUICTransportParameterId, std::unique_ptr<QUICTransportParameterValue>> _parameters;
};

class QUICTransportParametersInClientHello : public QUICTransportParameters
{
public:
  QUICTransportParametersInClientHello(QUICVersion negotiated_version, QUICVersion initial_version)
    : QUICTransportParameters(), _negotiated_version(negotiated_version), _initial_version(initial_version){};
  QUICTransportParametersInClientHello(const uint8_t *buf, size_t len);
  QUICVersion negotiated_version() const;
  QUICVersion initial_version() const;

protected:
  std::ptrdiff_t _parameters_offset() const override;
  void _store(uint8_t *buf, uint16_t *len) const override;

private:
  QUICVersion _negotiated_version = 0;
  QUICVersion _initial_version    = 0;
};

class QUICTransportParametersInEncryptedExtensions : public QUICTransportParameters
{
public:
  QUICTransportParametersInEncryptedExtensions() : QUICTransportParameters(){};
  QUICTransportParametersInEncryptedExtensions(const uint8_t *buf, size_t len) : QUICTransportParameters(buf, len){};
  const uint8_t *supported_versions_len(uint16_t *n) const;
  void add_version(QUICVersion version);

protected:
  std::ptrdiff_t _parameters_offset() const override;
  void _store(uint8_t *buf, uint16_t *len) const override;

  uint8_t _n_versions        = 0;
  QUICVersion _versions[256] = {};
};

class QUICTransportParametersHandler
{
public:
  static constexpr int TRANSPORT_PARAMETER_ID = 26;

  static int add(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char **out, size_t *outlen, X509 *x,
                 size_t chainidx, int *al, void *add_arg);

  static void free(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char *out, void *add_arg);

  static int parse(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char *in, size_t inlen, X509 *x,
                   size_t chainidx, int *al, void *parse_arg);
};
