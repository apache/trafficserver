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

#include "tscore/RbTree.h"

namespace ts
{
namespace detail
{
  /// Equality.
  /// @note If @a n is @c NULL it is treated as having the color @c BLACK.
  /// @return @c true if @a c and the color of @a n are the same.
  inline bool
  operator==(RBNode *n, RBNode::Color c)
  {
    return c == (n ? n->getColor() : RBNode::BLACK);
  }
  /// Equality.
  /// @note If @a n is @c NULL it is treated as having the color @c BLACK.
  /// @return @c true if @a c and the color of @a n are the same.
  inline bool
  operator==(RBNode::Color c, RBNode *n)
  {
    return n == c;
  }

  RBNode *
  RBNode::getChild(Direction d) const
  {
    return d == RIGHT ? _right : d == LEFT ? _left : nullptr;
  }

  RBNode *
  RBNode::rotate(Direction d)
  {
    self *parent        = _parent; // Cache because it can change before we use it.
    Direction child_dir = _parent ? _parent->getChildDirection(this) : NONE;
    Direction other_dir = this->flip(d);
    self *child         = this;

    if (d != NONE && this->getChild(other_dir)) {
      child = this->getChild(other_dir);
      this->clearChild(other_dir);
      this->setChild(child->getChild(d), other_dir);
      child->clearChild(d);
      child->setChild(this, d);
      child->structureFixup();
      this->structureFixup();
      if (parent) {
        parent->clearChild(child_dir);
        parent->setChild(child, child_dir);
      } else {
        child->_parent = nullptr;
      }
    }
    return child;
  }

  RBNode *
  RBNode::setChild(self *n, Direction d)
  {
    if (n) {
      n->_parent = this;
    }
    if (d == RIGHT) {
      _right = n;
    } else if (d == LEFT) {
      _left = n;
    }
    return n;
  }

  // Returns the root node
  RBNode *
  RBNode::rippleStructureFixup()
  {
    self *root = this; // last node seen, root node at the end
    self *p    = this;
    while (p) {
      p->structureFixup();
      root = p;
      p    = root->_parent;
    }
    return root;
  }

  void
  RBNode::replaceWith(self *n)
  {
    n->_color = _color;
    if (_parent) {
      Direction d = _parent->getChildDirection(this);
      _parent->setChild(nullptr, d);
      if (_parent != n) {
        _parent->setChild(n, d);
      }
    } else {
      n->_parent = nullptr;
    }
    n->_left = n->_right = nullptr;
    if (_left && _left != n) {
      n->setChild(_left, LEFT);
    }
    if (_right && _right != n) {
      n->setChild(_right, RIGHT);
    }
    _left = _right = nullptr;
  }

  /* Rebalance the tree. This node is the unbalanced node. */
  RBNode *
  RBNode::rebalanceAfterInsert()
  {
    self *x(this); // the node with the imbalance

    while (x && x->_parent == RED) {
      Direction child_dir = NONE;

      if (x->_parent->_parent) {
        child_dir = x->_parent->_parent->getChildDirection(x->_parent);
      } else {
        break;
      }
      Direction other_dir(flip(child_dir));

      self *y = x->_parent->_parent->getChild(other_dir);
      if (y == RED) {
        x->_parent->_color = BLACK;
        y->_color          = BLACK;
        x                  = x->_parent->_parent;
        x->_color          = RED;
      } else {
        if (x->_parent->getChild(other_dir) == x) {
          x = x->_parent;
          x->rotate(child_dir);
        }
        // Note setting the parent color to BLACK causes the loop to exit.
        x->_parent->_color          = BLACK;
        x->_parent->_parent->_color = RED;
        x->_parent->_parent->rotate(other_dir);
      }
    }

    // every node above this one has a subtree structure change,
    // so notify it. serendipitously, this makes it easy to return
    // the new root node.
    self *root   = this->rippleStructureFixup();
    root->_color = BLACK;

    return root;
  }

