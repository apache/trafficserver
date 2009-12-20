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
      <p>&nbsp;&nbsp;MRTG <@query mrtg> - Detailed Statistics</p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
	<tr>
          <td height="2" colspan="2" class="configureLabel">System Highlights</td>
        </tr>
<!-- ccon start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Client Connections:<BR>
            <A HREF="ccon.html"><IMG HEIGHT=135 WIDTH=500 SRC="ccon-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the client connection count.
          </td>
        </tr>
<!-- ccon end -->
<!-- tps start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Client Transactions Per Second:<BR>
            <A HREF="tps.html"><IMG HEIGHT=135 WIDTH=500 SRC="tps-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of client ops/sec taken every five
            minutes over a ten second window.
          </td>
        </tr>
<!-- tps end -->
<!-- doc start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Document Hit Rate:<BR>
            <A HREF="doc.html"><IMG HEIGHT=135 WIDTH=500 SRC="doc-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute average cache hit rate, including
            fresh hits, and hits successfully revalidated with the origin server.
            The orange curve shows the percentage of hits that were stale, but
            revalidated successfully with the origin server.
          </td>
        </tr>
<!-- doc end -->
<!-- ramcachehit start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            RAM Cache Read I/O Hit Rate:<BR>
            <A HREF="ramcachehit.html"><IMG HEIGHT=135 WIDTH=500 SRC="ramcachehit-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute percentage of all cache read I/Os that
            were satisfied from the RAM cache.
          </td>
        </tr>
<!-- ramcachehit end -->
<!-- eorap start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Errors & Aborts (Percentage):<BR>
            <A HREF="eorap.html"><IMG HEIGHT=135 WIDTH=500 SRC="eorap-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the 5 minute average percentage of errors (blue) and
            aborts (orange).  Aborts are typically less than 5 percent in conditions
            of good network connectivity.
          </td>
        </tr>
<!-- eorap end -->
<!-- hittm start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Cache Hit Latency:<BR>
            <A HREF="hittm.html"><IMG HEIGHT=135 WIDTH=500 SRC="hittm-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The 5 minute average latency (in milliseconds) for cache hits.  Overall
            cache hit latency is shown in blue.  Fresh cache hits are shown in
            orange.  The hit latency is affected by cache load, object size, and
            network conditions.
          </td>
        </tr>
<!-- hittm end -->
<!-- misstm start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Cache Miss Latency:<BR>
            <A HREF="misstm.html"><IMG HEIGHT=135 WIDTH=500 SRC="misstm-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The 5 minute average latency (in milliseconds) for cache misses.  Overall
            miss latency is shown in blue.  Misses that were previously not cached,
            but are now, are shown in orange.  
          </td>
        </tr>
<!-- misstm end -->
<!-- idlcpu start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            CPU Available:<BR>
            <A HREF="idlcpu.html"><IMG HEIGHT=135 WIDTH=500 SRC="idlcpu-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the instantaneous percentage of all CPUs that are not
            actively doing computation, broken into non-busy I/O wait (blue), and
            non-busy, non-I/O idle time (orange).
          </td>
        </tr>
<!-- idlcpu end -->
<!-- tsmem start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            <@record proxy.config.server_name> Memory Usage:<BR>
            <A HREF="tsmem.html"><IMG HEIGHT=135 WIDTH=500 SRC="tsmem-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The amount of memory used by the <@record proxy.config.server_name> process, as 
            reported by the operating system.  Total virtual memory space is shown
            in blue, and the current physically resident size is shown in orange.
          </td>
        </tr>
<!-- tsmem end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel">Transaction Breakdown</td>
        </tr>
<!-- hormp start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Hits & Misses (Percentage):<BR>
            <A HREF="hormp.html"><IMG HEIGHT=135 WIDTH=500 SRC="hormp-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the 5 minute average percentage of hits (blue)
            and misses (orange).
          </td>
        </tr>
<!-- hormp end -->
<!-- eorap start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Errors & Aborts (Percentage):</B><BR>
            <A HREF="eorap.html"><IMG HEIGHT=135 WIDTH=500 SRC="eorap-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the 5 minute average percentage of errors (blue) and
            aborts (orange).  Aborts are typically less than 5 percent in conditions
            of good network connectivity.
          </td>
        </tr>
