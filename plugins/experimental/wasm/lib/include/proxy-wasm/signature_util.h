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

#pragma once

#include <string_view>

namespace proxy_wasm {

// Utility functions to verify Wasm signatures.
class SignatureUtil {
public:
  /**
   * verifySignature validates Wasm signature.
   * @param bytecode is the source bytecode.
   * @param message is the reference to store the message (success or error).
   * @return indicates whether the bytecode has a valid Wasm signature.
   */
  static bool verifySignature(std::string_view bytecode, std::string &message);
};

} // namespace proxy_wasm
