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

/****************************************************************************

   test_interp_glue.h

   Description:


 ****************************************************************************/

#ifndef _TEST_INTERP_GLUE_H_
#define _TEST_INTERP_GLUE_H_

#ifdef	__cplusplus
extern "C"
{
#endif

  int pm_create_instance(const char *instance_name, const char *hostname, char **args);
  int pm_start_instance(const char *instance_name, char **args);
  int pm_stop_instance(const char *instance_name, char **args);
  int pm_destroy_instance(const char *instance_name, char **args);
  char *pm_run(const char *hostname, const char *binary, const char *args, int timeout);
  char *pm_run_slave(const char *master_instance, const char *binary, const char *args, int timeout);
  int pm_alloc_port(const char *hostname);
  int add_to_log(const char *log_line);
  int set_log_parser(const char *instance, const char *parser);
  char *get_var_value(const char *var_name);
  int set_var_value(const char *var_name, const char *var_value);
  int wait_for_server_port(const char *instance, const char *port_str, int timeout_ms);
  int wait_for_instance_death(const char *instance, int timeout_ms);
  char *get_instance_file(const char *instance, const char *file);
  int put_instance_file_raw(const char *instance, const char *relative_path, const char *src);
  int put_instance_file_subs(const char *instance, const char *relative_path, const char *src);
  char **stat_instance_file(const char *instance, const char *file);
  int is_instance_alive(const char *instance);

  char **raf_proc_manager(const char *instance_name, const char *raf_cmd, char **raf_args);
  char **raf_instance(const char *instance_name, const char *raf_cmd, char **raf_args);

#ifdef	__cplusplus
}
#endif

#endif
