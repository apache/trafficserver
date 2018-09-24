/** @file

  A brief file description

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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "ExpandingArray.h"

ExpandingArray::ExpandingArray(int initialSize, bool freeContents)
{
  if (initialSize < EA_MIN_SIZE) {
    initialSize = EA_MIN_SIZE;
  }

  internalArray = (void **)ats_malloc(initialSize * sizeof(void *));

  freeContentsOnDestruct = freeContents;
  internalArraySize      = initialSize;
  numValidValues         = 0;
}

ExpandingArray::~ExpandingArray()
{
  if (freeContentsOnDestruct == true) {
    for (int i = 0; i < numValidValues; i++) {
      ats_free(internalArray[i]);
    }
  }
  ats_free(internalArray);
}

void *ExpandingArray::operator[](int index)
{
  if (index < numValidValues) {
    return internalArray[index];
  } else {
    return nullptr;
  }
}

int
ExpandingArray::addEntry(void *entry)
{
  if (numValidValues == internalArraySize) {
    // Time to increase the size of the array
    internalArray = (void **)ats_realloc(internalArray, 2 * sizeof(void *) * internalArraySize);
    internalArraySize *= 2;
  }

  internalArray[numValidValues] = entry;

  return numValidValues++;
}

void
ExpandingArray::sortWithFunction(int(sortFunc)(const void *, const void *))
{
  qsort(internalArray, numValidValues, sizeof(void *), sortFunc);
}