<!-- eorap end -->
<!-- msie start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            MSIE Requests (Percentage):<BR>
            <A HREF="msie.html"><IMG HEIGHT=135 WIDTH=500 SRC="msie-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the 5 minute average percentage of requests coming from
            Microsoft Internet Explorer browsers (blue), and the percentage of requests
            coming from MSIE browsers that have an IMS or no-cache header (orange).
          </td>
        </tr>
<!-- msie end -->
<!-- bw start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Bandwidth Savings:<BR>
            <A HREF="bw.html"><IMG HEIGHT=135 WIDTH=500 SRC="bw-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the fraction of bytes served from the cache as opposed to
            from the network.  This is sometimes referred to as a "byte hit rate"
            or "hit rate by bytes".  The five minute average is shown in blue, and
            samples of 10 sec averages reported by the manager are shown in orange.
          </td>
        </tr>
<!-- bw end -->
<!-- horm start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Hits & Misses (Count):<BR>
            <A HREF="horm.html"><IMG HEIGHT=135 WIDTH=500 SRC="horm-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the total number of hits (blue) and misses (orange) over
            the last 5 minute interval.
          </td>
        </tr>
<!-- horm end -->
<!-- eora start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Errors & Aborts (Count):<BR>
            <A HREF="eora.html"><IMG HEIGHT=135 WIDTH=500 SRC="eora-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the total number of errors (blue) and aborts (orange)
            over the last 5 minute interval.
          </td>
        </tr>
<!-- eora end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel">Transaction Latency</td>
        </tr>
<!-- hittm start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Cache Hit Latency:<BR>
            <A HREF="hittm.html"><IMG HEIGHT=135 WIDTH=500 SRC="hittm-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The 5 minute average latency (in milliseconds) for cache hits.  Overall
            cache hit latency is shown in blue.  Fresh cache hits are shown in
            orange.  The hit latency is affected by cache load, object size, and
            network conditions.
          </td>
        </tr>
<!-- hittm end -->
<!-- misstm start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Cache Miss Latency:<BR>
            <A HREF="misstm.html"><IMG HEIGHT=135 WIDTH=500 SRC="misstm-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The 5 minute average latency (in milliseconds) for cache misses.  Overall
            miss latency is shown in blue.  Misses that were previously not cached,
            but are now, are shown in orange.  
          </td>
        </tr>
<!-- misstm end -->
<!-- errtm start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Error Latency:<BR>
            <A HREF="errtm.html"><IMG HEIGHT=135 WIDTH=500 SRC="errtm-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The 5 minute average latency (in milliseconds) for errors originating
            at the proxy.  These are typically DNS failures and connect timeouts.
            These do not count server-originating errors, which count as misses.
          </td>
        </tr>
<!-- errtm end -->
<!-- abrtm start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            HTTP Abort Latency:<BR>
            <A HREF="abrtm.html"><IMG HEIGHT=135 WIDTH=500 SRC="abrtm-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The 5 minute average latency (in milliseconds) for requests aborted
            by the client.
          </td>
        </tr>
<!-- abrtm end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel">Object Store Metrics</td>
        </tr>
<!-- cache start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Cache Usage:<BR>
            <A HREF="cache.html"><IMG HEIGHT=135 WIDTH=500 SRC="cache-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The size of the cache in megabytes currently in use is shown in blue.  The orange line
            depicts the total size in megabytes available for caching.
          </td>
        </tr>
<!-- cache end -->
<!-- ramcacheusage start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            RAM Cache Usage:<BR>
            <A HREF="ramcacheusage.html"><IMG HEIGHT=135 WIDTH=500 SRC="ramcacheusage-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the current active size of the RAM cache
            in megabytes.  The orange curve depicts the size of the RAM cache in megabytes
            that are currently locked.
          </td>
        </tr>
<!-- ramcacheusage end -->
<!-- dns start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            DNS Cache Usage:<BR>
            <A HREF="dns.html"><IMG HEIGHT=135 WIDTH=500 SRC="dns-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the total number of DNS requests over the last
            5 minutes.
          </td>
        </tr>
<!-- dns end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel">CPU Utilization</td>
        </tr>
