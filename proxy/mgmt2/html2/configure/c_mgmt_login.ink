<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_mgmt_auth.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;<@record proxy.config.manager_name> UI Login Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.admin.basic_auth>Basic Authentication</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.admin.basic_auth" value="1" <@checked proxy.config.admin.basic_auth\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.admin.basic_auth" value="0" <@checked proxy.config.admin.basic_auth\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables basic user authentication to control
            access to the <@record proxy.config.manager_name> UI.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Administrator</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.admin.admin_user>Login</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="22"
                name="proxy.config.admin.admin_user" value="<@record proxy.config.admin.admin_user>" maxlength="16">
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the administrator ID that controls access to the <@record proxy.config.manager_name> UI.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.admin.admin_password>Password</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <table>
                    <tr>
                      <td nowrap class="bodyText">Old Password</td>
                      <td><input type="password" size="22" name="admin_old_passwd" value="" maxlength="32"></td>
                    </tr>
                    <tr>
                      <td nowrap class="bodyText">New Password</td>
                      <td><input type="password" size="22" name="admin_new_passwd" value="" maxlength="32"></td>
                    </tr>
                    <tr>
                      <td nowrap class="bodyText">New Password (Retype)</td>
                      <td><input type="password" size="22" name="admin_new_passwd_retype" value="" maxlength="32"></td>
                    </tr>
                  </table>
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the encrypted administrator password that
                        controls access to the <@record proxy.config.manager_name> UI.
                    <li>To change the password, enter the old password, then enter
                        the new password twice.
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

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td class=configureLabel vAlign=top noWrap width="100%" colspan=2><@submit_error_flg additional_administrative_accounts>Additional Users</td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left" colspan=2>
      Specifies additional user accounts.  Administrators can create
      new accounts to grant additional users access to the
      <@record proxy.config.manager_name> UI.
    </td>
  </tr>
  <tr>
    <input type=hidden name="users" value="1">
    <td align=middle colspan=2>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr>
          <td class="configureLabelSmall" width="33%">Username</td>
          <td class="configureLabelSmall" width="33%">Access</td>
          <td class="configureLabelSmall" width="33%">Encrypted Password</td>
          <td class="configureLabelSmall">Delete</td>
        </tr>
<@mgmt_auth_object>
      </table>
    </td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr>
                <td class="configureLabelSmall" width="100%" colspan="2"><@submit_error_flg add_new_administrative_user>Add New User</td>
              </tr>
              <tr>
                <td>
                  <table>
                    <tr>
                      <td class=bodyText noWrap>New User</td>
                      <td><input type="text" size="22" name="new_user" value="" maxlength="16"></td>
                    </tr>
                    <tr>
                      <td class=bodyText noWrap>New Password</td>
                      <td><input type="password" size="22" name="new_passwd" value="" maxlength="32"></td>
                    </tr>
                    <tr>
                      <td class=bodyText noWrap>New Password (Retype)</td>
                      <td><input type="password" size="22" name="new_passwd_retype" value="" maxlength="32"></td>
                    </tr>
                  </table>
                </td>
 	        <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li><@record proxy.config.product_name> adds new
                    users with 'No Access' privileges.  After adding a
                    new user, you can modify the privileges above.
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

<@include /include/body_footer.ink>


