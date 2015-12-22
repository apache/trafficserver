#ifndef METADATA_H_
#define METADATA_H_

#include "webp/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MetadataPayload {
  uint8_t* bytes;
  size_t size;
} MetadataPayload;

typedef struct Metadata {
  MetadataPayload exif;
  MetadataPayload iccp;
  MetadataPayload xmp;
} Metadata;

#define METADATA_OFFSET(x) offsetof(Metadata, x)

void MetadataInit(Metadata* const metadata);
void MetadataPayloadDelete(MetadataPayload* const payload);
void MetadataFree(Metadata* const metadata);

// Stores 'metadata' to 'payload->bytes', returns false on allocation error.
int MetadataCopy(const char* metadata, size_t metadata_len,
                 MetadataPayload* const payload);

#ifdef __cplusplus
}    // extern "C"
#endif

#endif  // METADATA_H_