<!-- idlcpu start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            CPU Available:</B><BR>
            <A HREF="idlcpu.html"><IMG HEIGHT=135 WIDTH=500 SRC="idlcpu-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the instantaneous percentage of all CPUs that are not
            actively doing computation, broken into non-busy I/O wait (blue), and
            non-busy, non-I/O idle time (orange).
          </td>
        </tr>
<!-- idlcpu end -->
<!-- cumcpu start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            CPU Busy:<BR>
            <A HREF="cumcpu.html"><IMG HEIGHT=135 WIDTH=500 SRC="cumcpu-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This illustrates the instantaneous percentage of all CPUs that are
            actively processing, either in the kernel (blue), or in
            applications (orange).
          </td>
        </tr>
<!-- cumcpu end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel">Connection Counts</td>
        </tr>
<!-- ccon start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Client Connections:<BR>
            <A HREF="ccon.html"><IMG HEIGHT=135 WIDTH=500 SRC="ccon-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the client connection count.
          </td>
        </tr>
<!-- ccon end -->
<!-- ocon start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Origin Server Connections:<BR>
            <A HREF="ocon.html"><IMG HEIGHT=135 WIDTH=500 SRC="ocon-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the origin server connection count.
          </td>
        </tr>
<!-- ocon end -->
<!-- pcon start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Parent Proxy Connections:<BR>
            <A HREF="pcon.html"><IMG HEIGHT=135 WIDTH=500 SRC="pcon-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the parent proxy connection count.
          </td>
        </tr>
<!-- pcon end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel">Network Statistics</td>
        </tr>
<!-- tcpmbps start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP Throughput:<BR>
            <A HREF="tcpmbps.html"><IMG HEIGHT=135 WIDTH=500 SRC="tcpmbps-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is the total TCP network I/O, in bits/second.  The data is
            measured by netstat.
          </td>
        </tr>
<!-- tcpmbps end -->
<!-- tcpbyt start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP Segment Transmitted:<BR>
            <A HREF="tcpbyt.html"><IMG HEIGHT=135 WIDTH=500 SRC="tcpbyt-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve graphs the number of TCP segment transmitted.  The orange curve 
            demarks the portion of the segment which were re-transmitted due to
            packet loss or timeout.  Under good network conditions, there should
            be very few retransmissions.
          </td>
        </tr>
<!-- tcpbyt end -->
<!-- tcpopn start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP Connect Rate:<BR>
            <A HREF="tcpopn.html"><IMG HEIGHT=135 WIDTH=500 SRC="tcpopn-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the 5 minute average TCP connect rate.  The blue curve shows
            the number of incoming connects/sec, while the orange curve shows the
            number of outgoing connects/sec.
          </td>
        </tr>
<!-- tcpopn end -->
<!-- est start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP ESTABLISHED Connections:<BR>
            <A HREF="est.html"><IMG HEIGHT=135 WIDTH=500 SRC="est-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The instantaneous count of the TCP connections in the ESTABLISHED
            state.  An ESTABLISHED connection is normally exchanging data.
          </td>
        </tr>
<!-- est end -->
<!-- cwt start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP CLOSE_WAIT Connections:<BR>
            <A HREF="cwt.html"><IMG HEIGHT=135 WIDTH=500 SRC="cwt-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The instantaneous count of the TCP connections in the CLOSE_WAIT state.
            CLOSE_WAIT connections have been closed by a remote peer, but the
            <@record proxy.config.server_name> has not closed yet.
          </td>
        </tr>
<!-- cwt end -->
<!-- fw1 start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP FIN_WAIT_1 Connections:<BR>
            <A HREF="fw1.html"><IMG HEIGHT=135 WIDTH=500 SRC="fw1-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The instantaneous count of the TCP connections in the FIN_WAIT_1 state.
            FIN_WAIT_1 connections have been closed by the <@record proxy.config.server_name>, but no
            acknowledgement of the close has yet been received from the remote peer.
          </td>
        </tr>
<!-- fw1 end -->
<!-- fw2 start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP FIN_WAIT_2 Connections:<BR>
            <A HREF="fw2.html"><IMG HEIGHT=135 WIDTH=500 SRC="fw2-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The instantaneous count of the TCP connections in the FIN_WAIT_2 state.
            FIN_WAIT_2 connections have been closed by the <@record proxy.config.server_name>, and
            acknowledged by the remote peer, but the other party has not yet closed.
          </td>
        </tr>
