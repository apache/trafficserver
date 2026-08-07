"""Verify slice serves a stale object identity after the origin object changes."""

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

Test.Summary = __doc__

Test.SkipUnless(
    Condition.PluginExists('slice.so'),
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False


class SliceHierarchyTest:
    """Build the child/parent hierarchy the incident ran on.

    Both tiers load slice with the same block size, and both put
    cache_range_requests behind it, as the affected property does.

    The parent only behaves as a slicing proxy for requests that arrive without
    slice's skip header. The child stamps that header onto every block request it
    issues (client.cc:66), so slice returns immediately on the parent
    (slice.cc:48) and a child block request is handled there by
    cache_range_requests alone: look up this exact Range, forward it on a miss,
    store whatever 206 comes back. The parent therefore holds N independent
    per-Range objects with no shared identity, and cannot notice, refuse or
    reconcile a version mix. That is where the mixed set lived in the incident.

    A client hitting the parent directly is a different path: the parent does
    slice that request, forms its own reference block and clamps against it.
    """

    _server_replay: str = 'replay/slice_stale_generation_server.replay.yaml'
    _client_replay: str = 'replay/slice_stale_generation_client.replay.yaml'

    _block_bytes: int = 16

    _origin_key_format: str = '--format "{url}{field.range}{field.uuid}"'

    def __init__(self, name: str) -> None:
        """Declare the origin, the parent and the child.

        :param name: suffix distinguishing this hierarchy's processes.
        """
        self._name = name
        self._configure_dns()
        self._configure_origin()
        self._configure_parent()
        self._configure_child()

    def _configure_dns(self) -> None:
        """Configure a DNS server so neither tier consults resolv.conf."""
        self._dns = Test.MakeDNServer(f'dns-{self._name}', default='127.0.0.1')

    def _configure_origin(self) -> None:
        """Configure the origin.

        The server is keyed on the block's byte range and on the phase uuid that
        slice propagates from the client request, so one replay file answers
        every block request of every phase and can replace the object between
        phases without holding any state.
        """
        self._origin = Test.MakeVerifierServerProcess(
            f'origin-{self._name}', self._server_replay, other_args=self._origin_key_format)

    def _slice_remap(self, source: str, upstream: str) -> str:
        """Build a remap rule carrying slice in front of cache_range_requests."""
        return (
            f'map {source} {upstream}'
            f' @plugin=slice.so @pparam=--blockbytes-test={self._block_bytes}'
            ' @plugin=cache_range_requests.so')

    def _records(self, ts: 'Process', debug: int) -> None:
        """Apply the records.yaml settings common to both tiers."""
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': debug,
                'proxy.config.diags.debug.tags': 'slice|cache_range_requests',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.http.parent_proxy.self_detect': 0,
            })

    def _configure_parent(self) -> None:
        """Configure the parent, which the child's block requests reach."""
        self._parent = Test.MakeATSProcess(f'ts-parent-{self._name}')
        self._parent.Disk.remap_config.AddLine(
            self._slice_remap('http://origin.test/', f'http://127.0.0.1:{self._origin.Variables.http_port}/'))
        self._parent.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache')
        self._records(self._parent, debug=1)

        # slice is loaded on the parent, but every block request the child sends
        # carries the skip header, so slice returns immediately and the request
        # is handled by cache_range_requests alone. Plugin debug output lands in
        # traffic.out, not diags.log.
        self._parent.Disk.traffic_out.Content = Testers.ContainsExpression(
            'slice passing GET or HEAD request through to next plugin',
            "The child's block requests should bypass the parent's slice.")
        self._parent.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'slice accepting and slicing', 'The parent should never slice a child block request.')

    def _make_child(self, label: str) -> 'Process':
        """Create a child tier that slices and forwards to the parent."""
        ts = Test.MakeATSProcess(f'ts-{label}-{self._name}')
        ts.Disk.remap_config.AddLine(self._slice_remap('http://slice/', 'http://origin.test/'))
        ts.Disk.parent_config.AddLine(
            f'dest_domain=. parent=127.0.0.1:{self._parent.Variables.port}'
            ' round_robin=consistent_hash go_direct=false')
        ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache')
        self._records(ts, debug=1)
        return ts

    def _configure_child(self) -> None:
        """Configure the child, which slices and forwards to the parent."""
        self._child = self._make_child('child')

    def _start_hierarchy(self, tr: 'TestRun') -> None:
        """Bring up origin, parent and child for the first TestRun."""
        tr.Processes.Default.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._parent)
        tr.Processes.Default.StartBefore(self._child)

    def _replay_phase(self, tr: 'TestRun', phase: str, ts: 'Process' = None) -> 'Process':
        """Replay the client transaction for one phase against a child."""
        ts = self._child if ts is None else ts
        return tr.AddVerifierClientProcess(
            f'client-{phase}-{self._name}', self._client_replay, http_ports=[ts.Variables.port], keys=phase)

    def _still_running(self, tr: 'TestRun') -> None:
        """Assert both tiers survive the TestRun."""
        tr.StillRunningAfter = self._child
        tr.StillRunningAfter = self._parent


