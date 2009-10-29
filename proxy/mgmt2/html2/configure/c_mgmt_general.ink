<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method="post" action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;General <@record proxy.config.manager_name> UI Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.admin.web_interface_port>UI Port</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.admin.web_interface_port" value="<@record proxy.config.admin.web_interface_port>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the port for the <@record proxy.config.manager_name> UI.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">HTTPS</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.admin.use_ssl>Enable/Disable</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.admin.use_ssl" value="1" <@checked proxy.config.admin.use_ssl\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.admin.use_ssl" value="0" <@checked proxy.config.admin.use_ssl\0>>
                    Disabled
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables SSL connections to the <@record proxy.config.manager_name> UI.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.admin.ssl_cert_file>Certificate File</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.admin.ssl_cert_file" value="<@record proxy.config.admin.ssl_cert_file>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the SSL certificate file.
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
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.admin.ui_refresh_rate>Monitor Refresh Rate</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.admin.ui_refresh_rate" value="<@record proxy.config.admin.ui_refresh_rate>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the rate of refresh for the <@record proxy.config.manager_name>
            UI monitor pages.
      </ul>
    </td>
  </tr>

</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