<!-- fw2 end -->
<!-- twait start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP TIME_WAIT Connections:<BR>
            <A HREF="twait.html"><IMG HEIGHT=135 WIDTH=500 SRC="twait-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The instantaneous count of the TCP connections in the TIME_WAIT state.
            TIME_WAIT connections are fully closed (<@record proxy.config.server_name> initiated the
            close), but will still be consuming OS table space for 2MSL seconds.
          </td>
        </tr>
<!-- twait end -->
<!-- lak start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            TCP LAST_ACK Connections:<BR>
            <A HREF="lak.html"><IMG HEIGHT=135 WIDTH=500 SRC="lak-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The instantaneous count of the TCP connections in the LAST_ACK state.
            LAST_ACK connections have been closed first by the remote peer, then
            closed by the <@record proxy.config.server_name>.  The OS
            is awaiting the acknowledgement of the final close.
          </td>
        </tr>
<!-- lak end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel"><@record proxy.config.product_name> Memory Usage</td>
        </tr>
<!-- tsmem start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            <@record proxy.config.server_name> Memory Usage:<BR>
            <A HREF="tsmem.html"><IMG HEIGHT=135 WIDTH=500 SRC="tsmem-<@query mrtg>.png"></A>
         </td>
          <td align=left class="bodyText">
            The amount of memory used by the <@record proxy.config.server_name> process, as 
            reported by the operating system.  Total virtual memory space is shown
            in blue, and the current physically resident size is shown in orange.
          </td>
        </tr>
<!-- tsmem end -->
<!-- tmmem start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            <@record proxy.config.manager_name> Memory Usage:</B><BR>
            <A HREF="tmmem.html"><IMG HEIGHT=135 WIDTH=500 SRC="tmmem-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The amount of virtual memory used by the <@record proxy.config.manager_name> process, as
            reported by the operating system.
          </td>
        </tr>
<!-- tmmem end -->
<!-- realmem start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Real Proxy Memory Usage:</B><BR>
            <A HREF="realmem.html"><IMG HEIGHT=135 WIDTH=500 SRC="realmem-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The amount of virtual memory used by the Real Audio proxy process, as
            reported by the operating system.
          </td>
        </tr>
<!-- realmem end -->

        <!--------------------------- PROTOCOLS -------------------------->
		<!-- NNTP -->
        <tr> 
          <td height="2" colspan="2" class="configureLabel">Protocols</td>
        </tr>
<!-- nccon start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            NNTP Client Connections:<BR>
            <A HREF="nccon.html"><IMG HEIGHT=135 WIDTH=500 SRC="nccon-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the NNTP client/server connection count.
            The blue curve and the orange line represent the client/server connection count, respectively.
          </td>
        </tr>
<!-- nccon end -->
<!-- ndoc start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            NNTP Hit Rate:<BR>
            <A HREF="ndoc.html"><IMG HEIGHT=135 WIDTH=500 SRC="ndoc-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute average NNTP hit rate.
          </td>
        </tr>
<!-- ndoc end -->
<!-- nbw start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            NNTP Bandwidth Savings:<BR>
            <A HREF="nbw.html"><IMG HEIGHT=135 WIDTH=500 SRC="nbw-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the NNTP bandwidth savings percentage.
          </td>
        </tr>
<!-- nbw end -->

		<!-- FTP -->
<!-- fccon start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            FTP Client Connections:<BR>
            <A HREF="fccon.html"><IMG HEIGHT=135 WIDTH=500 SRC="fccon-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the NNTP client/server connection count.
            The blue curve and the orange line represent the client/server connection count, respectively. <i>(not including FTP over HTTP traffic)</i>
          </td>
        </tr>
<!-- fccon end -->
<!-- fdoc start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            FTP Hit Rate:<BR>
            <A HREF="fdoc.html"><IMG HEIGHT=135 WIDTH=500 SRC="fdoc-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute average FTP hit rate. <i>(not including FTP over HTTP traffic)</i>
          </td>
        </tr>
<!-- fdoc end -->
<!-- fbw start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            FTP Bandwidth Savings:<BR>
            <A HREF="fbw.html"><IMG HEIGHT=135 WIDTH=500 SRC="fbw-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the FTP bandwidth savings percentage. <i>(not including FTP over HTTP traffic)</i>
          </td>
        </tr>
