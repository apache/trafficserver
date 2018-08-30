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

/*
 *  ink_exception.h
 *
 *  Contains some global exception classes.
 *
 *  $Date: 2003-06-01 18:36:43 $
 *
 *
 */

#pragma once

class Exception
{
};

class OSException : public Exception
{
};

class IOException : public OSException
{
};

class FileOpenException : public IOException
{
};
class FileStatException : public IOException
{
};
class FileSeekException : public IOException
{
};
class FileReadException : public IOException
{
};
class FileWriteException : public IOException
{
};
class FileCloseException : public IOException
{
};

class MemoryMapException : public OSException
{
};
class MemoryUnmapException : public OSException
{
};
