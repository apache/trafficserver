.. Copyright 2022, Alan M. Carroll
   SPDX-License-Identifier: Apache-2.0

.. include:: ../../../../common.defs
.. include:: ../txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _acceleration:

***********************
Comparison Acceleration
***********************

This is a design document about accelerating multiple comparisons, primarily string comparisons.

Background
**********

For basic usage linear processing of comparison is adequate. For common production use, however,
this is infeasible for perforamnce reasons. There are commonly multiple thousands of comparisons
against the same value. The cost of this should be proporational to the size of the value not the
number of comparisons although the *result* must be as if a linear search was done. The data
structures needed for this depend on the nature of the comparisons. For string comparisons that
consist of exact, prefix, and suffix matching, the best structure is a trie. However this is
complicated by the nature of |TxB| comparisons which allow "combination" comparisons that combine
other comparisons. A single trie does not suffice and additional data structures are required.

Overall Design
**************

For the purposes of acceleration comparisons are divided in to three basic types.

*  Primitives (exact, prefix, suffix)
*  Combinations
*  Other (non-accelerating)

Primitives are put in to the trie. Combinations are put in auxillary structures. Any combination comparison
is ultimately dependent upon primitives. The auxillary structures are designed to allows matches from
walking the trie to "roll up" in to the containing combinations. The simplest example is a
:cmp:`any-of` containing a set of exact matches. If any of those are matched in the trie
the combination can also be marked as matched immediately without further search.

Even in the case where a combination cannot be immediately marked as matched we still want to be
able to skip any primitives by tracking if those were matched during the trie search.

To keep the apparent linear ordering every comparison has a rank which is simply its place in the
comparison order. If a comparison matches that means all other comparisons of lesser (numerically
greater) rank can be ignored. This is useful in several places but most signficantlly for "other" comparisons.
These cannot be accelerated but the evaluation can be improved in many cases by stopping the evaluation
once the rank becomes less than an already matched accelerated comparison. E.g. if a comparison of
rank 14 is matched by the trie, only non-accelerated comparisons of rank 13 or better must be checked.
Combination comparisons can also take advantage of this in that results from the trie search can
determine whether the combination matches regardless of the result of the non-accelerated matches.
For example, an "all-of" comparison which has any failed matches among the accelerated comparisons can be
discarded without further evaluation.
