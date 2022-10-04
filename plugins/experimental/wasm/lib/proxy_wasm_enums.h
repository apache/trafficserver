/*
 * Copyright 2016-2019 Envoy Project Authors
 * Copyright 2020 Google LLC
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

/*
 * Intrinsic enumerations available to WASM modules.
 */
// NOLINT(namespace-envoy)
#pragma once

#include <cstdint>

enum class LogLevel : int32_t { trace, debug, info, warn, error, critical, Max = critical };
enum class FilterStatus : int32_t { Continue = 0, StopIteration = 1 };
enum class FilterHeadersStatus : int32_t {
  Continue = 0,
  StopIteration = 1,
  ContinueAndEndStream = 2,
  StopAllIterationAndBuffer = 3,
  StopAllIterationAndWatermark = 4,
};
enum class FilterMetadataStatus : int32_t { Continue = 0 };
enum class FilterTrailersStatus : int32_t { Continue = 0, StopIteration = 1 };
enum class FilterDataStatus : int32_t {
  Continue = 0,
  StopIterationAndBuffer = 1,
  StopIterationAndWatermark = 2,
  StopIterationNoBuffer = 3
};
enum class GrpcStatus : int32_t {
  Ok = 0,
  Canceled = 1,
  Unknown = 2,
  InvalidArgument = 3,
  DeadlineExceeded = 4,
  NotFound = 5,
  AlreadyExists = 6,
  PermissionDenied = 7,
  ResourceExhausted = 8,
  FailedPrecondition = 9,
  Aborted = 10,
  OutOfRange = 11,
  Unimplemented = 12,
  Internal = 13,
  Unavailable = 14,
  DataLoss = 15,
  Unauthenticated = 16,
  MaximumValid = Unauthenticated,
  InvalidCode = -1
};
enum class MetricType : int32_t {
  Counter = 0,
  Gauge = 1,
  Histogram = 2,
  Max = 2,
};
enum class CloseType : int32_t {
  Unknown = 0,
  Local = 1,  // Close initiated by the proxy.
  Remote = 2, // Close initiated by the peer.
};
