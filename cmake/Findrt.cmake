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

# librt provides shm_open()/shm_unlink() on older glibc (e.g. CentOS 7 / glibc 2.17).
# glibc >= 2.34 folds them into libc and macOS has them in libc, so the library is
# absent there; the imported target is then an empty no-op.

find_library(rt_LIBRARY rt)

mark_as_advanced(rt_FOUND rt_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(rt REQUIRED_VARS rt_LIBRARY)

# Always provide the target; only carry a link dependency when librt exists.
add_library(rt::rt INTERFACE IMPORTED)
if(rt_FOUND)
  target_link_libraries(rt::rt INTERFACE ${rt_LIBRARY})
endif()
