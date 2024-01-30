.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: txnbox_common.defs

***************
Transaction Box
***************

.. important::

   This is an experimental plugin and it should be build by using `-DBUILD_EXPERIMENTAL_PLUGINS=YES`.


Transaction Box, or |TxB|, is an Apache Traffic Server plugin to manipulate
:term:`transaction`\s. The functionality is based on requests I have received over the years from
users and admnistrators for |TS|. The primary points of interest are

*  YAML based configuration.
*  Smooth interaction between global and remap hooks.
*  Consistent access to data, a single way to access data usable in all circumstances.

|TxB| is designed as a very general plugin which can replace a number of other plugins. It is also
intended, in the long run, to replace "remap.config".

.. toctree::
   :maxdepth: 2

   txn_box.en
   building.en
   install.en
   expr.en.rst
   directive.en
   selection.en
   guide.en
   examples.en
   arch.en
   user/ExtractorReference.en
   user/DirectiveReference.en
   user/ComparisonReference.en
   user/ModifierReference.en
   future.en
   misc.en
   dev/dev-guide.en
   dev/acceleration.en

Reference
*********

.. toctree::
   :maxdepth: 1

   reference.en
