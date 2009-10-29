<!-------------------------------------------------------------------------
  body_header.ink
  ------------------------------------------------------------------------->
<body bgcolor="#000000" leftmargin="0" topmargin="0" marginwidth="0" marginheight="0" background="/images/header_bg.gif"
  onLoad="javascript:openandclose('<@query menu>:<@query item>');">

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr align="left"> 
    <td> 
      <table width="100%" border="0" cellspacing="0" cellpadding="0">
        <tr>
          <td valign="bottom">
            <table width="100%" border="0" cellspacing="0" cellpadding="0">
              <tr>
<@tab_object>
              </tr>
            </table>
          </td>
          <td align="right" width="100%"><a href="http://www.inktomi.com/" target="_blank"><img src="/images/ink_logo.gif" width="190" height="64" border="0"></a></td>
        </tr>
      </table>
    </td>
  </tr>
  <tr> 
    <td class="hilightColor">
      <table height="25" width="100%" border="0" cellspacing="5" cellpadding="2">
        <tr>
          <td class="secondaryFont" width="100%">&nbsp;&nbsp;<@user></td>
          <td class="secondaryFont" align="right" nowrap>
	    <a class="help" href="<@help_link>" target="help_window">Get Help!&nbsp;&nbsp;</a>
	  </td>
	</tr>
      </table>
    </td>
  </tr>
  <tr> 
    <td class="tertiaryColor"><img src="/images/dot_clear.gif" width="1" height="2"></td>
  </tr>
</table>
<table width="100%" border="0" cellspacing="0" cellpadding="0" height="100%">
  <tr>
    <td width="230" class="secondaryColor" align="left" valign="top">
<@tree_object>
      <img src="/images/dot_clear.gif" width="200" height="1" border="0" alt="">
    </td>
    <td class="primaryColor" align="center" valign="top" width="100%"> 
<@alarm_summary_object>
      <img src="/images/dot_clear.gif" width="1" height="15"> 
