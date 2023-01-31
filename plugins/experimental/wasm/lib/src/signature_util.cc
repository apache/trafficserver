// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "include/proxy-wasm/signature_util.h"

#include <array>
#include <cstring>

#ifdef PROXY_WASM_VERIFY_WITH_ED25519_PUBKEY
#include <openssl/evp.h>
#include <openssl/sha.h>
#endif

#include "include/proxy-wasm/bytecode_util.h"

namespace {

#ifdef PROXY_WASM_VERIFY_WITH_ED25519_PUBKEY

uint8_t hex2dec(const unsigned char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  throw std::logic_error{"invalid hex character"};
}

template <size_t N> constexpr std::array<uint8_t, N> hex2pubkey(const char (&hex)[2 * N + 1]) {
  std::array<uint8_t, N> pubkey{};
  for (size_t i = 0; i < pubkey.size(); i++) {
    pubkey[i] = hex2dec(hex[2 * i]) << 4 | hex2dec(hex[2 * i + 1]);
  }
  return pubkey;
}

#endif

} // namespace

namespace proxy_wasm {

bool SignatureUtil::verifySignature(std::string_view bytecode, std::string &message) {

#ifdef PROXY_WASM_VERIFY_WITH_ED25519_PUBKEY

  /*
   * Ed25519 signature generated using https://github.com/jedisct1/wasmsign
   */

  std::string_view payload;
  if (!BytecodeUtil::getCustomSection(bytecode, "signature_wasmsign", payload)) {
    message = "Failed to parse corrupted Wasm module";
    return false;
  }

  if (payload.empty()) {
    message = "Custom Section \"signature_wasmsign\" not found";
    return false;
  }

  if (bytecode.data() + bytecode.size() != payload.data() + payload.size()) {
    message = "Custom Section \"signature_wasmsign\" not at the end of Wasm module";
    return false;
  }

  if (payload.size() != 68) {
    message = "Signature has a wrong size (want: 68, is: " + std::to_string(payload.size()) + ")";
    return false;
  }

  uint32_t alg_id;
  std::memcpy(&alg_id, payload.data(), sizeof(uint32_t));
  alg_id = wasmtoh(alg_id, true);

  if (alg_id != 2) {
    message = "Signature has a wrong alg_id (want: 2, is: " + std::to_string(alg_id) + ")";
    return false;
  }

  const auto *signature = reinterpret_cast<const uint8_t *>(payload.data()) + sizeof(uint32_t);

  SHA512_CTX ctx;
  SHA512_Init(&ctx);
  SHA512_Update(&ctx, "WasmSignature", sizeof("WasmSignature") - 1);
  const uint32_t ad_len = 0;
  SHA512_Update(&ctx, &ad_len, sizeof(uint32_t));
  const size_t section_len = 3 + sizeof("signature_wasmsign") - 1 + 68;
  SHA512_Update(&ctx, bytecode.data(), bytecode.size() - section_len);
  uint8_t hash[SHA512_DIGEST_LENGTH];
  SHA512_Final(hash, &ctx);

  static const auto ed25519_pubkey = hex2pubkey<32>(PROXY_WASM_VERIFY_WITH_ED25519_PUBKEY);

  EVP_PKEY *pubkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, ed25519_pubkey.data(),
                                                 32 /* ED25519_PUBLIC_KEY_LEN */);
  if (pubkey == nullptr) {
    message = "Failed to load the public key";
    return false;
  }

  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  if (mdctx == nullptr) {
    message = "Failed to allocate memory for EVP_MD_CTX";
    EVP_PKEY_free(pubkey);
    return false;
  }

  bool ok =
      (EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pubkey) != 0) &&
      (EVP_DigestVerify(mdctx, signature, 64 /* ED25519_SIGNATURE_LEN */, hash, sizeof(hash)) != 0);

  EVP_MD_CTX_free(mdctx);
  EVP_PKEY_free(pubkey);

  if (!ok) {
    message = "Signature mismatch";
    return false;
  }

  message = "Wasm signature OK (Ed25519)";
  return true;

#endif

  return true;
}

} // namespace proxy_wasm
