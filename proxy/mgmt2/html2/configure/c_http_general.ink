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
      <p>&nbsp;&nbsp;General HTTP Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.http.server_port>HTTP Proxy Server Port</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.http.server_port" value="<@record proxy.config.http.server_port>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the port that <@record proxy.config.product_name> uses when acting as
            a web proxy server for web traffic or when serving web traffic transparently.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.http.enable_url_expandomatic>URL Expandomatic</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.http.enable_url_expandomatic" value="1" <@checked proxy.config.http.enable_url_expandomatic\1>>
	Enabled <br>
      <input type="radio" name="proxy.config.http.enable_url_expandomatic" value="0" <@checked proxy.config.http.enable_url_expandomatic\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables .com domain expansion, which configures the 
            <@record proxy.config.product_name> to attempt to resolve unqualified 
            hostnames by redirecting them to the expanded address, prepended 
            with www. and appended with .com.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.http.push_method_enabled>PUSH Method</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.http.push_method_enabled" value="1" <@checked proxy.config.http.push_method_enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.http.push_method_enabled" value="0" <@checked proxy.config.http.push_method_enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables the HTTP PUSH option that allows you to
	    deliver content directly to the cache without user request.
        <li>If you enable this option, you must also specify a filtering 
            rule in the filter.config file to allow only certain machines 
            to push content into the cache.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.http.ssl_ports>HTTPS Redirect</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.http.ssl_ports" value="<@record proxy.config.http.ssl_ports>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the range of ports used for tunneling.  
            <@record proxy.config.product_name> allows tunnels only to
            the specified ports.
        <li>For example, to retrieve an object using HTTPS via Traffic
            Server, you must establish a tunnel via
            <@record proxy.config.product_name> to an origin server.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">FTP over HTTP</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.ftp.anonymous_passwd>Anonymous Password</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.http.ftp.anonymous_passwd" value="<@record proxy.config.http.ftp.anonymous_passwd>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the anonymous password for FTP servers that
                        require a password for access.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.data_connection_mode>Data Connection Mode</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText"> 
                  <input type="radio" name="proxy.config.ftp.data_connection_mode" value="1" <@checked proxy.config.ftp.data_connection_mode\1>>
                    PASV then PORT <br>
                  <input type="radio" name="proxy.config.ftp.data_connection_mode" value="2" <@checked proxy.config.ftp.data_connection_mode\2>>
                    PORT Only <br>
                  <input type="radio" name="proxy.config.ftp.data_connection_mode" value="3" <@checked proxy.config.ftp.data_connection_mode\3>>
                    PASV Only
                </td>
                <td width="100%" class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the FTP connection mode over HTTP.
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
