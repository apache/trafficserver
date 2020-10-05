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

.. _admin-plugins-webp-transform:

Webp Transform Plugin
*********************

This plugin converts jpeg and png images and transforms them into webp format for browsers that support webp.
Also, the plugin converts webp images and transforms them to jpeg for browsers that don't support webp
All response with content-type 'image/jpeg' or 'image/png' will go through the transform.
Content-type is changed to 'image/webp' or 'image/jpeg' on successful transformation.

Installation
============

Add the following line to :file:`plugin.config`::

    webp_transform.so [convert_to_jpeg,convert_to_webp]


Note
====

This plugin only supports jpeg and png and requires Magick++ from ImageMagick.
Other image formats can easily be supported.
