#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

add_atsplugin(magick magick.cc)

# This creates the magic import target if necessary (older cmake)
include(magick_target)

target_link_libraries(magick PRIVATE OpenSSL::Crypto ImageMagick::MagickWand ImageMagick::MagickCore ts::tscppapi)

# There is a periodic issue with asan errors and imagemagick so disable this test for asan builds to reduce noise
if(NOT ENABLE_ASAN)
  verify_global_plugin(magick ${CMAKE_CURRENT_SOURCE_DIR}/image_magic_dlopen_leak_suppression.txt)
endif()
