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

<SCRIPT LANGUAGE="JavaScript">

function xor( data, pattern )
{  // Simple xor of a string with a pattern.
   var ii = 0;    // Data index.
   var jj = 0;    // Pattern index.
   var result = "";
   // If no pattern is supplied, then we use a simple pattern.
   // This is fine since a missing pattern will cause TM to complain and the request will
   // not go to the script.
   if( pattern == null || pattern == "" || pattern.length <= 0 ) {
      pattern = "simple_xor_pattern";
   }
   // XOR every character in the data string with a character in the pattern string.
   for( ii = 0; ii < data.length; ii++ ) {
      if( jj >= pattern.length ) {
         jj = 0;
      }
      result += ("%" + (data.charCodeAt(ii) ^ pattern.charCodeAt(jj++)));
   }
   return result;
}

function encode( data, pattern ) 
{  // Encode a string.  The result will be a well behaved escape()d string.
   // Note that the encoded string may be up to three times as long as the original.
   return ( xor( data, pattern ));
}

function testEncode( form ) 
{
   document.ftp.FTPPassword.value = encode( document.ftp.FTPPasswordName.value, document.ftp.FTPUserName.value )
   return true;
}
// -->

</SCRIPT>
<form method=POST name="ftp" onSubmit="testEncode(this.form);" action="/submit_snapshot_ftpserver.cgi?<@link_query c_snapshot_ftpsystem>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file c_snapshot_ftpsystem>>
<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Configuration Snapshots</p>
    </td>
  </tr>
</table>
<@include /configure/c_buttons.ink>
<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10"> 

<!-------------------------------------------------------------------------
  light blue bar
  ------------------------------------------------------------------------->
  <tr> 
    <td colspan="2" class="helpBg">
      <font class="configureBody">
        Snapshots allow you to save and restore
        <@record proxy.config.product_name> configurations.
	<@record proxy.config.product_name> stores snapshots on the
        node in which they were taken.  However, snapshots are
        restored to all nodes in the cluster.
      </font>
    </td>
  </tr>
  <tr> 
    <td height="2" colspan="2" class="configureLabel">List Available Snapshots on the FTP Server</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall">Login Information</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <table>
                    <tr>
                      <td nowrap class="bodyText"><@submit_error_flg FTPServerNameError>FTP Server</td>
                      <td><input type="text" size="22" name="FTPServerName" value="<@post_data FTPServerName>"></td>
                      <td class="configureHelp" valign="top" align="left"> 
                        <ul>
                          <li>Specifies the FTP server name.
                        </ul>
                      </td>
                    </tr>
                    <tr>
                      <td nowrap class="bodyText"><@submit_error_flg FTPUserNameError>Login </td>
                      <td><input type="text" size="22" name="FTPUserName" value="<@post_data FTPUserName>"></td>
                      <td class="configureHelp" valign="top" align="left"> 
                        <ul>
                          <li>Specifies the FTP server user name to login as.
                        </ul>
                      </td>
                    </tr>
                    <tr>
                      <td nowrap class="bodyText"><@submit_error_flg FTPPasswordError>Password</td>
                      <td><input type="password" size="22" name="FTPPasswordName" value="<@post_data FTPPasswordName>"></td>
                      <input type="hidden" size="22" name="FTPPassword" value="">
                      <td class="configureHelp" valign="top" align="left"> 
                        <ul>
                          <li>Specifies the password for the above user name.
                        </ul>
                      </td>
                    </tr>
                    <tr>
                      <td nowrap class="bodyText"><@submit_error_flg FTPRemoteDirError>Remote Directory &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</td>
                      <td><input type="text" size="22" name="FTPRemoteDir" value="<@post_data FTPRemoteDir>"></td>
                      <td class="configureHelp" valign="top" align="left"> 
                        <ul>
                          <li>Specifies the remote directory where the snapshots are located. 
                        </ul>
                      </td>
                    </tr>
                  </table>
                </td>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>
  <@select ftp_snapshot>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
