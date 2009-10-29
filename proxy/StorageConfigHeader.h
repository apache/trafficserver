/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

char *STORAGE_CONFIG_HEADER = "# 									\n\
# Storage Configuration file                                            \n\
#                                                                       \n\
#                                                                       \n\
# The storage configuration is a list of all the storage to             \n\
# be used by the cache.                                                 \n\
#                                                                       \n\
# It has the format:                                                    \n\
#									\n\
#    pathname  [size]							\n\
#									\n\
# Where 'pathname' is the name of a raw partition, directory or file	\n\
# and 'size' is size in bytes. See the configuration information for	\n\
# your specific O/S below. Size must be specified for directories	\n\
# or files and is optional for raw partitions;				\n\
#									\n\
# Example 1:  64MB in the /big_dir directory				\n\
#    /big_dir          67108864						\n\
#									\n\
# Example 2:  64MB in current directory					\n\
#    .                 67108864						\n\
#									\n\
#############################################################		\n\
##           Solaris Specific Configuration                ##		\n\
#############################################################		\n\
# Example 1:  4GB in 2 2GB raw partitions				\n\
#									\n\
#    /devices/sbus@1f,0/SUNW,fas@e,8800000/sd@2,0:a			\n\
#    /devices/sbus@1f,0/SUNW,fas@e,8800000/sd@2,0:b			\n\
#									\n\
# For a raw partition or a file, the useful size is limited to 2 GB.	\n\
#									\n\
#############################################################           \n\
##        Digtial Unix Specific Configuration              ##		\n\
#############################################################		\n\
# Example 1:  using a naked disk					\n\
#									\n\
#    /dev/rz14c								\n\
#    									\n\
# You can use any partition. You should not use raw devices		\n\
# (e.g., /dev/rrz14c). Partitions can be any size.			\n\
#									\n\
# Example 2: using an LSM volume					\n\
#									\n\
#	/dev/vol/rootdg/cache0						\n\
#									\n\
# You can use any LSM organization, but it is recommended that you	\n\
# use 'naked' disk volumes for best performance.			\n\
#									\n\
";