  // Returns new root node
  RBNode *
  RBNode::remove()
  {
    self *root = nullptr; // new root node, returned to caller

    /*  Handle two special cases first.
        - This is the only node in the tree, return a new root of NIL
        - This is the root node with only one child, return that child as new root
    */
    if (!_parent && !(_left && _right)) {
      if (_left) {
        _left->_parent = nullptr;
        root           = _left;
        root->_color   = BLACK;
      } else if (_right) {
        _right->_parent = nullptr;
        root            = _right;
        root->_color    = BLACK;
      } // else that was the only node, so leave @a root @c NULL.
      return root;
    }

    /*  The node to be removed from the tree.
        If @c this (the target node) has both children, we remove
        its successor, which cannot have a left child and
        put that node in place of the target node. Otherwise this
        node has at most one child, so we can remove it.
        Note that the successor of a node with a right child is always
        a right descendant of the node. Therefore, remove_node
        is an element of the tree rooted at this node.
        Because of the initial special case checks, we know
        that remove_node is @b not the root node.
    */
    self *remove_node(_left && _right ? _right->leftmostDescendant() : this);

    // This is the color of the node physically removed from the tree.
    // Normally this is the color of @a remove_node
    Color remove_color = remove_node->_color;
    // Need to remember the direction from @a remove_node to @a splice_node
    Direction d(NONE);

    // The child node that will be promoted to replace the removed node.
    // The choice of left or right is irrelevant, as remove_node has at
    // most one child (and splice_node may be NIL if remove_node has no
    // children).
    self *splice_node(remove_node->_left ? remove_node->_left : remove_node->_right);

    if (splice_node) {
      // @c replace_with copies color so in this case the actual color
      // lost is that of the splice_node.
      remove_color = splice_node->_color;
      remove_node->replaceWith(splice_node);
    } else {
      // No children on remove node so we can just clip it off the tree
      // We update splice_node to maintain the invariant that it is
      // the node where the physical removal occurred.
      splice_node = remove_node->_parent;
      // Keep @a d up to date.
      d = splice_node->getChildDirection(remove_node);
      splice_node->setChild(nullptr, d);
    }

    // If the node to pull out of the tree isn't this one,
    // then replace this node in the tree with that removed
    // node in liu of copying the data over.
    if (remove_node != this) {
      // Don't leave @a splice_node referring to a removed node
      if (splice_node == this) {
        splice_node = remove_node;
      }
      this->replaceWith(remove_node);
    }

    root         = splice_node->rebalanceAfterRemove(remove_color, d);
    root->_color = BLACK;
    return root;
  }

  /**
   * Rebalance tree after a deletion
   * Called on the spliced in node or its parent, whichever is not NIL.
   * This modifies the tree structure only if @a c is @c BLACK.
   */
  RBNode *
  RBNode::rebalanceAfterRemove(Color c,    //!< The color of the removed node
                               Direction d //!< Direction of removed node from its parent
  )
  {
    self *root;

    if (BLACK == c) { // only rebalance if too much black
      self *n      = this;
      self *parent = n->_parent;

      // If @a direction is set, then we need to start at a leaf pseudo-node.
      // This is why we need @a parent, otherwise we could just use @a n.
      if (NONE != d) {
        parent = n;
        n      = nullptr;
      }

      while (parent) { // @a n is not the root
        // If the current node is RED, we can just recolor and be done
        if (n && n == RED) {
          n->_color = BLACK;
          break;
        } else {
          // Parameterizing the rebalance logic on the directions. We
          // write for the left child case and flip directions for the
          // right child case
          Direction near(LEFT), far(RIGHT);
          if ((NONE == d && parent->getChildDirection(n) == RIGHT) || RIGHT == d) {
            near = RIGHT;
            far  = LEFT;
          }

          self *w = parent->getChild(far); // sibling(n)

          if (w->_color == RED) {
            w->_color      = BLACK;
            parent->_color = RED;
            parent->rotate(near);
            w = parent->getChild(far);
          }

          self *wfc = w->getChild(far);
          if (w->getChild(near) == BLACK && wfc == BLACK) {
            w->_color = RED;
            n         = parent;
            parent    = n->_parent;
            d         = NONE; // Cancel any leaf node logic
          } else {
            if (wfc->_color == BLACK) {
              w->getChild(near)->_color = BLACK;
              w->_color                 = RED;
              w->rotate(far);
              w   = parent->getChild(far);
              wfc = w->getChild(far); // w changed, update far child cache.
            }
            w->_color      = parent->_color;
            parent->_color = BLACK;
            wfc->_color    = BLACK;
            parent->rotate(near);
            break;
          }
        }
      }
    }
    root = this->rippleStructureFixup();
    return root;
  }

  /** Ensure that the local information associated with each node is
      correct globally This should only be called on debug builds as it
      breaks any efficiencies we have gained from our tree structure.
      */
  int
  RBNode::validate()
  {
#if 0
  int black_ht = 0;
  int black_ht1, black_ht2;

  if (_left) {
    black_ht1 = _left->validate();
  }
  else
    black_ht1 = 1;

  if (black_ht1 > 0 && _right)
    black_ht2 = _right->validate();
  else
    black_ht2 = 1;

  if (black_ht1 == black_ht2) {
    black_ht = black_ht1;
    if (this->_color == BLACK)
      ++black_ht;
    else {  // No red-red
      if (_left == RED)
        black_ht = 0;
      else if (_right == RED)
        black_ht = 0;
      if (black_ht == 0)
        std::cout << "Red-red child\n";
    }
  } else {
    std::cout << "Height mismatch " << black_ht1 << " " << black_ht2 << "\n";
  }
  if (black_ht > 0 && !this->structureValidate())
    black_ht = 0;

  return black_ht;
#else
    return 0;
#endif
  }
} // namespace detail
} // namespace ts
