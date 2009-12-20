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
