<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;FTP Timeout Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel">Keep-Alive Timeout</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.server_ctrl_keep_alive_no_activity_timeout>Server Control</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.ftp.server_ctrl_keep_alive_no_activity_timeout" value="<@record proxy.config.ftp.server_ctrl_keep_alive_no_activity_timeout>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the timeout value when the FTP server control
                        connection is not used by any FTP clients.
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
    <td height="2" colspan="2" class="configureLabel">Inactivity Timeouts</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.client_ctrl_no_activity_timeout>Client Control</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.ftp.client_ctrl_no_activity_timeout" value="<@record proxy.config.ftp.client_ctrl_no_activity_timeout>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the inactivity timeout for the FTP client control connection.
                  </ul>
                </td>
              </tr>
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.server_ctrl_no_activity_timeout>Server Control</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.ftp.server_ctrl_no_activity_timeout" value="<@record proxy.config.ftp.server_ctrl_no_activity_timeout>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the inactivity timeout for the FTP server control connection.
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
    <td height="2" colspan="2" class="configureLabel">Active Timeouts</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
  <tr> 
    <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.client_ctrl_active_timeout>Client Control</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.ftp.client_ctrl_active_timeout" value="<@record proxy.config.ftp.client_ctrl_active_timeout>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the active timeout for the FTP server control connection.
      </ul>
    </td>
  </tr>


  <tr> 
    <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.server_ctrl_active_timeout>Server Control</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.ftp.server_ctrl_active_timeout" value="<@record proxy.config.ftp.server_ctrl_active_timeout>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the active timeout for the FTP server control connection.
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
