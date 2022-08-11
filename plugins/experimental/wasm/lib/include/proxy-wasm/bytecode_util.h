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
#include <vector>
#include <unordered_map>

#include "include/proxy-wasm/wasm_vm.h"

namespace proxy_wasm {

// Utilitiy functions which directly operate on Wasm bytecodes.
class BytecodeUtil {
public:
  /**
   * checkWasmHeader validates Wasm header.
   * @param bytecode is the target bytecode.
   * @return indicates whether the bytecode has valid Wasm header.
   */
  static bool checkWasmHeader(std::string_view bytecode);

  /**
   * getAbiVersion extracts ABI version from the bytecode.
   * @param bytecode is the target bytecode.
   * @param ret is the reference to store the extracted ABI version or UnKnown if it doesn't exist.
   * @return indicates whether parsing succeeded or not.
   */
  static bool getAbiVersion(std::string_view bytecode, proxy_wasm::AbiVersion &ret);

  /**
   * getCustomSection extract the view of the custom section for a given name.
   * @param bytecode is the target bytecode.
   * @param name is the name of the custom section.
   * @param ret is the reference to store the resulting view to the custom section.
   * @return indicates whether parsing succeeded or not.
   */
  static bool getCustomSection(std::string_view bytecode, std::string_view name,
                               std::string_view &ret);

  /**
   * getFunctionNameIndex constructs the map from function indexes to function names stored in
   * the function name subsection in "name" custom section.
   * See https://webassembly.github.io/spec/core/appendix/custom.html#binary-funcnamesec for detail.
   * @param bytecode is the target bytecode.
   * @param ret is the reference to store map from function indexes to function names.
   * @return indicates whether parsing succeeded or not.
   */
  static bool getFunctionNameIndex(std::string_view bytecode,
                                   std::unordered_map<uint32_t, std::string> &ret);

  /**
   * getStrippedSource gets Wasm module without Custom Sections to save some memory in workers.
   * @param bytecode is the original bytecode.
   * @param ret is the reference to the stripped bytecode or a copy of the original bytecode.
   * @return indicates whether parsing succeeded or not.
   */
  static bool getStrippedSource(std::string_view bytecode, std::string &ret);

private:
  static bool parseVarint(const char *&pos, const char *end, uint32_t &ret);
};

} // namespace proxy_wasm
