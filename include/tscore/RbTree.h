/** @file

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

namespace ts
{
namespace detail
{
  /** A node in a red/black tree.

      This class provides only the basic tree operations. The client
      must provide the search and decision logic. This enables this
      class to be a base class for templated nodes with much less code
      duplication.
  */
  struct RBNode {
    typedef RBNode self; ///< self reference type

    /// Node colors
    typedef enum {
      RED,
      BLACK,
    } Color;

    /// Directional constants
    typedef enum {
      NONE,
      LEFT,
      RIGHT,
    } Direction;

    /// Get a child by direction.
    /// @return The child in the direction @a d if it exists,
    /// @c NULL if not.
    self *getChild(Direction d //!< The direction of the desired child
    ) const;

    /** Determine which child a node is
        @return @c LEFT if @a n is the left child,
        @c RIGHT if @a n is the right child,
        @c NONE if @a n is not a child
    */
    Direction
    getChildDirection(self *const &n //!< The presumed child node
    ) const
    {
      return (n == _left) ? LEFT : (n == _right) ? RIGHT : NONE;
    }

    /** Get the parent node.
        @return A Node* to the parent node or a @c nil Node* if no parent.
    */
    self *
    getParent() const
    {
      return const_cast<self *>(_parent);
    }

    /// @return The color of the node.
    Color
    getColor() const
    {
      return _color;
    }

    self *
    leftmostDescendant() const
    {
      const self *n = this;
      while (n->_left)
        n = n->_left;

      return const_cast<self *>(n);
    }

    /** Reverse a direction
        @return @c LEFT if @a d is @c RIGHT, @c RIGHT if @a d is @c LEFT,
        @c NONE otherwise.
    */
    Direction
    flip(Direction d)
    {
      return LEFT == d ? RIGHT : RIGHT == d ? LEFT : NONE;
    }

    /** Perform internal validation checks.
        @return 0 on failure, black height of the tree on success.
    */
    int validate();

    /// Default constructor.
    RBNode() {}
    /// Destructor (force virtual).
    virtual ~RBNode() {}
    /** Rotate the subtree rooted at this node.
        The node is rotated in to the position of one of its children.
        Which child is determined by the direction parameter @a d. The
        child in the other direction becomes the new root of the subtree.

        If the parent pointer is set, then the child pointer of the original
        parent is updated so that the tree is left in a consistent state.

        @note If there is no child in the other direction, the rotation
        fails and the original node is returned. It is @b not required
        that a child exist in the direction specified by @a d.

        @return The new root node for the subtree.
    */
    self *rotate(Direction d //!< The direction to rotate
    );

    /** Set the child node in direction @a d to @a n.
        The @a d child is set to the node @a n. The pointers in this
        node and @a n are set correctly. This can only be called if
        there is no child node already present.

        @return @a n.
    */
    self *setChild(self *n,    //!< The node to set as the child
                   Direction d //!< The direction of the child
    );

    /** Remove this node from the tree.
        The tree is rebalanced after removal.
        @return The new root node.
    */
    self *remove();

    void
    clearChild(Direction dir)
    {
      if (LEFT == dir)
        _left = nullptr;
      else if (RIGHT == dir)
        _right = nullptr;
    }

    /** @name Subclass hook methods */
    //@{
    /** Structural change notification.
        This method is called if the structure of the subtree rooted at
        this node was changed.

        This is intended a hook. The base method is empty so that subclasses
        are not required to override.
    */
    virtual void
    structureFixup()
    {
    }

    /** Called from @c validate to perform any additional validation checks.
        Clients should chain this if they wish to perform additional checks.
        @return @c true if the validation is successful, @c false otherwise.
        @note The base method simply returns @c true.
    */
    virtual bool
    structureValidate()
    {
      return true;
    }
    //@}

    /** Replace this node with another node.
        This is presumed to be non-order modifying so the next reference
        is @b not updated.
    */
    void replaceWith(self *n //!< Node to put in place of this node.
    );

    //! Rebalance the tree starting at this node
    /** The tree is rebalanced so that all of the invariants are
        true. The (potentially new) root of the tree is returned.

        @return The root node of the tree after the rebalance.
    */
    self *rebalanceAfterInsert();

    /** Rebalance the tree after a deletion.
        Called on the lowest modified node.
        @return The new root of the tree.
    */
    self *rebalanceAfterRemove(Color c,    //!< The color of the removed node.
                               Direction d //!< Direction of removed node from parent
    );

    //! Invoke @c structure_fixup() on this node and all of its ancestors.
    self *rippleStructureFixup();

    Color _color  = RED;     ///< node color
    self *_parent = nullptr; ///< parent node (needed for rotations)
    self *_left   = nullptr; ///< left child
    self *_right  = nullptr; ///< right child
    self *_next   = nullptr; ///< Next node.
    self *_prev   = nullptr; ///< Previous node.
  };

} /* namespace detail */

} /* namespace ts */