<!-- fbw end -->

        <!--------------------- MIXT TPROTOCOLS -------------------------->
        <tr> 
          <td height="2" colspan="2" class="configureLabel">Media Protocols</td>
        </tr>

	<!-- RNI -->
<!-- rniconn start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            RNI Client/Server Connections:<BR>
            <A HREF="rniconn.html"><IMG HEIGHT=135 WIDTH=500 SRC="rniconn-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the RNI client/server connection count.
            The blue curve and the orange line represent the client/server connection count, respectively.
          </td>
        </tr>
<!-- rniconn end -->
<!-- rnicachehit start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            RNI Hit Rate:<BR>
            <A HREF="rnicachehit.html"><IMG HEIGHT=135 WIDTH=500 SRC="rnicachehit-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute average RNI hit rate.
          </td>
        </tr>
<!-- rnicachehit end -->
<!-- rnibw start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            RNI Bandwidth Savings:<BR>
            <A HREF="rnibw.html"><IMG HEIGHT=135 WIDTH=500 SRC="rnibw-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the RNI bandwidth savings percentage.
          </td>
        </tr>
<!-- rnibw end -->


	<!-- WMT -->
<!-- wmtconn start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            WMT Client/Server Connections:<BR>
            <A HREF="wmtconn.html"><IMG HEIGHT=135 WIDTH=500 SRC="wmtconn-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the WMT client/server connection count.
            The blue curve and the orange line represent the client/server connection count, respectively.
          </td>
        </tr>
<!-- wmtconn end -->
<!-- wmtlive start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            WMT Unique Live Stream Connections:<BR>
            <A HREF="wmtlive.html"><IMG HEIGHT=135 WIDTH=500 SRC="wmtlive-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the WMT current unique live stream and on-demand connection count.
            The blue curve and the orange line represent the live stream/on-demand connection count, respectively.
          </td>
        </tr>
<!-- wmtlive end -->
<!-- wmtcachehit start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            WMT Hit Rate:<BR>
            <A HREF="wmtcachehit.html"><IMG HEIGHT=135 WIDTH=500 SRC="wmtcachehit-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute average WMT hit rate.
          </td>
        </tr>
<!-- wmtcachehit end -->
<!-- wmtbw start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            WMT Bandwidth Savings:<BR>
            <A HREF="wmtbw.html"><IMG HEIGHT=135 WIDTH=500 SRC="wmtbw-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the WMT bandwidth savings percentage.
          </td>
        </tr>
<!-- wmtbw end -->

	<!-- QT -->
<!-- qtconn start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            QT Client/Server Connections:<BR>
            <A HREF="qtconn.html"><IMG HEIGHT=135 WIDTH=500 SRC="qtconn-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the QT client/server connection count.
            The blue curve and the orange line represent the client/server connection count, respectively.
          </td>
        </tr>
<!-- qtconn end -->
<!-- qtlive start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            QT Unique Live Stream Connections:<BR>
            <A HREF="qtlive.html"><IMG HEIGHT=135 WIDTH=500 SRC="qtlive-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the QT current unique live stream connection count.
          </td>
        </tr>
<!-- qtlive end -->
<!-- qtcachehit start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            QT Hit Rate:<BR>
            <A HREF="qtcachehit.html"><IMG HEIGHT=135 WIDTH=500 SRC="qtcachehit-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute average QT hit rate.
          </td>
        </tr>
<!-- qtcachehit end -->
<!-- qtbw start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            QT Bandwidth Savings:<BR>
            <A HREF="qtbw.html"><IMG HEIGHT=135 WIDTH=500 SRC="qtbw-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the QT bandwidth savings percentage.
          </td>
        </tr>
<!-- qtbw end -->

	<!-- MPEG4 -->
<!-- mpeg4conn start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            MPEG4 Client/Server Connections:<BR>
            <A HREF="mpeg4conn.html"><IMG HEIGHT=135 WIDTH=500 SRC="mpeg4conn-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the MPEG4 client/server connection count.
            The blue curve and the orange line represent the client/server connection count, respectively.
          </td>
        </tr>
