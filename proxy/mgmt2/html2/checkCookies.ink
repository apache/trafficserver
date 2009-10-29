<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<html>
<head>
  <title><@record proxy.config.manager_name> UI</title>
  <meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1">
</head>

<body bgcolor="#000000">
<div align="center">
  <table border="0" cellspacing="0" cellpadding="0">
    <tr> 
      <td> 
        <div align="right">
          <font color="#313131" face="Verdana, Arial, Helvetica, sans-serif" size="7">
            <b>Checking for cookies Support</b>
          </font><br>
          <font color="#FFCC00" face="Arial, Helvetica, sans-serif">
            <b>Please wait while we check if cookies is enabled...</b>


     <SCRIPT language=javascript>
        function IsCookieTurnedOn() {
           document.cookie = "cookie_test=1";
           if ( ! document.cookie ) {
              return false;
           }
           else {
              return true;
           }
        }
 
        if ( IsCookieTurnedOn() == false ) {
          document.writeln ("<table border=\"2\" width=\"480\" cellspacing=\"1\" cellpadding=\"6\">");
          document.writeln ("<tr>");
           document.writeln ("<td valign=\"top\" align=\"center\"><font face=\"Arial, Helvetica, sans-serif\" color=\"#FFCC00\">Cookies disabled. Please enable cookies to use session management</font></TD>");
          document.writeln ("</tr>");
          document.writeln ("<tr>");
          document.writeln("</table>");
          window.location.replace("/enableCookies.ink")
        }
     </SCRIPT>


          </font>
        </div>
      </td>
    </tr>
  </table>
</div>
</body>
</html>
