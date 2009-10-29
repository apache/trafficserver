<!-------------------------------------------------------------------------
  c_radius_general.ink
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;General Radius Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel">Primary Radius Server</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.radius.proc.radius.primary_server.name>Hostname</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.radius.proc.radius.primary_server.name" value="<@record proxy.config.radius.proc.radius.primary_server.name>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the name of the Radius server.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.radius.proc.radius.primary_server.auth_port>Port</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.radius.proc.radius.primary_server.auth_port" value="<@record proxy.config.radius.proc.radius.primary_server.auth_port>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the authentication port of the Radius server.
                  </ul>
                </td>
              </tr>

              <tr>
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.radius.proc.radius.primary_server.shared_key_file>Shared Key</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
  		  <@password_object proxy.config.radius.proc.radius.primary_server.shared_key_file>
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the key to use for encoding.
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
    <td height="2" colspan="2" class="configureLabel">Secondary Radius Server (Optional)</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr>
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.radius.proc.radius.secondary_server.name>Hostname</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.radius.proc.radius.secondary_server.name" value="<@record proxy.config.radius.proc.radius.secondary_server.name>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the name of the Radius server.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.radius.proc.radius.secondary_server.auth_port>Port</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.radius.proc.radius.secondary_server.auth_port" value="<@record proxy.config.radius.proc.radius.secondary_server.auth_port>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the authentication port of the Radius server.
                  </ul>
                </td>
              </tr>

              <tr>
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.radius.proc.radius.secondary_server.shared_key_file>Shared Key</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <@password_object proxy.config.radius.proc.radius.secondary_server.shared_key_file>
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the key to use for encoding.
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
