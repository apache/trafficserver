/** @file

Container for a pending @c Action.

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

/** Hold a pending @c Action.
 *
 * This is modeled on smart pointer classes. This class wraps a pointer to
 * an @c Action (which is also the super type for @c Event). If cleared or
 * re-assigned the current @c Action, if any, is canceled before the pointer is
 * lost to avoid ghost actions triggering after the main continuation is gone.
 *
 * The class is aware of the special value @c ACTION_RESULT_DONE. If that is assigned
 * the pending action will not be canceled or cleared.
 */
class PendingAction
{
  using self_type = PendingAction;

public:
  PendingAction()                  = default;
  PendingAction(self_type const &) = delete;
  ~PendingAction();
  self_type &operator=(self_type const &) = delete;

  /// Check if there is an action.
  /// @return @c true if no action is present, @c false otherwise.
  bool empty() const;

  /** Assign a new @a action.
   *
   * @param action The instance to store.
   * @return @a this
   *
   * Any existing @c Action is canceled.
   * Assignment is thread
   */
  self_type &operator=(Action *action);

  /** Get the @c Continuation for the @c Action.
   *
   * @return A pointer to the continuation if there is an @c Action, @c nullptr if not.
   */
  Continuation *get_continuation() const;

  /** Get the @c Action.
   *
   * @return A pointer to the @c Action is present, @c nullptr if not.
   */
  Action *get() const;

  /** Clear the current @c Action if it is @a action.
   *
   * @param action @c Action to check.
   *
   * @return @c true if the action was cleared, @c false if not.
   *
   * This clears the internal pointer without any side effect. it is used when the @c Action
   * is handled and therefore should no longer be canceled.
   */
  bool clear_if_action_is(Action *action);

private:
  std::atomic<Action *> pending_action = nullptr;
};

inline bool
PendingAction::empty() const
{
  return pending_action == nullptr;
}

inline PendingAction &
PendingAction::operator=(Action *action)
{
  // Apparently @c HttpSM depends on not canceling the previous action if a new
  // one completes immediately. Canceling the contained action in that case
  // cause the @c HttpSM to permanently stall.
  if (ACTION_RESULT_DONE != action) {
    Action *expected; // Need for exchange, and to load @a pending_action only once.
    // Avoid race conditions - for each assigned action, ensure exactly one thread
    // cancels it. Assigning @a expected in the @c while expression avoids potential
    // races if two calls to this method have the same @a action.
    while ((expected = pending_action) != action) {
      if (pending_action.compare_exchange_strong(expected, action)) {
        // This thread did the swap and now no other thread can get @a expected which
        // must be canceled if it's not @c nullptr and then this thread is done.
        if (expected != nullptr) {
          expected->cancel();
        }
        break;
      }
    }
  }
  return *this;
}

inline Continuation *
PendingAction::get_continuation() const
{
  return pending_action ? pending_action.load()->continuation : nullptr;
}

inline Action *
PendingAction::get() const
{
  return pending_action;
}

inline PendingAction::~PendingAction()
{
  if (pending_action) {
    pending_action.load()->cancel();
  }
}

inline bool
PendingAction::clear_if_action_is(Action *action)
{
  if (action != nullptr) {
    while (action == pending_action) {
      if (pending_action.compare_exchange_strong(action, nullptr)) {
        // do NOT cancel - this is called when the event is handled.
        return true;
      }
    }
  }
  return false;
}
