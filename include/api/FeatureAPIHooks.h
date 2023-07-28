/** @file

  Internal SDK stuff

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

#pragma once

#include "APIHook.h"
#include "APIHooks.h"

#include "ts/InkAPIPrivateIOCore.h"

#include "tscore/ink_apidefs.h"

/** Container for API hooks for a specific feature.

    This is an array of hook lists, each identified by a numeric identifier (id). Each array element is a list of all
    hooks for that ID. Adding a hook means adding to the list in the corresponding array element. There is no provision
    for removing a hook.

    @note The minimum value for a hook ID is zero. Therefore the template parameter @a N_ID should be one more than the
    maximum hook ID so the valid ids are 0..(N-1) in the standard C array style.
 */
template <typename ID, ///< Type of hook ID
          int N        ///< Number of hooks
          >
class FeatureAPIHooks
{
public:
  FeatureAPIHooks();  ///< Constructor (empty container).
  ~FeatureAPIHooks(); ///< Destructor.

  /// Remove all hooks.
  void clear();
  /// Add the hook @a cont to the end of the hooks for @a id.
  void append(ID id, INKContInternal *cont);
  /// Get the list of hooks for @a id.
  APIHook *get(ID id) const;
  /// @return @c true if @a id is a valid id, @c false otherwise.
  static bool is_valid(ID id);

  /// Invoke the callbacks for the hook @a id.
  void invoke(ID id, int event, void *data);

  /// Fast check for any hooks in this container.
  ///
  /// @return @c true if any list has at least one hook, @c false if
  /// all lists have no hooks.
  bool has_hooks() const;

  /// Check for existence of hooks of a specific @a id.
  /// @return @c true if any hooks of type @a id are present.
  bool has_hooks_for(ID id) const;

  /// Get a pointer to the set of hooks for a specific hook @id
  APIHooks const *operator[](ID id) const;

private:
  bool m_hooks_p = false; ///< Flag for (not) empty container.
  /// The array of hooks lists.
  APIHooks m_hooks[N];
};

template <typename ID, int N> FeatureAPIHooks<ID, N>::FeatureAPIHooks() {}

template <typename ID, int N> FeatureAPIHooks<ID, N>::~FeatureAPIHooks()
{
  this->clear();
}

// The APIHooks::clear() method can't be inlined (easily), and we end up calling
// clear() very frequently (it's used in a number of features). A rough estimate
// is that we may call APIHooks::clear() as much as 230x per transaction (there's
// 180 additional APIHooks that should be eliminated in a different PR). This
// code at least avoids calling this function for a majority of the cases.
// Before this code, APIHooks::clear() would show up as top 5 in perf top.
template <typename ID, int N>
void
FeatureAPIHooks<ID, N>::clear()
{
  if (m_hooks_p) {
    for (auto &h : m_hooks) {
      if (!h.is_empty()) {
        h.clear();
      }
    }
    m_hooks_p = false;
  }
}

template <typename ID, int N>
void
FeatureAPIHooks<ID, N>::append(ID id, INKContInternal *cont)
{
  if (is_valid(id)) {
    m_hooks_p = true;
    m_hooks[id].append(cont);
  }
}

template <typename ID, int N>
APIHook *
FeatureAPIHooks<ID, N>::get(ID id) const
{
  return likely(is_valid(id)) ? m_hooks[id].head() : nullptr;
}

template <typename ID, int N>
APIHooks const *
FeatureAPIHooks<ID, N>::operator[](ID id) const
{
  return likely(is_valid(id)) ? &(m_hooks[id]) : nullptr;
}

template <typename ID, int N>
void
FeatureAPIHooks<ID, N>::invoke(ID id, int event, void *data)
{
  if (likely(is_valid(id))) {
    m_hooks[id].invoke(event, data);
  }
}

template <typename ID, int N>
bool
FeatureAPIHooks<ID, N>::has_hooks() const
{
  return m_hooks_p;
}

template <typename ID, int N>
bool
FeatureAPIHooks<ID, N>::is_valid(ID id)
{
  return 0 <= id && id < N;
}
