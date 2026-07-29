#!/usr/bin/env python3
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Generate a changelog from merged PRs in a GitHub milestone.

Usage:
    uv run --project tools/changelog python tools/changelog/changelog.py \
        -o apache -r trafficserver -m 10.2.0

Output is written to stdout in the format used by CHANGELOG-* files:

    Changes with Apache Traffic Server 10.2.0
      #11945 - Make directory operations methods on `Directory`
      #12026 - Static link opentelemetry-cpp libraries to otel_tracer plugin
      ...

To generate a changelog file for a release:

    uv run --project tools/changelog python tools/changelog/changelog.py \
        -o apache -r trafficserver -m 10.2.0 > CHANGELOG-10.2.0

Use --doc to include extra metadata (merge commit SHA, full commit message,
labels) for each PR, useful for generating release documentation:

    uv run --project tools/changelog python tools/changelog/changelog.py \
        -o apache -r trafficserver -m 10.2.0 --doc --format yaml > changelog.yaml

For security releases whose fixes land directly on a release branch without
public pull requests (and therefore never appear in a milestone), use
--from-git to source the changelog from a git commit range. This is merged
with the milestone PRs (deduplicated by PR number) so the result includes both
the direct-to-branch commits and any milestone PRs. The -m value is still used
as the version label in the output:

    uv run --project tools/changelog python tools/changelog/changelog.py \
        -o apache -r trafficserver -m 10.1.4 \
        --from-git 10.1.3..upstream/10.1.x

