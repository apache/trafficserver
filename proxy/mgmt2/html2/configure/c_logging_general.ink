<!-------------------------------------------------------------------------
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
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;General Logging Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.logging_enabled>Logging</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.log2.logging_enabled" value="3" <@checked proxy.config.log2.logging_enabled\3>>
        Log Transactions and Errors <br>
      <input type="radio" name="proxy.config.log2.logging_enabled" value="2" <@checked proxy.config.log2.logging_enabled\2>>
        Log Transactions Only <br>
      <input type="radio" name="proxy.config.log2.logging_enabled" value="1" <@checked proxy.config.log2.logging_enabled\1>>
        Log Errors Only <br>
      <input type="radio" name="proxy.config.log2.logging_enabled" value="0" <@checked proxy.config.log2.logging_enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables event logging.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.logfile_dir>Log Directory</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.log2.logfile_dir" value="<@record proxy.config.log2.logfile_dir>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the full path to the logging directory.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Log Space</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.max_space_mb_for_logs>Limit</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.log2.max_space_mb_for_logs" value="<@record proxy.config.log2.max_space_mb_for_logs>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the amount of space allocated to the logging directory in MB.
                  </ul>
                </td>
              </tr>            

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.max_space_mb_headroom>Headroom</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.log2.max_space_mb_headroom" value="<@record proxy.config.log2.max_space_mb_headroom>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the tolerance for the log space limit in MB.
                    <li>If auto-delete rolled files is enabled, autodeletion of
                        log files is triggered when the amount of free space 
                        available in the logging directory is less than the value 
                        specified here.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Log Rolling</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.rolling_enabled>Enable/Disable</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.log2.rolling_enabled" value="1" <@checked proxy.config.log2.rolling_enabled\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.log2.rolling_enabled" value="0" <@checked proxy.config.log2.rolling_enabled\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables log file rolling.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.rolling_offset_hr>Offset Hour</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.log2.rolling_offset_hr" value="<@record proxy.config.log2.rolling_offset_hr>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the file rolling offset hour.  The hour of the
                        day that starts the log rolling period.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.rolling_interval_sec>Interval</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.log2.rolling_interval_sec" value="<@record proxy.config.log2.rolling_interval_sec>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the log file rolling interval, in seconds.  
                    <li>The minimum value is 300 (5 minutes).
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.auto_delete_rolled_files>Auto-Delete Rolled Files</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.log2.auto_delete_rolled_files" value="1" <@checked proxy.config.log2.auto_delete_rolled_files\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.log2.auto_delete_rolled_files" value="0" <@checked proxy.config.log2.auto_delete_rolled_files\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables automatic deletion of rolled files.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
