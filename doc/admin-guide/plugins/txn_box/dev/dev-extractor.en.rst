.. Copyright 2022, Alan M. Carroll
   SPDX-License-Identifier: Apache-2.0
.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _dev-extractor:

Extractor Development
*********************

Extractors are referenced by feature expressions. This means every extractor must be able to output
to a string, and may optionally provide typed data.

Unlike other elements, use of an extractor involves referencing a global instance, rather than
instantiating an instance per use. This is because

*  Extractors are used far more frequently.
*  Most extractors do not require any local storage or state.

All extractors are implemented by a class. This must be a subclass of :txb:`Extractor`. By
convention the name of the class should be "Ex\_" followed by the extractor name. For example the
class :code:`Ex_ua_req_url` is the implementation of the "ua-req-url" extractor.

By convention, a :code:`TextView` named :code:`NAME` is declared to define the name of the
extractor. This isn't required, the name is defined by the registration call, but it's convenient.

There are several methods that are needed to be fully functional. Several of them take a
:txb:`Extractor::Spec` parameter. For any specific use of an extractor there is a single instance of
this class which is passed to all methods of the extractor. In some sense, this represents the
per use instance data. This class is a subclass of the BufferWriter specifier to provide additional
members. These are

:code:`_exf`
   A pointer to the extractor instance. This is used to call the extractor during feature extraction.

:code:`_name`
   The name of the extractor used in the feature expression.

:code:`_data`
   A memory span which is by default empty. It can be used to store per instance data if needed as
   described below in the examples.

Required Methods
================

.. code-block:: cpp

   swoc::Rv<ActiveType> validate(Config & cfg, Spec & spec, swoc::TextView const& arg);

This is called during configuration loading when the extractor is parsed. It is expected to do two things -

*  Validate the argument if any.
*  Indicate the return type.

If the extractor can only return a string and has no argument, the base implementation can be used,
which will always return the types ``STRING`` and ``NIL`` and no errors.

:arg:`cfg`
   The configuration object, representing the configuration being loaded.

:arg:`spec`
   The parsed specifier for the extractor. This can also be used to store instance data if needed.

:arg:`arg`
   The argument to the extractor, if any. Arguments are specified by adding angle enclosed text
   after the extractor. For instance the proxy response field extrator :ex:`proxy-rsp-field`
   requires an argument that is the field name - :code:`proxy-rsp-field<Best-Band>` to get the field
   with the name "Best-Band'. If an argument is required, the :code:`validate` method must parse the
   argument and validate it, returning an error if it is invalid.

An extractor that returns any type other than a string must override this method.

.. code-block:: cpp

   Feature extract(Context & ctx, Spec const& spec);

This method must be overridden. This is called when the value for the extractor is needed and should
perform the extraction, returning the result.

:arg:`ctx`
   The context for the transaction.

:arg:`spec`
   The parsed specifier. This is the same instance passed to :code:`validate`.

.. code-block:: cpp

   swoc::BufferWriter & format(swoc::BufferWriter& w, Spec const& spec, Context & ctx);

This method is called when the value for the extractor is needed in a string. The method must output
the extracted value to the buffer as a string.

:arg:`w`
   The output buffer.

:arg:`spec`
   The parsed specifier. This is the same instance passed to :code:`validate`.

:arg:`ctx`
   The context instance.

The :code:`extract` and :code:`format` mehods are closely related and generally one will invoke the
other, most frequently :code:`format` calling :code:`extract` and passing the result to
:code:`bwformat` to generate the string output. Therefore there is a default implementation of this
method.

.. code-block:: cpp

   return bwformat(w, spec, this->extract(ctx, spec));

If this suffices, then it does not be to be overridden. There are cases where this is necessary
which is why the methods are separate.

In some cases an extractor needs to store instance related information. This should be allocated
from configuration memory. The specifier has a member :txb:`Extractor::Spec::_data` which holds a
:code:`MemSpan<void>`. Because the same specifier instance is passed to :code:`validate` and
:code:`extract` a configuration allocated span can be stored there for later retrieval. While any
span can be assigned to a void span, the :code:`MemSpan::rebind<T>` method must be used to retrieve the actual
type.

String Extractor
----------------

For performance reasons string extractors are required to extract into transient context memory. If the
output size isn't reasonably bounded at extraction time then it may be necessary to attempt the
extraction, detect the transient memory length being insufficient, and trying again. To simplify this
there is a class, :txb:`StringExtractor` to help with the implementation. This requires the extractor
to implement the :code:`format` method and uses that to implement the :code:`extract` method.

Example
=======

Consider an extractor for the inbound transaction count. The code is in :git:`plugin/src/Ex_Ssn.cc`.

