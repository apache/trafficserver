/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// Wasm memory page is always 64 KiB.
#define PROXY_WASM_HOST_WASM_MEMORY_PAGE_SIZE_BYTES (64 * 1024)

// Maximum allowed Wasm memory size.
#ifndef PROXY_WASM_HOST_MAX_WASM_MEMORY_SIZE_BYTES
#define PROXY_WASM_HOST_MAX_WASM_MEMORY_SIZE_BYTES (1024 * 1024 * 1024)
#endif

// Maximum allowed random_get buffer size. This value is consistent with
// the JavaScript Crypto.getRandomValues() maximum buffer size.
// See: https://w3c.github.io/webcrypto/#Crypto-method-getRandomValues
#ifndef PROXY_WASM_HOST_WASI_RANDOM_GET_MAX_SIZE_BYTES
#define PROXY_WASM_HOST_WASI_RANDOM_GET_MAX_SIZE_BYTES (64 * 1024)
#endif

// Maximum allowed size of Pairs buffer to deserialize.
#ifndef PROXY_WASM_HOST_PAIRS_MAX_BYTES
#define PROXY_WASM_HOST_PAIRS_MAX_BYTES (1024 * 1024)
#endif

// Maximum allowed number of pairs in a Pairs buffer to deserialize.
#ifndef PROXY_WASM_HOST_PAIRS_MAX_COUNT
#define PROXY_WASM_HOST_PAIRS_MAX_COUNT 1024
#endif
