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
----------------

There are three configuration options for this plugin::

    --access_key    <key>
    --secret_key    <key>
    --virtual_host
    --config        <config file>


Using the first two in a remap rule would be e.g.::

   ...  @plugin=s3_auth @pparam=--access_key @pparam=my-key \
                        @pparam=--secret_key @pparam=my-secret \
			@pparam=--virtual_host


Alternatively, you can store the access key and secret in an external
configuration file, and point the remap rule(s) to it:

   ...  @plugin=s3_auth @pparam=--config @pparam=s3.config


Where s3.config would look like::

    # AWS S3 authentication
        access_key=my-key
        secret_key=my-secret
        virtual_host=yes


For more details on the S3 auth, see::

  http://docs.aws.amazon.com/AmazonS3/latest/dev/RESTAuthentication.html


ToDo
----

This is a pretty barebone start for the S3 services, it is missing a number of features:

- It does not do UTF8 encoding (as required)

- It only implements the v2 authentication mechanism. For details on v4, see

  http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-authenticating-requests.html

- It does not deal with canonicalization of AMZ headers.

- It does not handle POST requests (but do we need to ?)

- It does not incorporate query parameters.


Contributions to any of these would be appreciated.
