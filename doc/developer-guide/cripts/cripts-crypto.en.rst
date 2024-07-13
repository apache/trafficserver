.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _cripts-crypto:

Crypto
******

Sometimes doing cryptographic operations can be useful in a Cript. Cripts provides
a foundation for such tasks, providing hashing, encryption and encoding/decoding.

.. note::
   This is still very much work in progress, and thus far we've only added the most
   basic of cryptographic operations. More will be added as needed.

.. _cripts-misc-crypto-hash:

Hash
====

=================================   ===============================================================
Object                              Description
=================================   ===============================================================
``Crypto::MD5``                     MD5 hashing.
``Crypto::SHA256``                  SHA256 hashing.
``Crypto::SHA512``                  SHA512 hashing.
``Crypto::HMAC::SHA256``            HMAC-SHA256 hashing.
=================================   ===============================================================

These objects all provide a ``Encode()`` and ``Decode()`` method, to hash and unhash strings.
Examples:

.. code-block:: cpp

   do_remap()
   {
     CDebug("SHA256 = {}", Crypto::SHA256::Encode("Hello World"));
   }

.. _cripts-misc-crypto-encryption:

Encryption
==========

Currently only one encryption object is provides, for AES256. This object provides
``Encrypt()`` and ``Decrypt()`` methods. A ``Hex()`` method is also provided to retrieve
the encrypted data as a hex string. For encrypting data in chunks, a ``Finalize()`` method
is provided to retrieve the final encrypted data.

=================================   ===============================================================
Object                              Description
=================================   ===============================================================
``Crypto::AES256``                  AES256 encryption and decryption.
=================================   ===============================================================

.. _cripts-misc-crypto-encoding:

Encoding
========

Finally, for convenience, Cripts provides a ``Base64`` object for encoding and decoding, as well
as a URL escaping object, ``Escape``.

=================================   ===============================================================
Object                              Description
=================================   ===============================================================
``Crypto::Base64``                  Methods for Base64 encoding.
``Crypto::Escape``                  Methods for URL escaping.
=================================   ===============================================================

These objects all provide a ``Encode()`` and ``Decode()`` method, to encode and decode strings.
