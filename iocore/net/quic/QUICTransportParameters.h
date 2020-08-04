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
#include <tscore/ink_memory.h>

#include <openssl/ssl.h>
#include "QUICTypes.h"
#include <cstddef>

class QUICTransportParameterId
{
public:
  enum {
    ORIGINAL_DESTINATION_CONNECTION_ID,
    MAX_IDLE_TIMEOUT,
    STATELESS_RESET_TOKEN,
    MAX_UDP_PAYLOAD_SIZE,
    INITIAL_MAX_DATA,
    INITIAL_MAX_STREAM_DATA_BIDI_LOCAL,
    INITIAL_MAX_STREAM_DATA_BIDI_REMOTE,
    INITIAL_MAX_STREAM_DATA_UNI,
    INITIAL_MAX_STREAMS_BIDI,
    INITIAL_MAX_STREAMS_UNI,
    ACK_DELAY_EXPONENT,
    MAX_ACK_DELAY,
    DISABLE_ACTIVE_MIGRATION,
    PREFERRED_ADDRESS,
    ACTIVE_CONNECTION_ID_LIMIT,
    INITIAL_SOURCE_CONNECTION_ID,
    RETRY_SOURCE_CONNECTION_ID,
  };

  explicit operator bool() const { return true; }
  bool
  operator==(const QUICTransportParameterId &x) const
  {
    return this->_id == x._id;
  }

  bool
  operator==(const uint16_t &x) const
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
  QUICTransportParameters(const uint8_t *buf, size_t len, QUICVersion version);
  virtual ~QUICTransportParameters();

  bool is_valid() const;

  const uint8_t *getAsBytes(QUICTransportParameterId id, uint16_t &len) const;
  uint64_t getAsUInt(QUICTransportParameterId id) const;
  bool contains(QUICTransportParameterId id) const;

  void set(QUICTransportParameterId id, const uint8_t *value, uint16_t value_len);
  void set(QUICTransportParameterId id, uint64_t value);

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
  void _load(const uint8_t *buf, size_t len, QUICVersion version);
  bool _valid = false;

  virtual std::ptrdiff_t _parameters_offset(const uint8_t *buf) const = 0;
  virtual int _validate_parameters(QUICVersion version) const;
  void _print() const;

  std::map<QUICTransportParameterId, Value *> _parameters;
};

class QUICTransportParametersInClientHello : public QUICTransportParameters
{
public:
  QUICTransportParametersInClientHello() : QUICTransportParameters(){};
  QUICTransportParametersInClientHello(const uint8_t *buf, size_t len, QUICVersion version);

protected:
  std::ptrdiff_t _parameters_offset(const uint8_t *buf) const override;
  int _validate_parameters(QUICVersion version) const override;

private:
};

class QUICTransportParametersInEncryptedExtensions : public QUICTransportParameters
{
public:
  QUICTransportParametersInEncryptedExtensions() : QUICTransportParameters(){};
  QUICTransportParametersInEncryptedExtensions(const uint8_t *buf, size_t len, QUICVersion version);

protected:
  std::ptrdiff_t _parameters_offset(const uint8_t *buf) const override;
  int _validate_parameters(QUICVersion version) const override;
};

class QUICTransportParametersHandler
{
public:
  static constexpr int TRANSPORT_PARAMETER_ID = 0xffa5;

  static int add(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char **out, size_t *outlen, X509 *x,
                 size_t chainidx, int *al, void *add_arg);
  static void free(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char *out, void *add_arg);
  static int parse(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char *in, size_t inlen, X509 *x,
                   size_t chainidx, int *al, void *parse_arg);
};
