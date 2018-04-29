/** @file

    Command registration.

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

#include <vector>
#include <functional>
#include <string>
#include <iostream>
#include <memory.h>
#include <tsconfig/Errata.h>

#pragma once
namespace ts
{
// Because in C+11 std::max is not constexpr
template <typename I>
constexpr inline I
maximum(I lhs, I rhs)
{
  return lhs < rhs ? rhs : lhs;
}

/// Top level container for commands.
class CommandTable
{
  typedef CommandTable self; ///< Self reference type.
public:
  /// Signature for a leaf command.
  using LeafAction = std::function<ts::Errata(int argc, char *argv[])>;
  /// Signature for a argumentless command.
  using NullaryAction = std::function<ts::Errata()>;

  CommandTable();

  /// A command.
  /// This is either a leaf (and has a function for an implementation) or it is a group
  /// of nested commands.
  class Command
  {
    typedef Command self; ///< Self reference type.
  public:
    Command(Command &&that) = default;
    ~Command();

    /** Add a subcommand to this command.
        @return The subcommand object.
    */
    Command &subCommand(std::string const &name, std::string const &help);
    /** Add a subcommand to this command.
        @return The new sub command instance.
    */
    Command &subCommand(std::string const &name, std::string const &help, NullaryAction const &f);
    /** Add a leaf command.
        @return This new sub command instance.
    */
    Command &subCommand(std::string const &name, std::string const &help, LeafAction const &f);

    /** Invoke a command.
        @return The return value of the executed command, or an error value if the command was not found.
    */
    ts::Errata invoke(int argc, char *argv[]);

    void helpMessage(int argc, char *argv[], std::ostream &out = std::cerr, std::string const &prefix = std::string()) const;

  protected:
    typedef std::vector<Command> CommandGroup;

    std::string _name; ///< Command name.
    std::string _help; ///< Help message.

    /** Class to hold varying types of functions.

        @internal A bit ugly, I need to do better wrapping and type erasure.
    */
    class Action
    {
    public:
      /// Type of the function stored.
      enum Type {
        NIL,     ///< Nothing / empty
        LEAF,    ///< Leaf action (arguments)
        NULLARY, ///< Nullary action.
      };
      Action() {}
      Action(Action &&that)
      {
        _type = that._type;
        memcpy(_data, that._data, sizeof(_data));
        that._type = NIL;
      }
      ~Action() { this->clear(); }

      Action &
      operator=(LeafAction const &a)
      {
        this->clear();
        _type = LEAF;
        new (_data) LeafAction(a);
        return *this;
      }

      Action &
      operator=(NullaryAction const &a)
      {
        this->clear();
        _type = NULLARY;
        new (_data) NullaryAction(a);
        return *this;
      }

      Errata
      invoke(int argc, char *argv[])
      {
        assert(LEAF == _type);
        return (*reinterpret_cast<LeafAction *>(_data))(argc, argv);
      }

      Errata
      invoke()
      {
        assert(NULLARY == _type);
        return (*reinterpret_cast<NullaryAction *>(_data))();
      }

      bool
      is_leaf() const
      {
        return LEAF == _type;
      }
      bool
      is_nullary() const
      {
        return NULLARY == _type;
      }

    protected:
      void
      clear()
      {
        switch (_type) {
        case NIL:
          break;
        case LEAF:
          reinterpret_cast<LeafAction *>(_data)->~LeafAction();
          break;
        case NULLARY:
          reinterpret_cast<NullaryAction *>(_data)->~NullaryAction();
          break;
        }
        _type = NIL;
      }

      Type _type = NIL; ///< Type of function stored.
      /// Raw storage for the function.
      char _data[sizeof(NullaryAction) > sizeof(LeafAction) ? sizeof(NullaryAction) : sizeof(LeafAction)];
    } _action;

    /// Next command for current keyword.
    CommandGroup _group;

    /// Default constructor, no execution logic.
    Command();
    /// Construct with a function for this command.
    Command(std::string const &name, std::string const &help);
    /// Construct with a function for this command.
    Command(std::string const &name, std::string const &help, LeafAction const &f);
    /// Construct with a function for this command.
    Command(std::string const &name, std::string const &help, NullaryAction const &f);

    friend class CommandTable;
  };

  /** Add a direct command.
      @return The created @c Command instance.
   */
  Command &add(std::string const &name, std::string const &help, LeafAction const &f);

  /** Add a direct command.
      @return The created @c Command instance.
   */
  Command &add(std::string const &name, std::string const &help, NullaryAction const &f);

  /** Add a parent command.
      @return The created @c Command instance.
  */
  Command &add(std::string const &name, std::string const &help);

  /** Set the index of the "first" argument.
      This causes the command processing to skip @a n arguments.
  */
  self &setArgIndex(int n);

  /** Invoke a command.
      @return The return value of the executed command, or an error value if the command was not found.
  */
  ts::Errata invoke(int argc, char *argv[]);

  void helpMessage(int argc, char *argv[]) const;

protected:
  Command _top;
  static int _opt_idx;

  friend class Command;
};

inline CommandTable &
CommandTable::setArgIndex(int n)
{
  _opt_idx = n;
  return *this;
}
} // namespace ts
