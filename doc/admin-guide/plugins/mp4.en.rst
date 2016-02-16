.. _admin-plugins-mp4:

MP4 Plugin
**********

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

This module provides streaming media server support for MP4 files.
User can send a HTTP request to the server with ``start`` argument
which is measured in seconds, and the server will respond with the
stream such that its start position corresponds to the requested time,
for example::

  http://v.foo.com/dota2.mp4?start=290.12

This allows performing a random seeking at any time. We can use flash
player, vlc, mplayer, firefox or chrome to play the streaming media.

This plugin can be used as a remap plugin. We can write this in remap.config::

  map http://v.foo.com/ http://v.internal.com/ @plugin=mp4.so


Note
===================

This plugin requires that the ``moov`` box in the mp4 file should be ahead
of ``mdat`` box. It is not a good idea to cache a large mp4 file, many video
sites will cut a large video file into many small mp4 files, and each
small mp4 file will be less than 80M(bytes), it will be a reasonable choice.
