/** @file

  Radix sort

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

  @section details Details

  Part of the utils library which contains classes that use multiple
  components of the IO-Core to implement some useful functionality. The
  classes also serve as good examples of how to use the IO-Core.

 */

#ifndef _I_RADIXSORT_H_
#define _I_RADIXSORT_H_

/** Simple radix sort class. */
class RadixSortImpl;

class RadixSort
{
public:

  /**
    @param elem_size each element is N bytes of data to be sorted.
    @param n_elems max # of elements that can be sorted.

  */
  RadixSort(int elem_size, int n_elems);
   ~RadixSort();

  /** Get back to empty state. */
  void init();

  /** First pass of radix sort is performed when element is initially added. */
  void add(char *elembytes);

  /** Perform remaining sort. */
  void sort();

  /** Start iterating through sorted elements. */
  void iterStart();

  /**
    @param elemdata should be of size elem_size.
    @return not 0 if element is valid, or 0 if empty.
  */
  int iter(char *elemdata);

  /** Debugging. */
  void dumpBuckets();
private:
    RadixSortImpl * m_impl;
};

#endif
