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
<@include /monitor/m_header.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;MRTG Overview</p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">

        <tr>
          <td height="2" colspan="2" class="configureLabel">System Overview</td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            Client Connections:<BR>
            <A HREF="ccon.html"><IMG HEIGHT=88 WIDTH=325 SRC="ccon-day.png"></A>
          </td>
          <td align=center class="bodyText">
            HTTP Document Hit Rate:<BR>
            <A HREF="doc.html"><IMG HEIGHT=88 WIDTH=325 SRC="doc-day.png"></A>
          </td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            Origin Server Connections:<BR>
            <A HREF="ocon.html"><IMG HEIGHT=88 WIDTH=325 SRC="ocon-day.png"></A>
          </td>
          <td align=center class="bodyText">
            Bandwidth Savings:<BR>
            <A HREF="bw.html"><IMG HEIGHT=88 WIDTH=325 SRC="bw-day.png"></A>
          </td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            Client Transactions Per Second:<BR>
            <A HREF="tps.html"><IMG HEIGHT=88 WIDTH=325 SRC="tps-day.png"></A>
          </td>
          <td align=center class="bodyText">
            DNS Cache Usage:<BR>
            <A HREF="dns.html"><IMG HEIGHT=88 WIDTH=325 SRC="dns-day.png"></A>
          </td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            <@record proxy.config.server_name> Memory Usage:<BR>
            <A HREF="tsmem.html"><IMG HEIGHT=88 WIDTH=325 SRC="tsmem-day.png"></A>
          </td>
          <td align=center class="bodyText">
            Cache Usage:<BR>
            <A HREF="cache.html"><IMG HEIGHT=88 WIDTH=325 SRC="cache-day.png"></A>
          </td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Cache Hit Latency:</FONT></B><BR>
            <A HREF="hittm.html"><IMG HEIGHT=88 WIDTH=325 SRC="hittm-day.png"></A>
          </td>
          <td align=center class="bodyText">
            HTTP Cache Miss Latency:</FONT></B><BR>
            <A HREF="misstm.html"><IMG HEIGHT=88 WIDTH=325 SRC="misstm-day.png"></A>
          </td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Hits & Misses (Count):<BR>
            <A HREF="horm.html"><IMG HEIGHT=88 WIDTH=325 SRC="horm-day.png"></A>
          </td>
          <td align=center class="bodyText">
            HTTP Errors & Aborts (Count):<BR>
            <A HREF="eora.html"><IMG HEIGHT=88 WIDTH=325 SRC="eora-day.png"></A>
          </td>
        </tr>

        <tr>
          <td height="2" colspan="2" class="configureLabel">Media Protocols</td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            RNI Client / Server Connections:<BR>
            <A HREF="rniconn.html"><IMG HEIGHT=88 WIDTH=325 SRC="rniconn-day.png"></A>
          </td>
          <td align=center class="bodyText">
            RNI Hit Rate:<BR>
            <A HREF="rnicachehit.html"><IMG HEIGHT=88 WIDTH=325 SRC="rnicachehit-day.png"></A>
          </td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            WMT Client / Server Connections:<BR>
            <A HREF="wmtconn.html"><IMG HEIGHT=88 WIDTH=325 SRC="wmtconn-day.png"></A>
          </td>
          <td align=center class="bodyText">
            WMT Hit Rate:<BR>
            <A HREF="wmtcachehit.html"><IMG HEIGHT=88 WIDTH=325 SRC="wmtcachehit-day.png"></A>
          </td>
        </tr>
        <tr align="center"> 
          <td align=center class="bodyText">
            QT Client / Server Connections:<BR>
            <A HREF="qtconn.html"><IMG HEIGHT=88 WIDTH=325 SRC="qtconn-day.png"></A>
          </td>
          <td align=center class="bodyText">
            QT Hit Rate:<BR>
            <A HREF="qtcachehit.html"><IMG HEIGHT=88 WIDTH=325 SRC="qtcachehit-day.png"></A>
          </td>
        </tr>

        <tr>
          <td height="2" colspan="2" class="configureLabel">&nbsp;</td>
        </tr>

        <tr align="center"> 
          <td align=center class="bodyText" colspan="2">
            Based on:<BR>
            <IMG SRC=mrtg-l.png><IMG SRC=mrtg-m.png><IMG SRC=mrtg-r.png><BR>
            Tobias Oetiker
            <A class="graph" HREF="mailto:oetiker@ee.ethz.ch">&lt;oetiker@ee.ethz.ch&gt;</A>
            and Dave Rand
            <A class="graph" HREF="mailto:dlr@bungi.com">&lt;dlr@bungi.com&gt;</A>
          </td>
        </tr>

      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>
