.. _admin-plugins-s3-auth:

AWS S3 Authentication plugin
****************************

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


This is a plugin for Apache Traffic Server that provides support for the
``Amazon S3`` authentication features. This is useful if you for example want
to use ``S3`` as your origin server, yet want to avoid direct user access to
the content.

Using the plugin
================


Using the plugin in a remap rule would be e.g.::

   # remap.config

   ...  @plugin=s3_auth @pparam=--access_key @pparam=my-key \
                        @pparam=--secret_key @pparam=my-secret \
			@pparam=--virtual_host


Alternatively, you can store the access key and secret in an external configuration file, and point the remap rule(s) to it::

   # remap.config

   ...  @plugin=s3_auth @pparam=--config @pparam=s3_auth_v2.config


Where ``s3.config`` could look like::

    # s3_auth_v2.config

    access_key=my-key
    secret_key=my-secret
    version=2
    virtual_host=yes

Both ways could be combined as well


AWS Authentication version 4
============================

The s3_auth plugin fully implements: `AWS Signing Version 4 <http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-authenticating-requests.html>`_ / `Authorization Header <http://docs.aws.amazon.com/AmazonS3/latest/API/sigv4-auth-using-authorization-header.html>`_ / `Transferring Payload in a Single Chunk <http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html>`_ / Unsigned Payload Option

Configuration options::

    # Mandatory options
    --access_key=<access_id>
    --secret_key=<key>
    --version=4

    # Optional
    --v4-include-headers=<comma-separated-list-of-headers-to-be-signed>
    --v4-exclude-headers=<comma-separated-list-of-headers-not-to-be-signed>
    --v4-region-map=region_map.config


If the following option is used then the options could be specified in a file::

    --config=s3_auth_v4.config


The ``s3_auth_v4.config`` config file could look like this::

    # s3_auth_v4.config

    access_key=<access_id>
    secret_key=<secret_key>
    version=4
    v4-include-headers=<comma-separated-list-of-headers-to-be-signed>
    v4-exclude-headers=<comma-separated-list-of-headers-not-to-be-signed>
    v4-region-map=region_map.config

Where the ``region_map.config`` defines the entry-point hostname to region mapping i.e.::

    # region_map.config

    # "us-east-1"
    s3.amazonaws.com                     : us-east-1
    s3-external-1.amazonaws.com          : us-east-1
    s3.dualstack.us-east-1.amazonaws.com : us-east-1

    # us-west-1
    s3-us-west-1.amazonaws.com           : us-west-1
    s3.dualstack.us-west-1.amazonaws.com : us-west-1

    # Default region if no entry-point matches:
    : s3.amazonaws.com

If ``--v4-region-map`` is not specified the plugin defaults to the mapping defined in `"Regions and Endpoints - S3" <http://docs.aws.amazon.com/general/latest/gr/rande.html#s3_region>`_

According to `Transferring Payload in a Single Chunk <http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html>`_ specification
the ``CanonicalHeaders`` list *must* include the ``Host`` header,  the ``Content-Type`` header if present in the request and all the ``x-amz-*`` headers
so ``--v4-include-headers`` and ``--v4-exclude-headers`` do not impact those headers and they are *always* signed.

The ``Via`` and ``X-Forwarded-For`` headers are *always* excluded from the signature since they are meant to be changed by the proxies and signing them could lead to invalidation of the signatue.

If ``--v4-include-headers`` is not specified all headers except those specified in ``--v4-exclude-headers`` will be signed.

If ``--v4-include-headers`` is specified only the headers specified will be signed except those specified in ``--v4-exclude-headers``


AWS Authentication version 2
============================

For more details on the S3 auth version 2 , see: `Signing and Authenticating REST Requests <http://docs.aws.amazon.com/AmazonS3/latest/dev/RESTAuthentication.html>`_


There are 4 plugin configuration options for version 2::

    --access_key    <access_id>
    --secret_key    <secret_key>
    --virtual_host
    --config        <config file>
    --version=2

This is a pretty barebone start for the S3 services, it is missing a number of features:

- It does not do UTF8 encoding (as required)
- It does not deal with canonicalization of AMZ headers.
- It does not handle POST requests (but do we need to ?)
- It does not incorporate query parameters.


Contributions to any of these would be appreciated.