class SliceStaleGenerationTest(SliceHierarchyTest):
    """Verify a cached reference block pins a stale object identity.

    The plugin takes the whole object length from the reference block's
    Content-Range and clips the client range to it, so the cached reference
    block, not the origin, defines the object's identity for every request::

        server.cc handleFirstServerHeader:
            data->m_contentlen = blockcr.m_length;
            data->m_req_range.m_end = std::min(data->m_contentlen, data->m_req_range.m_end);

    Replacing the object under the same URL therefore forks every cache into one
    that filled before the replacement and one that filled after, for as long as
    the reference block stays fresh. On the stale side, ranges that exist in the
    current object are answered against the stale length with the stale ETag:
    clipped short, or refused with a 416. Neither path logs a block stitch error,
    because handleNextServerHeader only complains when blocks disagree with each
    other and here they are uniformly stale.

    Modelled on an incident where a versioned, year-cacheable object was replaced
    in place. Two edges seven hours apart on either side of the replacement served
    object lengths 4043309056 and 7031250004 for the same URL, the stale one
    clipping a 64 MiB range request down to 16 MiB.
    """

    # The reference block the origin holds at the new generation for each phase
    # that reads the cached object. The test asserts the origin never gets these.
    _unreachable_keys = ('/objbytes=0-15clipped', '/objbytes=0-15unsatisfiable')

    def __init__(self) -> None:
        """Declare the hierarchy and its assertions."""
        super().__init__('stale')

        for key in self._unreachable_keys:
            self._origin.Streams.stdout += Testers.ExcludesExpression(
                f'request with key {key}', 'The stale object should never be refetched after the origin object changed.')

        # Debug is enabled on the child, so Config::canLogError cannot suppress a
        # block stitch error by pacing. The stale response is served with none.
        self._child.Disk.diags_log.Content = Testers.ExcludesExpression(
            'logSliceError', 'The stale response should be served with no block stitch error.')
        self._child.Disk.diags_log.Content += Testers.ExcludesExpression(
            'Mismatch/Bad block Content-Range', 'The stale blocks agree with each other, so nothing should mismatch.')

    def _fill_cache(self) -> None:
        """Cache the whole object while the origin holds the first generation."""
        tr = Test.AddTestRun('Cache the object while the origin holds the first generation')
        self._start_hierarchy(tr)
        self._replay_phase(tr, 'fill')
        self._still_running(tr)

    def _verify_clipped_range(self) -> None:
        """A range inside the current object is clipped to the stale length."""
        tr = Test.AddTestRun('A range inside the current object is clipped to the stale length')
        self._replay_phase(tr, 'clipped')
        self._still_running(tr)

    def _verify_unsatisfiable_range(self) -> None:
        """A range past the stale length is refused with a 416."""
        tr = Test.AddTestRun('A range past the stale length is refused with a 416')
        self._replay_phase(tr, 'unsatisfiable')
        self._still_running(tr)

    def _verify_uncached_object(self) -> None:
        """An object first fetched after the replacement is served correctly."""
        tr = Test.AddTestRun('An object first fetched after the replacement is served correctly')
        self._replay_phase(tr, 'control')
        self._still_running(tr)

    def run(self) -> None:
        """Configure the test runs."""
        self._fill_cache()
        self._verify_clipped_range()
        self._verify_unsatisfiable_range()
        self._verify_uncached_object()


