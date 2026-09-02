"""Verify a PURGE traverses every slice block, not just the ones before a gap."""

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
    Condition.PluginExists('header_rewrite.so'),
)
Test.ContinueOnFail = True


class SlicePurgeGapsTest:
    """Verify a PURGE removes every block it was asked for.

    Slice turns one client PURGE into one PURGE per block. The walk used to stop
    at the first block that was not cached, because it had no other end
    condition: m_contentlen was only ever set from a 206's Content-Range, a PURGE
    response carried none, and so m_req_range.blockIsInside() was true for every
    block number. The 404 stood in for a length the plugin never learned.

    ATS now reports the removed object's extent as X-Purged-Content-Range, so the
    walk learns where the object ends from the blocks it is already deleting, and
    a 404 is merely noted and stepped over. The requested range bounds the walk
    throughout; until some block reports an extent, an open ended one is bounded
    only by a limit on consecutive misses. A block response that is neither 200 nor
    404 is a failure rather than an absence, and says nothing about the blocks
    behind it, so the walk stops there and reports that status.

    Each object below exercises one thing, and each is purged exactly once,
    because a purge consumes the state it is measured against.

    /hole      blocks 0 and 2 cached, block 1 absent. A 404 mid-walk must not end
               it, so block 2 goes too.
    /nofirst   only block 1 cached. The walk steps over block 0's 404, picks the
               extent up from block 1, and the client must not be handed that 404.
    /mixed     blocks reporting a 30 byte and a 50 byte object, as happens when
               the origin object is replaced in place. The walk must follow the
               largest extent reported, not the first.
    /ranged    same gap as /hole, purged with a closed range. Already bounded, so
               this isolates the 404 handling from the extent discovery.
    /openend   purged with "bytes=20-". The start is stated, so only block 2 may
               go and block 0 must survive.
    /outside   only block 1 cached, purged for block 0 alone. No block is removed,
               so no extent is ever reported and only the range can end the walk
               before it reaches a block the client did not name.
    /endbytes  purged with "bytes=-10". A suffix range cannot know which block it
               starts at, so it is widened to the whole object and every block
               goes.
    /sparse    only block 4 cached, on a proxy whose miss bound is 2, so the
               default walk cannot reach it. Covers the bound, its per-request
               override, and the fallback when the override is malformed.
    /badrange  purged with an unparseable range, which must be refused rather
               than silently applied to block 0.
    /failblock blocks 0 and 3 cached, with a 500 injected for block 1's PURGE. Block
               0 is removed, the walk stops at block 1, block 3 is left alone, and
               the client hears the 500 rather than the 200 block 0 earned.
    /denied    block 0 cached, with a 403 injected for every block PURGE, as
               ip_allow refusing PURGE would. The walk stops at its first block, so
               nothing is removed and the client must not be told 404.

    Whether a block was purged is measured on the origin, not on the response
    body: the origin serves each check phase exactly what the matching fill phase
    served, so a purged block and a surviving block are indistinguishable to the
    client, and the only difference is whether the origin was asked again.
    """

    _client_replay: str = 'replay/slice_purge_gaps_client.replay.yaml'
    _server_replay: str = 'replay/slice_purge_gaps_server.replay.yaml'

    # The core answers a block PURGE with nothing but 200 or 404, so a failure has to
    # be injected to be tested at all.
    _fail_rules: str = 'purge_block_failure.conf'

    _block_bytes: int = 10

    # Low enough that the default walk cannot reach /sparse's block 4, which is
    # what makes the per-request override observable.
    _low_miss_bound: int = 2

    # Keyed on the block range as well as the phase uuid, so one origin
    # transaction answers one block of one phase.
    _origin_key_format: str = '--format "{url}{field.range}{field.uuid}"'

    def __init__(self) -> None:
        """Declare the origin and the two proxies."""
        self._started = False
        self._configure_origin()
        self._ts = self._make_ts('ts', fail_rules=True)
        self._ts_bound = self._make_ts('ts-bound', miss_bound=self._low_miss_bound)

        self._ts.Disk.traffic_out.Content = Testers.ContainsExpression(
            'Purge suffix range widened to the whole object',
            'A suffix range purge should be widened rather than guessing at its start.')

        self._ts_bound.Disk.traffic_out.Content = Testers.ContainsExpression(
            f'gave up after {self._low_miss_bound} consecutive uncached block',
            'The walk should stop at its configured miss bound rather than scanning the whole range.')
        self._ts_bound.Disk.diags_log.Content = Testers.ContainsExpression(
            'Ignoring invalid X-Slice-Purge-Probe', 'A malformed override should be rejected, not acted on.')

        # A purge issues nothing but block PURGEs. request_block logs every request
        # header it builds at debug, so an only-if-cached here would mean a
        # read-only length probe had been reintroduced.
        for ts in (self._ts, self._ts_bound):
            ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
                'only-if-cached', 'A purge should not issue a read-only length probe.')

    def _configure_origin(self) -> None:
        """Configure the origin."""
        self._origin = Test.MakeVerifierServerProcess('origin', self._server_replay, other_args=self._origin_key_format)

        # ATS answers PURGE itself and the plugin issues no other request kind, so
        # neither may ever be seen upstream.
        self._origin.Streams.stdout += Testers.ExcludesExpression(
            'PURGE', 'A PURGE should be answered by ATS and never forwarded to the origin.')
        self._origin.Streams.stdout += Testers.ExcludesExpression('HEAD /', 'A purge should never issue a HEAD upstream.')

    def _make_ts(self, label: str, miss_bound: int = None, fail_rules: bool = False) -> 'Process':
        """Create a proxy that slices in front of cache_range_requests.

        --ref-relative keeps a ranged GET from dragging block 0 in as a reference
        block, which is what lets a fill phase leave a chosen block uncached.

        :param label: process name suffix.
        :param miss_bound: --purge-probe-blocks value, or None for the default.
        :param fail_rules: load the header_rewrite rules that fail a block PURGE.
        """
        ts = Test.MakeATSProcess(label, enable_cache=True)

        rules = ''
        if fail_rules:
            ts.Setup.CopyAs(f'rules/{self._fail_rules}', Test.RunDirectory)
            rules = f' @plugin=header_rewrite.so @pparam={Test.RunDirectory}/{self._fail_rules}'

        bound = '' if miss_bound is None else f' @pparam=--purge-probe-blocks={miss_bound}'
        ts.Disk.remap_config.AddLine(
            f'map http://slice/ http://127.0.0.1:{self._origin.Variables.http_port}/'
            f'{rules} @plugin=slice.so @pparam=--blockbytes-test={self._block_bytes} @pparam=--ref-relative{bound}'
            ' @plugin=cache_range_requests.so')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'slice|cache_range_requests',
            })
        return ts

    def _run(self, summary: str, phases: str, ts: 'Process' = None) -> None:
        """Add a TestRun replaying one or more phases, in replay file order.

        :param summary: TestRun description.
        :param phases: space separated verifier-client keys.
        :param ts: proxy to replay against, defaulting to the ordinary one.
        """
        ts = self._ts if ts is None else ts
        tr = Test.AddTestRun(summary)
        if not self._started:
            tr.Processes.Default.StartBefore(self._origin)
            tr.Processes.Default.StartBefore(self._ts)
            tr.Processes.Default.StartBefore(self._ts_bound)
            self._started = True
        tr.AddVerifierClientProcess(
            f"client-{phases.replace(' ', '-')}", self._client_replay, http_ports=[ts.Variables.port], keys=phases)
        tr.StillRunningAfter = ts

    def _origin_saw(self, phase: str, block: str, why: str) -> None:
        """Assert the origin was asked for a block under a given phase."""
        self._origin.Streams.stdout += Testers.ContainsExpression(f'request with key /{block}{phase}', why)

    def _fill(self, summary: str, url: str, blocks: list, ts: 'Process' = None) -> None:
        """Cache the given blocks of an object, and prove each fill happened.

        The fill assertions carry weight: a check phase only observes that the
        origin was asked, which is equally true of a block that was purged and one
        that was never cached at all. Asserting the fill reached the origin is what
        makes the later check mean "removed" rather than merely "absent".
        """
        self._run(summary, ' '.join(f'{url}-fill-{block}' for block in blocks), ts)
        for block in blocks:
            self._origin_saw(
                f'{url}-fill-{block}', f'{url}bytes={block * 10}-{block * 10 + 9}',
                f'Block {block} of /{url} should have been fetched and cached.')

    def _purged(self, phase: str, block: str, why: str) -> None:
        """Assert a phase's block request reached the origin, so it was purged."""
        self._origin_saw(phase, block, why)

    def _survived(self, phase: str, block: str, why: str) -> None:
        """Assert a phase's block request never reached the origin, so it survived.

        The origin has no transaction registered for such a phase either, so a
        wrongly purged block fails the client's own expectation as well.
        """
        self._origin.Streams.stdout += Testers.ExcludesExpression(f'request with key /{block}{phase}', why)

    def _gap_mid_walk(self) -> None:
        """A 404 in the middle of the walk must not end it."""
        self._fill('Cache blocks 0 and 2 of /hole, leaving block 1 uncached', 'hole', [0, 2])
        self._run('PURGE the whole /hole object', 'hole-purge')
        self._run('Both cached blocks of /hole were purged', 'hole-check-0 hole-check-2')
        self._purged('hole-check-0', 'holebytes=0-9', 'Block 0 is in front of the gap, so it should be purged.')
        self._purged('hole-check-2', 'holebytes=20-29', 'A PURGE should traverse blocks behind an uncached one.')

    def _uncached_first_block(self) -> None:
        """A miss on the first block must not stop the purge before it starts."""
        self._fill('Cache only block 1 of /nofirst', 'nofirst', [1])
        self._run('PURGE /nofirst, whose block 0 is not cached', 'nofirst-purge')
        self._run('Block 1 of /nofirst was purged', 'nofirst-check-1')
        self._purged('nofirst-check-1', 'nofirstbytes=10-19', 'An uncached first block should not stop the purge.')

    def _largest_extent_wins(self) -> None:
        """A block reporting a longer object widens the walk."""
        self._fill('Cache blocks 0, 1 and 4 of /mixed, disagreeing about its length', 'mixed', [0, 1, 4])
        self._run('PURGE /mixed', 'mixed-purge')
        self._run('Block 4 of /mixed was purged, so the walk took the longer extent', 'mixed-check-4')
        self._purged('mixed-check-4', 'mixedbytes=40-49', 'The walk should follow the largest extent any block reports.')

    def _closed_range(self) -> None:
        """A closed range bounds the walk itself, isolating the 404 step-over."""
        self._fill('Cache blocks 0 and 2 of /ranged, leaving block 1 uncached', 'ranged', [0, 2])
        self._run('PURGE /ranged with a closed range spanning the whole object', 'ranged-purge')
        self._run('Both cached blocks of /ranged were purged', 'ranged-check-0 ranged-check-2')
        self._purged('ranged-check-0', 'rangedbytes=0-9', 'A closed range purge should remove the blocks it covers.')
        self._purged('ranged-check-2', 'rangedbytes=20-29', 'A 404 should not end a closed range purge either.')

    def _open_ended_range(self) -> None:
        """A "bytes=N-" purge states its start, so it purges only what it names."""
        self._fill('Cache blocks 0 and 2 of /openend', 'openend', [0, 2])
        self._run('PURGE /openend from byte 20 on', 'openend-purge')
        self._run('Block 2 of /openend went and block 0 stayed', 'openend-check-2 openend-check-0')
        self._purged('openend-check-2', 'openendbytes=20-29', 'The block covering the range should be purged.')
        self._survived('openend-check-0', 'openendbytes=0-9', 'A purge should not remove blocks before its stated start.')

    def _range_bounds_the_walk(self) -> None:
        """A range ends the walk even before any block has reported an extent.

        /outside has only block 1 cached and is purged for block 0 alone. Nothing
        is ever removed, so no block reports an extent, and the miss bound would
        otherwise carry the walk past the end of the named range and into a block
        the client never asked to remove.
        """
        self._fill('Cache only block 1 of /outside', 'outside', [1])
        self._run('PURGE /outside for block 0 only, which is not cached', 'outside-purge')
        self._run('Block 1 of /outside survived a purge that did not name it', 'outside-check-1')
        self._survived(
            'outside-check-1', 'outsidebytes=10-19', 'A purge should stop at the end of its range, not at the miss bound.')

    def _suffix_range(self) -> None:
        """A "bytes=-N" purge is widened to the whole object."""
        self._fill('Cache blocks 0 and 2 of /endbytes', 'endbytes', [0, 2])
        self._run('PURGE the last 10 bytes of /endbytes', 'endbytes-purge')
        self._run('Every cached block of /endbytes went, not just the named tail', 'endbytes-check-0 endbytes-check-2')
        self._purged('endbytes-check-2', 'endbytesbytes=20-29', 'The block covering the suffix range must be purged.')
        self._purged(
            'endbytes-check-0', 'endbytesbytes=0-9',
            'A widened suffix purge removes the whole object, which is a superset of what was named.')

    def _miss_bound_and_override(self) -> None:
        """The miss bound stops a walk that has found nothing, and is overridable.

        /sparse has only block 4 of five cached, out of reach of this proxy's
        configured bound of two. Nothing about the remap changes between the two
        purges below; only the request header does.
        """
        ts = self._ts_bound
        self._fill('Cache only block 4 of /sparse, out of reach of the configured bound', 'sparse', [4], ts)

        self._run('A purge with a malformed override falls back to the configured bound', 'sparse-purge-narrow', ts)
        self._run('Block 4 of /sparse survived the too-narrow purge', 'sparse-check-alive', ts)
        self._survived('sparse-check-alive', 'sparsebytes=40-49', 'A walk that gave up before block 4 should not have purged it.')

        self._run('PURGE /sparse with an override wide enough to reach block 4', 'sparse-purge-wide', ts)
        self._run('Block 4 of /sparse was purged once the bound reached it', 'sparse-check-gone', ts)
        self._purged('sparse-check-gone', 'sparsebytes=40-49', 'A request supplied bound should let the walk reach block 4.')

    def _unparseable_range(self) -> None:
        """A purge whose range cannot be parsed is refused, not guessed at.

        An unparseable range leaves the plugin's range covering block 0 only, so
        walking it would delete the head of the object and report success. A purge
        is destructive, so it is rejected instead.
        """
        self._fill('Cache block 0 of /badrange', 'badrange', [0])
        self._run('A PURGE with an unparseable range is refused', 'badrange-purge')
        self._run('Block 0 of /badrange survived the refused purge', 'badrange-check-0')
        self._survived('badrange-check-0', 'badrangebytes=0-9', 'A refused purge must not have removed anything.')
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            'Refusing PURGE with an unparseable range', 'The refusal should be visible in the error log.')

    def _block_failure(self) -> None:
        """A block that could not be purged is not a block that was absent.

        Only a 404 says the block was not cached. Any other status says the walk could
        not tell, and says nothing about the blocks behind it either, so it stops there
        and the client hears that status rather than the 200 the earlier blocks earned.
        Both failures are injected with header_rewrite, since the core answers a block
        PURGE with nothing but 200 or 404 on its own.
        """
        self._fill('Cache blocks 0 and 3 of /failblock', 'failblock', [0, 3])
        self._run('PURGE /failblock, whose block 1 answers 500', 'failblock-purge')
        self._run('/failblock was purged up to the failing block only', 'failblock-check-0 failblock-check-3')
        self._purged('failblock-check-0', 'failblockbytes=0-9', 'A block removed before the failure should stay removed.')
        self._survived(
            'failblock-check-3', 'failblockbytes=30-39',
            'The walk should stop at the failing block, leaving what is behind it cached.')
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            'Purge of block 1 failed', 'The block that ended the walk should be logged.')

        self._fill('Cache block 0 of /denied', 'denied', [0])
        self._run('PURGE /denied, whose every block answers 403', 'denied-purge')
        self._run('Block 0 of /denied survived the refused purge', 'denied-check-0')
        self._survived('denied-check-0', 'deniedbytes=0-9', 'A purge refused at its first block must not remove anything.')
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            'Purge of block 0 failed', 'A purge refused outright should be logged rather than reported as a 404.')

    def run(self) -> None:
        """Configure the test runs."""
        self._gap_mid_walk()
        self._uncached_first_block()
        self._largest_extent_wins()
        self._closed_range()
        self._open_ended_range()
        self._range_bounds_the_walk()
        self._suffix_range()
        self._unparseable_range()
        self._miss_bound_and_override()
        self._block_failure()


SlicePurgeGapsTest().run()
