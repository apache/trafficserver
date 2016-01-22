/**
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

#ifndef METADATA_H_
#define METADATA_H_

#include "webp/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MetadataPayload {
  uint8_t *bytes;
  size_t size;
} MetadataPayload;

typedef struct Metadata {
  MetadataPayload exif;
  MetadataPayload iccp;
  MetadataPayload xmp;
} Metadata;

#define METADATA_OFFSET(x) offsetof(Metadata, x)

void MetadataInit(Metadata *const metadata);
void MetadataPayloadDelete(MetadataPayload *const payload);
void MetadataFree(Metadata *const metadata);

// Stores 'metadata' to 'payload->bytes', returns false on allocation error.
int MetadataCopy(const char *metadata, size_t metadata_len, MetadataPayload *const payload);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // METADATA_H_