<!-- mpeg4conn end -->
<!-- mpeg4live start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            MPEG4 Unique Live Stream Connections:<BR>
            <A HREF="mpeg4live.html"><IMG HEIGHT=135 WIDTH=500 SRC="mpeg4live-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This is an instantaneous snapshot of the MPEG4 current unique live stream connection count.
          </td>
        </tr>
<!-- mpeg4live end -->
<!-- mpeg4cachehit start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            MPEG4 Hit Rate:<BR>
            <A HREF="mpeg4cachehit.html"><IMG HEIGHT=135 WIDTH=500 SRC="mpeg4cachehit-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The blue curve shows the 5 minute average MPEG4 hit rate.
          </td>
        </tr>
<!-- mpeg4cachehit end -->
<!-- mpeg4bw start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            MPEG4 Bandwidth Savings:<BR>
            <A HREF="mpeg4bw.html"><IMG HEIGHT=135 WIDTH=500 SRC="mpeg4bw-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the MPEG4 bandwidth savings percentage.
          </td>
        </tr>
<!-- mpeg4bw end -->
        <tr> 
          <td height="2" colspan="2" class="configureLabel">Cache Disk Statistics</td>
        </tr>
 <!-- cacheread start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Number of Cache Read:<BR>
            <A HREF="cacheread.html"><IMG HEIGHT=135 WIDTH=500 SRC="cacheread-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The graph shows the number of cache reads per second.
          </td>
        </tr>
<!-- cacheread end -->
<!-- cachewrite start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Number of Cache Writes:<BR>
            <A HREF="cachewrite.html"><IMG HEIGHT=135 WIDTH=500 SRC="cachewrite-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The graph shows the number of cache writes per second.
          </td>
        </tr>
<!-- cachewrite end -->
<!-- cachereadkb start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Kilobytes of Cache Read:<BR>
            <A HREF="cachereadkb.html"><IMG HEIGHT=135 WIDTH=500 SRC="cachereadkb-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The graph shows the number of kilobytes read from the cache per second.
          </td>
        </tr>
<!-- cachereadkb end -->
<!-- cachewritekb start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Kilobytes of Cache Writes:<BR>
            <A HREF="cachewritekb.html"><IMG HEIGHT=135 WIDTH=500 SRC="cachewritekb-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            The graph shows the number of kilobytes written to cache per second.
          </td>
        </tr>
<!-- cachewritekb end -->
      <tr> 
          <td height="2" colspan="2" class="configureLabel">Internal Statistics</td>
        </tr>
<!-- netread start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Network Reads:<BR>
            <A HREF="netread.html"><IMG HEIGHT=135 WIDTH=500 SRC="netread-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the total number of network reads performed (blue) over the
            last 5 minutes, along with the number of network reads that were wasted,
            returning no data (orange).
          </td>
        </tr>
<!-- netread end -->
<!-- netwrt start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Network Writes:<BR>
            <A HREF="netwrt.html"><IMG HEIGHT=135 WIDTH=500 SRC="netwrt-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            This shows the total number of network writes performed (blue) over the
            last 5 minutes, along with the number of network writes that wrote no
            data (orange).
          </td>
        </tr>
<!-- netwrt end -->

        <tr> 
          <td height="2" colspan="2" class="configureLabel">Duty Cycles</td>
        </tr>
<!-- uptime start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            Server Duty Cycle:<BR>
            <A HREF="uptime.html"><IMG HEIGHT=135 WIDTH=500 SRC="uptime-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
            Duty cycle for <@record proxy.config.product_name> components in seconds.
          </td>
        </tr>
<!-- uptime end -->
<!-- mrtgruntime start -->
        <tr align="center"> 
          <td align=center class="bodyText">
            MRTG Runtime:<BR>
            <A HREF="mrtgruntime.html"><IMG HEIGHT=135 WIDTH=500 SRC="mrtgruntime-<@query mrtg>.png"></A>
          </td>
          <td align=left class="bodyText">
       Wall-clock time elapsed while gathering statistics and generating graphs. 
       The blue curve depicts the wall-clock time elapsed while completing the 
       previous MRTG cycle, and should be well below the sampling 
       interval (5 minutes). 
       The orange curve represents the wall-clock time elapsed while completing 
       just the graph generation phase of the previous MRTG cycle.
       NOTE: arbitrary delays are added to prevent MRTG consuming 100% of CPU time.
          </td>
        </tr>
<!-- mrtgruntime end -->
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
