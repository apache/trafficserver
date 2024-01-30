.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: /common.defs

.. highlight:: yaml

.. _memory-management:

****************
Memory Mangement
****************

While most elements do not require additional memory, this is not always the case. If additional memory is needed to
perform a task, this should be allocated in |TxB| memory that has a transaction based lifetime for faster allocation and
automated cleanup. If elements in a constellation need to share data, the data must be placed in |TxB| managed memory to
avoid corrouption or crashes on configuration reload.

|TxB| provides a number of mechanisms for allocating memory efficiently for configuration and transaction
lifetimes.

=====================
Configuration Storage
=====================

The root of memomry management is the configuration instance, represented by an instance of
:txb:`Config`. This contains a memory arena with the same lifetime as the configuration and is used to store
configuration related data. Elements can also allocate memory in this arena. Such memory has two useful
properties.

*  The memory lifetime is the same as the configuration instance.
*  The memory is accessible from any element.

Raw configuration memory can be allocated as described later but this leaves the problem of locating that memory
again, with the self referential problem of having to know the location of the memory in the configuration to
find the location. The method :txb:`Config::obtain_named_object` provides a bootstrapping object in configuration.
This finds or creates an instance of a specific type in configuration memory and associates it with a name. The
method :txb:`Config::named_object` can be used later to find that object.

The most challenging aspect is finding configuration allocated memory later. If the configuration
based memory is used per directive instance, this is not a problem - a span can be stored in the
directive instance. But if the memory is to be shared across instances more is required because
otherwise the instances can't find the same memory. This is the problem the :code:`_rtti`
indirection solves.

Note - directive instances are per configuration which means invocations are multi-threaded. It is
entirely possible to have the same directive instance being invoked simultaneously for different
transactions. If the requirement is to set up shared status this can be done via the configuration
initializer argument to :txb:`Config::define`. If the templated overload is used then the method
:txb:`Directive::cfg_init` is used as the initializer. The base class method does nothing therefore
this method can be omitted if not needed.

When configuration storage is needed, it is frequently the case this is because the directive needs
to share state with extractors or modifiers. These can access the directive configuration storage by
using the :txb:`Config::drtv_info` method with the name of the directive to get the configuration
static information for the directive, which includes the reserved configuration memory.

In some cases the configuration allocated memory will need additional cleanup beyond simply being
released. This can be done via :txb:`Config::mark_for_cleanup`. This takes a pointer and destructs
the object using :code:`std::destroy_at<T>` just before the configuration memory is released during
config destruction, where :code:`T` is the type of the pointer passed to
:txb:`Config::mark_for_cleanup`.

===============
Context Storage
===============

Elements can request storage local to a transaction context, represented by :txb:`Context`. This
memory is much faster to acquire than standard :code:`malloc` but will be released when the
transaction ends. For many uses the latter is a benefit, not a cost, and in such cases context
memory should be used. Note the context memory is released only at context destruction after the
transaction finishes, it cannot be released at any other time. Abandoned memory isn't leaked, it is
cleaned up along with all of the context local memory.

Simple allocation is done with :txb:`Context::alloc_span` which allocates sufficient memory to
hold an array of the specified type and count. This is raw memory - no initialization is done. If
that is necessary it could be done as ::

   auto span = ctx.alloc_span<Alpha>(count); // get space for @a count instances of @c Alpha
   span.apply([](Alpha &alpha) -> void { new (&alpha) Alpha; });

If cleanup is needed the same mechanism can be used to invoke the destructor on the elements. ::

   span.apply([](Alpha &alpha) -> void { std::destroy_at(&alpha); });

This is necessary only when there are references to memory or stateful objects outside of the
context. Generally this memory should reference nothing, or only other context memory in which case
no clean up is needed. For instance, the most common use is as string storage which needs no
cleanup.

If the context allocation needs to be shared or accessed from different hooks, this is a bit more
challenging. A pointer can't be stored directly in the element instance because it would be
different for every transaction creating a self-dependency loop where to find the memory the pointer
needs to be found which is in the memory ...

To break this loop memory in the context can be reserved and present in every context at the same
offset in context memory. Information about this is stored in an instance of :txb:`ReservedSpan`
which is not a memory span but an offset and length which can be converted to a memory span in a
specific context using :txb:`Context::storage_for`. The reserved span can be stored in a class
member if every instance needs access to the memory, or in configuration reserved storage if
different elements need to share the same context memory. In contrast to directly allocated context
memory, reserved context memory is zero initialized to enable simple initialization checking by
different methods in an element or different elements entirely. :txb:`Context::storage_for` does
nothing further, it simply converts the offset and size to a span inside the context instance.

If the memory needs to be initialized beyond being zero initialized, it could be difficult to
determine when exactly the initalization should be done. To deal with this the method
:txb:`Context::initialized_storage_for` method is provided. The context tracks whether this method
has been called for a specific context and reserved span and if not, the constructor for the span
type is invoked on the span. This is done exactly as above, the difference being the memory is
constructed in place at most once for each context. Therefore different elements can all call this
method with the guarantee only the first one invoked for a transaction will initialize the span.