class SliceMixedGenerationTest(SliceHierarchyTest):
    """Verify the parent stores a version mix and the child cannot recover.

    The other failure mode from the same origin object replacement, and the one
    the parent's per-Range cache makes possible. Only the reference block is
    refetched after the replacement, so the parent ends up holding two blocks of
    one object at two different generations, both fresh, with nothing to relate
    them. It serves each on request without complaint.

    The child is the only tier that compares blocks, and only against its own
    reference block. It forms the client response header from the reference block,
    which is correct for the current object, and only then discovers that the
    interior block belongs to the previous one::

        server.cc handleNextServerHeader:
            if (!blockcr.isValid() || blockcr.m_length != data->m_contentlen) {
              logSliceError("Mismatch/Bad block Content-Range", data, header);

    The self heal refetches the reference block, which is already the newest one
    the parent holds, so the same block comes back and the interior block still
    disagrees with it. The second mismatch is where slice gives up.

    The upstream is aborted. Slice can abort but cannot evict, so the mixed pair
    on the parent survives. The final TestRun proves where the damage actually
    lives: a second child with a completely cold cache, pointed at the same
    parent, fails identically. It never saw the previous generation; it simply
    inherits the mix from the one place that holds it. That is the incident's
    shape, where all 105 blocks hashed to a single parent and every one of the 32
    child nodes served the same broken object.
    """

    # The reference block is cached with a one second lifetime, so let it expire.
    _expiry_wait: int = 2

    def __init__(self) -> None:
        """Declare the hierarchy and its assertions."""
        super().__init__('mixed')
        self._cold_child = self._make_child('cold-child')

        # Unlike the stale case, the child does report this one: first the interior
        # block against the reference block, then the refetch against the interior.
        self._child.Disk.diags_log.Content = Testers.ContainsExpression(
            'Mismatch/Bad block Content-Range.*blk_range="16-31".*etag_got="%22v1%22"',
            'The interior block should disagree with the reference block.')
        self._child.Disk.diags_log.Content += Testers.ContainsExpression(
            'Mismatch/Bad block Content-Range.*blk_range="0-15".*etag_got="%22v2%22"',
            'The refetched reference block should disagree in turn, leaving no way out.')

        # The parent never compares blocks, so it never complains about the mix
        # it is storing and serving to the child.
        self._parent.Disk.diags_log.Content += Testers.ExcludesExpression(
            'logSliceError', 'The parent should not notice the version mix it holds.')
        self._parent.Disk.diags_log.Content += Testers.ExcludesExpression(
            'Mismatch/Bad block Content-Range', 'The parent should not compare blocks at all.')

    def _fill_interior_block(self) -> None:
        """Cache an interior block at the first generation, on both tiers."""
        tr = Test.AddTestRun('Cache an interior block at the first generation')
        self._start_hierarchy(tr)
        self._replay_phase(tr, 'fill-interior')
        self._still_running(tr)

    def _verify_aborted_response(self) -> None:
        """The reference block moves on and the transaction cannot be completed."""
        tr = Test.AddTestRun('A mixed generation object cannot be delivered and the request fails')
        client = self._replay_phase(tr, 'mixed')
        # Let the reference block go stale so that only it is revalidated.
        tr.Processes.Default.Command = f'sleep {self._expiry_wait}; ' + tr.Processes.Default.Command
        # Slice aborts the transaction, so the client never reads a response at
        # all: not a short body, no response header. verifier-client exits 1.
        client.ReturnCode = 1
        client.Streams.stdout += Testers.ContainsExpression(
            'Failed to find a well-formed, completed HTTP response: PARSE_INCOMPLETE',
            'The client should not receive a parsable response.')
        client.Streams.stdout += Testers.ContainsExpression(
            'Failed HTTP/1 transaction with key: mixed', 'The transaction should fail.')
        self._still_running(tr)

    def _verify_mix_is_on_the_parent(self) -> None:
        """A cold child fails identically, because the mix lives on the parent."""
        tr = Test.AddTestRun('A second child with a cold cache inherits the mix from the parent')
        tr.Processes.Default.StartBefore(self._cold_child)
        client = self._replay_phase(tr, 'cold-child', ts=self._cold_child)
        client.ReturnCode = 1
        client.Streams.stdout += Testers.ContainsExpression(
            'Failed HTTP/1 transaction with key: cold-child', 'A node that never saw the old generation should fail the same way.')
        self._cold_child.Disk.diags_log.Content = Testers.ContainsExpression(
            'Mismatch/Bad block Content-Range', 'The cold child should hit the same mismatch.')
        tr.StillRunningAfter = self._cold_child
        self._still_running(tr)

    def run(self) -> None:
        """Configure the test runs."""
        self._fill_interior_block()
        self._verify_aborted_response()
        self._verify_mix_is_on_the_parent()


SliceStaleGenerationTest().run()
SliceMixedGenerationTest().run()
