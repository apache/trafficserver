<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<html>
<head>

  <title>Debug Logs</title>
  <meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1">
  <script language="JavaScript1.2" src="/sniffer.js" type="text/javascript"></script>
  <script language="JavaScript1.2">
  if(!is_win && !is_mac)
    document.write("<link rel='stylesheet' type='text/css' href='/inktomiLarge.css' title='Default'>");
  else
    document.write("<link rel='stylesheet' type='text/css' href='/inktomi.css' title='Default'>");
  </script>

</head>

<body bgcolor="#FFFFFF">
<h1>Debug Logs</h1>

<form method="post" action="/log.cgi">
<input type=hidden name="submit_from_page" value="/configure/c_view_debug_logs.ink">

<@submit_error_msg>

<table width="100%" border="1" cellspacing="0" cellpadding="5" bordercolor="#cccccc">
  <tr>
    <td height="2" class="configureLabel">Log File</td>
  </tr>
  <tr>
    <td>
      <select name="logfile" size="1">
        <option value='default'>- select a log file -</option>
        <@select_debug_logs>
      </select>
    </td>
  </tr>
  <tr>
    <td height="2" class="configureLabel">Action</td>
  </tr>
  <tr>
    <td>
    <table width="100%" cellspacing="0" cellpadding="0">
      <tr>
    <td class=bodyText nowrap>
      <input type=radio name="action" value="view_all" <@action_checked action\view_all>>
        Display the selected log file<br>
      <@submit_error_flg view_last>
      <input type=radio name="action" value="view_last" <@action_checked action\view_last>>
        Display last <input type=text size=6 name="nlines" value="<@post_data nlines>"> lines of the selected file<br>
      <@submit_error_flg view_subset>
      <input type=radio name="action" value="view_subset" <@action_checked action\view_subset>>
        Display lines that match <input type=text size=20 name="substring" value="<@post_data substring>"> in the selected log file<br>
      <input type=radio name="action" value="remove" <@action_checked action\remove>>
        Remove the selected log file <br>
      <input type=radio name="action" value="save" <@action_checked action\save>>
        Save the selected log file in local filesystem<br>
    </td>
      </tr>

  <tr>
    <td valign="bottom" align="right">
      <input class="configureButton" type=submit name="apply" value="  Apply  ">
    </td>
  </tr>

    </table>
    </td>
  </tr>


<@log_action /configure/helper/log.sh>

</table>

</form>
</body>
</html>

