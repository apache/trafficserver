/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "missing_mangled_definition.h"

/*
 * Assume the definition of foo from the corresponding .c
 * object. This will result in an undefined symbol, however,
 * because the .c file will be compiled with a C compiler
 * while this will be compiled with a C++ compiler such that
 * it will expect a definition for a mangled foo symbol.
 */

void
TSPluginInit(int argc, const char *argv[])
{
  foo();
}