The implementation is done in two parts

Specifically for extractor, the |TS| plugin API support must be extended to call
:code:`TSHttpSsnTransactionCount` to perform the actual extraction. This is straight forward. A
method is added to the HTTP session support class :code:`ts::HttpSsn` in
:git:`plugin/include/txn_box/ts_util.h`.

.. code-block:: cpp

    unsigned HttpSsn::txn_count() const { return TSHttpSsnTransactionCount(_ssn); };

Given access to the data to be extracted, the next step is to define the extractor class.

.. code-block:: cpp

   class Ex_inbound_txn_count : public Extractor {
   public:
     static constexpr TextView NAME { "inbound-txn-count" };

     Rv<ActiveType> validate(Config&, Extractor::Spec&, TextView const&) override;

     Feature extract(Context & ctx, Spec const& spec)  override;
   };

This is a minimal implementation. The method implemtations are straight forward.

.. code-block:: cpp

   Rv<ActiveType> Ex_inbound_txn_count::validate(Config&, Extractor::Spec&, TextView const&) {
     return ActiveType{ INTEGER }; // never a problem, just return the type.
   }

   Feature Ex_inbound_txn_count::extract(Context &ctx, Spec const&) {
     return feature_type_for<INTEGER>(ctx.inbound_ssn().txn_count());
   }

The :code:`validate` method doesn't check for any errors (as there is no argument) and returns an
active type of "INTEGER" which is the type of value extracted. The :code:`extract` method retrieves
the inbound session from the context instance and then gets the transaction count from there. The
method is required to return a :txb:`Feature` instance. This type can be constructed from any of the
valid feature types. The meta-function :txb:`feature_type_for` is used to retrieve the feature type
used for INTEGER values and the methods constructions casts the transaction count to that type and
returns it, which in turn constructs a feature with the value and type.

This provides the implementation but the extractor must be declared and registered to be used. This is
done in a static initializer in the source file.

.. code-block:: cpp

   namespace {
      Ex_inbound_txn_count inbound_txn_count;

      [[maybe_unused]] bool INITIALIZED = [] () -> bool {
        Extractor::define(Ex_inbound_txn_count::NAME, &inbound_txn_count);

        return true;
      } ();
   } // namespace

This declares a file scope instance of the extractor class and a static :code:`bool` variable
"INITIALIZED". The value is set to the result of a lambda that takes no arguments. The point of this
is to force the invocation of the lambda which in turns calls :txb:`Extractor::define` to define the
"inbound-txn-count" extractor, passing the extractor name and implementation class instance. The
enclosing anonymous :code:`namespace` helps avoid name collisions by preventing any external
linkage.

As an example of instance storage, the random extractor (:txb:`Ex_random`) must store two integers
which are the limits of the generated value. The argument for this is parsed in :code:`validate` and
stored using the code

.. code-block:: cpp

   auto values = cfg.alloc_span<feature_type_for<INTEGER>>(2);
   spec._data = values; // remember where the storage is.

:arg:`values` gets a configuratin allocated span the size of two integers. This is then cached in
the specifier and other code parses the arguments and sets the values in the span. During invocation
in :code:`extract` the values are retrieved.

.. code-block:: cpp

   auto values = spec._data.rebind<feature_type_for<INTEGER>>();

As before, :arg:`values` is a :code:`MemSpan<feature_type_for<INTEGER>>` of size 2 and therefore the
values can be accessed as :code:`values[0]` and :code:`values[1]`.

More commonly a nested class will be defined and used as the configuration type, allocating a span
of size 1, but the mechanism is the same.

Note this memory is uninitialized. If a class instance is to be stored it must be completely
assigned by the code (as is the case for :code:`Ex_random`) or placement :code:`new` should be used
to construct to a known state. It is usually the case that all of the members are set (because if
the member isn't set during configuration load, why is it there?) but sometimes more complex
initialization is required.

For the random extractor this could have been done with

.. code-block:: cpp

   using I = feature_type_for<INTEGER>;
   auto values = cfg.alloc_span<I>(2);
   values.apply([](I& i) { new (&i) I; });
   spec._data = values; // remember where the storage is.

While clearly not really useful for an integral type, the technique is identical for a class, only
the type is the class intead of the feature integer value type.

Or, if zero initialized memory suffices

.. code-block:: cpp

   auto values = cfg.alloc_span<feature_type_for<INTEGER>>(2);
   memset(values, 0);
   spec._data = values; // remember where the storage is.

.. note::

   This configuration allocated memory is *per configuration*. That means it can be accessed from
   multiple threads in different transactions simultaneously.
