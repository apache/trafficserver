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
#include <ts/ink_memory.h>

#include <openssl/ssl.h>
#include "QUICTypes.h"
#include <cstddef>

class QUICTransportParameterId
{
public:
  enum {
    INITIAL_MAX_STREAM_DATA = 0,
    INITIAL_MAX_DATA,
    INITIAL_MAX_STREAM_ID_BIDI,
    IDLE_TIMEOUT,
    OMIT_CONNECTION_ID,
    MAX_PACKET_SIZE,
    STATELESS_RESET_TOKEN,
    ACK_DELAY_EXPONENT,
    INITIAL_MAX_STREAM_ID_UNI
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

class QUICTransportParameters
{
public:
  QUICTransportParameters(const uint8_t *buf, size_t len);
  ~QUICTransportParameters();

  bool is_valid() const;

  const uint8_t *getAsBytes(QUICTransportParameterId id, uint16_t &len) const;
  uint8_t getAsUInt8(QUICTransportParameterId id) const;
  uint16_t getAsUInt16(QUICTransportParameterId id) const;
  uint32_t getAsUInt32(QUICTransportParameterId id) const;

  void set(QUICTransportParameterId id, const uint8_t *value, uint16_t value_len);
  void set(QUICTransportParameterId id, uint16_t value);
  void set(QUICTransportParameterId id, uint32_t value);

  void store(uint8_t *buf, uint16_t *len) const;

protected:
  class Value
  {
  public:
    Value(const uint8_t *data, uint16_t len);
    ~Value();
    const uint8_t *data() const;
    uint16_t len() const;

  private:
    uint8_t *_data = nullptr;
    uint16_t _len  = 0;
  };

  QUICTransportParameters(){};
  void _load(const uint8_t *buf, size_t len);
  bool _valid = false;

  virtual std::ptrdiff_t _parameters_offset(const uint8_t *buf) const = 0;
  virtual int _validate_parameters() const;
  virtual void _store(uint8_t *buf, uint16_t *len) const = 0;
  void _print() const;

  std::map<QUICTransportParameterId, Value *> _parameters;
};

class QUICTransportParametersInClientHello : public QUICTransportParameters
{
public:
  QUICTransportParametersInClientHello(QUICVersion initial_version)
    : QUICTransportParameters(), _initial_version(initial_version){};
  QUICTransportParametersInClientHello(const uint8_t *buf, size_t len);
  QUICVersion initial_version() const;

protected:
  std::ptrdiff_t _parameters_offset(const uint8_t *buf) const override;
  int _validate_parameters() const override;
  void _store(uint8_t *buf, uint16_t *len) const override;

private:
  QUICVersion _initial_version = 0;
};

class QUICTransportParametersInEncryptedExtensions : public QUICTransportParameters
{
public:
  QUICTransportParametersInEncryptedExtensions(QUICVersion negotiated_version)
    : QUICTransportParameters(), _negotiated_version(negotiated_version){};
  QUICTransportParametersInEncryptedExtensions(const uint8_t *buf, size_t len);
  QUICVersion negotiated_version() const;
  void add_version(QUICVersion version);

protected:
  std::ptrdiff_t _parameters_offset(const uint8_t *buf) const override;
  int _validate_parameters() const override;
  void _store(uint8_t *buf, uint16_t *len) const override;

  QUICVersion _negotiated_version = 0;
  uint8_t _n_versions             = 0;
  QUICVersion _versions[256]      = {};
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
