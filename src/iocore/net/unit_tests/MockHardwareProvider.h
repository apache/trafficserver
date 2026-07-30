/** @file

  A minimal OpenSSL 3 provider for unit tests, standing in for an HSM or other
  hardware-backed key store.

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

#pragma once

#include <openssl/provider.h>

#include <string>

namespace MockHardwareProvider
{

// The provider name, which doubles as the URI scheme its store loader claims.
extern char const SCHEME[];

// A key URI this provider resolves, in the shape a hardware module's
// configuration would use.
extern char const URI[];

// Registers and activates the mock provider for the lifetime of the instance,
// and publishes the key material it should yield.
//
// The provider is registered with OSSL_PROVIDER_add_builtin, so everything stays
// in-process and no external module (pkcs11-provider, SoftHSM) is needed.
//
// Loading it takes the shape a real hardware module does: an OSSL_STORE loader
// for its own URI scheme. Real hardware keeps the key on the device and exposes
// only an operation handle; this mock returns ordinary key material instead,
// because what tests need from it is that the key is fetched through the
// provider at all, not that it is unextractable.
class ScopedProvider
{
public:
  // key_pem is the private key, PEM-encoded, that a load of URI will yield.
  explicit ScopedProvider(std::string const &key_pem);
  ScopedProvider(ScopedProvider const &)            = delete;
  ScopedProvider(ScopedProvider &&)                 = delete;
  ScopedProvider &operator=(ScopedProvider const &) = delete;
  ScopedProvider &operator=(ScopedProvider &&)      = delete;
  ~ScopedProvider();

  // Whether registration and activation succeeded. Construction does not throw
  // or assert so that callers can report the failure through their own test
  // framework.
  bool is_loaded() const;

private:
  OSSL_PROVIDER *default_provider{nullptr};
  OSSL_PROVIDER *hw_provider{nullptr};
};

} // namespace MockHardwareProvider
