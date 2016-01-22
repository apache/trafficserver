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

#include <stdlib.h>
#include <string.h>

#include "webp/types.h"
#include "metadata.h"

void
MetadataInit(Metadata *const metadata)
{
  if (metadata == NULL)
    return;
  memset(metadata, 0, sizeof(*metadata));
}

void
MetadataPayloadDelete(MetadataPayload *const payload)
{
  if (payload == NULL)
    return;
  free(payload->bytes);
  payload->bytes = NULL;
  payload->size = 0;
}

void
MetadataFree(Metadata *const metadata)
{
  if (metadata == NULL)
    return;
  MetadataPayloadDelete(&metadata->exif);
  MetadataPayloadDelete(&metadata->iccp);
  MetadataPayloadDelete(&metadata->xmp);
}

int
MetadataCopy(const char *metadata, size_t metadata_len, MetadataPayload *const payload)
{
  if (metadata == NULL || metadata_len == 0 || payload == NULL)
    return 0;
  payload->bytes = (uint8_t *)malloc(metadata_len);
  if (payload->bytes == NULL)
    return 0;
  payload->size = metadata_len;
  memcpy(payload->bytes, metadata, metadata_len);
  return 1;
}