Requires a GitHub token via GH_TOKEN env var or -a flag to avoid rate limits.
"""

import argparse
import json
import os
import re
import subprocess
import sys

import httpx

try:
    import yaml
except ImportError:
    yaml = None

API_URL = "https://api.github.com"


def gh_cli_available() -> bool:
    try:
        subprocess.run(["gh", "--version"], capture_output=True, check=True)
        return True
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False


def changelog_via_gh(owner: str, repo: str, milestone: str, verbose: bool, doc: bool) -> list[dict]:
    """Use the gh CLI to fetch milestone PRs (avoids API rate limits)."""
    milestone_id = None
    result = subprocess.run(
        ["gh", "api", f"/repos/{owner}/{repo}/milestones?state=all", "--paginate"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"gh api error: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    milestones = json.loads(result.stdout)
    for ms in milestones:
        if ms["title"] == milestone:
            milestone_id = ms["number"]
            break

    if milestone_id is None:
        print(f"Milestone not found: {milestone}", file=sys.stderr)
        sys.exit(1)

    print(f"Looking for issues from Milestone {milestone}", file=sys.stderr)

    changelog = []
    page = 1
    while True:
        print(f"Page {page}", file=sys.stderr)
        result = subprocess.run(
            [
                "gh",
                "api",
                f"/repos/{owner}/{repo}/issues?milestone={milestone_id}&state=closed&page={page}&per_page=100",
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"gh api error: {result.stderr}", file=sys.stderr)
            sys.exit(1)

        issues = json.loads(result.stdout)
        if not issues:
            break

        for issue in issues:
            number = issue["number"]
            title = issue["title"]
            if verbose:
                print(f"Issue #{number} - {title} ", end="", file=sys.stderr)

            if "pull_request" not in issue:
                if verbose:
                    print("not a PR.", file=sys.stderr)
                continue

            merge_result = subprocess.run(
                ["gh", "api", f"/repos/{owner}/{repo}/pulls/{number}/merge"],
                capture_output=True,
                text=True,
            )
            if merge_result.returncode != 0:
                if verbose:
                    print("not merged.", file=sys.stderr)
                continue

            if verbose:
                print("added.", file=sys.stderr)

            entry: dict = {"number": number, "title": title}
            if doc:
                labels = [label["name"] for label in issue.get("labels", [])]
                entry["labels"] = labels
                pr_detail = subprocess.run(
                    ["gh", "api", f"/repos/{owner}/{repo}/pulls/{number}"],
                    capture_output=True,
                    text=True,
                )
                if pr_detail.returncode == 0:
                    pr_data = json.loads(pr_detail.stdout)
                    entry["sha"] = pr_data.get("merge_commit_sha", "")
                    entry["body"] = pr_data.get("body", "") or ""
                else:
                    entry["sha"] = ""
                    entry["body"] = ""
            changelog.append(entry)

        page += 1

    return changelog


def changelog_via_api(
    owner: str,
    repo: str,
    milestone: str,
    token: str | None,
    verbose: bool,
    doc: bool,
) -> list[dict]:
    """Use httpx to call the GitHub REST API directly."""
    headers = {
        "Accept": "application/vnd.github.v3+json",
        "User-Agent": "ATS-Changelog-Tool",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"

    with httpx.Client(base_url=API_URL, headers=headers, timeout=30) as client:
        milestone_id = _lookup_milestone(client, owner, repo, milestone)
        if milestone_id is None:
            print(f"Milestone not found: {milestone}", file=sys.stderr)
            sys.exit(1)

        print(f"Looking for issues from Milestone {milestone}", file=sys.stderr)

        changelog = []
        page = 1
        while True:
            print(f"Page {page}", file=sys.stderr)
            resp = client.get(
                f"/repos/{owner}/{repo}/issues",
                params={
                    "milestone": milestone_id,
                    "state": "closed",
                    "page": page,
                    "per_page": 100,
                },
            )
            _check_rate_limit(resp)
            resp.raise_for_status()
            issues = resp.json()

            if not issues:
                break

            for issue in issues:
                number = issue["number"]
                title = issue["title"]
                if verbose:
                    print(f"Issue #{number} - {title} ", end="", file=sys.stderr)

                if "pull_request" not in issue:
                    if verbose:
                        print("not a PR.", file=sys.stderr)
                    continue

                if not _is_merged(client, owner, repo, number):
                    if verbose:
                        print("not merged.", file=sys.stderr)
                    continue

                if verbose:
                    print("added.", file=sys.stderr)

                entry: dict = {"number": number, "title": title}
                if doc:
                    labels = [label["name"] for label in issue.get("labels", [])]
                    entry["labels"] = labels
                    pr_resp = client.get(f"/repos/{owner}/{repo}/pulls/{number}")
                    _check_rate_limit(pr_resp)
                    pr_resp.raise_for_status()
                    pr_data = pr_resp.json()
                    entry["sha"] = pr_data.get("merge_commit_sha", "")
                    entry["body"] = pr_data.get("body", "") or ""
                changelog.append(entry)

            page += 1

    return changelog


def changelog_via_git(git_range: str, verbose: bool, doc: bool) -> list[dict]:
    """Build the changelog from a git commit range.

    Used for security releases whose fixes are committed directly to a release
    branch without public PRs, so they never appear in a GitHub milestone. Each
    commit becomes an entry; a trailing "(#N)" in the subject is captured as the
    PR number when present, but is not required.
    """
    field_sep = "\x1f"
    record_sep = "\x1e"
    fmt = f"%H{field_sep}%s{field_sep}%b{record_sep}"
    result = subprocess.run(
        ["git", "log", "--no-merges", "--reverse", f"--pretty=format:{fmt}", git_range],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"git log error: {result.stderr.strip()}", file=sys.stderr)
        sys.exit(1)

    print(f"Reading commits from git range {git_range}", file=sys.stderr)

    changelog = []
    for record in result.stdout.split(record_sep):
        record = record.strip("\n")
        if not record:
            continue
        sha, subject, body = record.split(field_sep, 2)
        matches = re.findall(r"#(\d+)", subject)
        number = int(matches[-1]) if matches else None
        if verbose:
            label = f"#{number}" if number else sha[:12]
            print(f"{label} - {subject} added.", file=sys.stderr)

        entry: dict = {"number": number, "title": subject}
        if doc:
            entry["sha"] = sha
            entry["body"] = body.strip()
        changelog.append(entry)

    return changelog


def merge_changelogs(milestone_entries: list[dict], git_entries: list[dict]) -> list[dict]:
    """Union the milestone-sourced and git-sourced entries, deduplicated by PR number.

    Git commits keep their chronological order and carry commit-derived metadata.
    Where a git commit's PR number matches a milestone PR, the milestone labels are
    grafted onto the git entry. Milestone PRs with no corresponding commit in the
    git range (e.g. folded into a combined backport) are appended, sorted by number.
    """
    ms_by_number = {e["number"]: e for e in milestone_entries}
    git_numbers = {e["number"] for e in git_entries if e.get("number")}

    for ge in git_entries:
        num = ge.get("number")
        if num in ms_by_number and ms_by_number[num].get("labels") and "labels" not in ge:
            ge["labels"] = ms_by_number[num]["labels"]

    extras = [e for e in milestone_entries if e["number"] not in git_numbers]
    extras.sort(key=lambda x: x["number"])

    return git_entries + extras


def _lookup_milestone(client: httpx.Client, owner: str, repo: str, title: str) -> int | None:
    page = 1
    while True:
        resp = client.get(f"/repos/{owner}/{repo}/milestones", params={"state": "all", "per_page": 100, "page": page})
        _check_rate_limit(resp)
        resp.raise_for_status()
        data = resp.json()
        if not data:
            break
        for ms in data:
            if ms["title"] == title:
                return ms["number"]
        page += 1
    return None


def _is_merged(client: httpx.Client, owner: str, repo: str, pr_number: int) -> bool:
    resp = client.get(f"/repos/{owner}/{repo}/pulls/{pr_number}/merge")
    if resp.status_code == 204:
        return True
    if resp.status_code == 404:
        return False
    _check_rate_limit(resp)
    resp.raise_for_status()
    return False


def _check_rate_limit(resp: httpx.Response) -> None:
    if resp.status_code == 403:
        print(
            "You have exceeded your rate limit. Try using an auth token.",
            file=sys.stderr,
        )
        sys.exit(2)


def main():
    parser = argparse.ArgumentParser(description="Generate changelog from merged PRs in a GitHub milestone.")
    parser.add_argument("-o", "--owner", required=True, help="Repository owner")
    parser.add_argument("-r", "--repo", required=True, help="Repository name")
    parser.add_argument("-m", "--milestone", required=True, help="Milestone title")
    parser.add_argument("-a", "--auth", default=None, help="GitHub auth token (or set GH_TOKEN env var)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument(
        "--doc",
        action="store_true",
        help="Include extra metadata (merge SHA, full commit message, labels) for documentation",
    )
    parser.add_argument(
        "--format",
        choices=["text", "yaml"],
        default="text",
        help="Output format (default: text)",
    )
    parser.add_argument(
        "--use-gh",
        action="store_true",
        help="Use gh CLI instead of direct API calls (avoids rate limits)",
    )
    parser.add_argument(
        "--from-git",
        metavar="RANGE",
        default=None,
        help="Also source commits from a git range (e.g. 10.1.3..upstream/10.1.x) and merge them "
        "with the milestone PRs, deduplicated by PR number. Use for security releases whose fixes "
        "land directly on a branch without public PRs. -m is still used as the version label.",
    )
    args = parser.parse_args()

    token = args.auth or os.environ.get("GH_TOKEN")

    if not token and not args.use_gh:
        print(
            "WARNING: No GitHub token provided. Unauthenticated requests are limited\n"
            "to 60 per hour, which is usually not enough to generate a full changelog.\n"
            "\n"
            "Provide a token via -a or the GH_TOKEN environment variable.\n"
            "\n"
            "To create a token:\n"
            "  1. Go to https://github.com/settings/tokens\n"
            "  2. Click 'Generate new token' -> 'Generate new token (classic)'\n"
            "  3. Select the 'public_repo' scope (sufficient for public repositories)\n"
            "  4. For fine-grained tokens, grant read-only access to Issues and\n"
            "     Pull Requests on the target repository\n"
            "\n"
            "Alternatively, use --use-gh to use the gh CLI with its existing auth.\n",
            file=sys.stderr,
        )

    if args.use_gh:
        if not gh_cli_available():
            print("gh CLI not found. Install it or omit --use-gh.", file=sys.stderr)
            sys.exit(1)
        changelog = changelog_via_gh(args.owner, args.repo, args.milestone, args.verbose, args.doc)
    else:
        changelog = changelog_via_api(
            args.owner,
            args.repo,
            args.milestone,
            token,
            args.verbose,
            args.doc,
        )

    if args.from_git:
        git_changelog = changelog_via_git(args.from_git, args.verbose, args.doc)
        changelog = merge_changelogs(changelog, git_changelog)

    if changelog:
        # Milestone-only entries all carry a PR number and sort by it. A merged
        # (--from-git) result preserves git's chronological order with milestone-only
        # PRs appended, so it must not be re-sorted.
        if not args.from_git:
            changelog.sort(key=lambda x: x["number"])

        if args.format == "yaml":
            output = {
                "milestone": args.milestone,
                "owner": args.owner,
                "repo": args.repo,
                "entries": changelog,
            }
            if yaml is None:
                print("ERROR: --format yaml requires PyYAML. Install it with: pip install pyyaml", file=sys.stderr)
                sys.exit(1)
            yaml.dump(output, sys.stdout, default_flow_style=False, sort_keys=False, allow_unicode=True)
        else:
            print(f"Changes with Apache Traffic Server {args.milestone}")
            for entry in changelog:
                if entry.get("number"):
                    print(f"  #{entry['number']} - {entry['title']}")
                else:
                    print(f"  {entry['title']}")
                if args.doc:
                    if entry.get("sha"):
                        print(f"    SHA: {entry['sha']}")
                    if entry.get("labels"):
                        print(f"    Labels: {', '.join(entry['labels'])}")
                    if entry.get("body"):
                        body_lines = entry["body"].strip().split("\n")
                        print(f"    Body:")
                        for line in body_lines:
                            print(f"      {line}")


if __name__ == "__main__":
    main()
