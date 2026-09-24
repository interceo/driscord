#!/usr/bin/env python3
"""Publish a server or client release of the commit a Release workflow runs on.

Called by .forgejo/workflows/release.yml. The release-builder in the cluster
polls Forgejo releases: `server-vX.Y.Z` builds the api/signaling images that
Flux rolls out, `client-vX.Y.Z` builds the Linux/Windows/macOS bundles of the
update channel; a version with a prerelease suffix goes to the beta channel.
This script only decides whether the release may exist and creates it.

Environment: COMPONENT (server|client), VERSION (X.Y.Z or X.Y.Z-pre),
DRY_RUN (true|false), FORGEJO_TOKEN (the job's own token) and the Actions
defaults GITHUB_SERVER_URL, GITHUB_REPOSITORY, GITHUB_SHA.
"""
from __future__ import annotations

import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

COMPONENTS = ("server", "client")
# No hyphen inside the prerelease: cmake/Version.cmake tells `-N-g<sha>` of
# git describe apart from the suffix by it.
VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.]+))?$")
# Tags cut before the server/client split released both at once. Flux orders
# them together with `server-v*` images and old clients compare against them,
# so each stream must start above the newest of them.
LEGACY_PREFIX = "v"
# This workflow's own commit statuses, if Forgejo posts any, are not CI.
OWN_CONTEXT_PREFIX = "Release /"


def fail(message: str) -> None:
    print(f"::error::{message}")
    sys.exit(1)


def semver_key(version: str) -> tuple:
    """Order of semver 2.0: a prerelease sorts before its release, numeric identifiers by value."""
    match = VERSION_RE.fullmatch(version)
    assert match is not None, version
    core = tuple(int(part) for part in match.groups()[:3])
    pre = match.group(4)
    if pre is None:
        return (core, 1, ())
    return (core, 0, tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split(".")))


class Forgejo:
    def __init__(self, server_url: str, repository: str, token: str) -> None:
        self.base = f"{server_url.rstrip('/')}/api/v1/repos/{repository}"
        self.token = token

    def request(self, method: str, path: str, body: dict[str, Any] | None = None) -> tuple[int, Any]:
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(f"{self.base}{path}", data=data, method=method)
        request.add_header("Authorization", f"token {self.token}")
        request.add_header("Accept", "application/json")
        if data is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return response.status, json.loads(response.read() or b"null")
        except urllib.error.HTTPError as error:
            detail = error.read().decode(errors="replace")[:500]
            return error.code, detail

    def get(self, path: str) -> Any:
        status, data = self.request("GET", path)
        if status != 200:
            fail(f"GET {path} returned {status}: {data}")
        return data

    def release_tags(self) -> list[str]:
        tags: list[str] = []
        page = 1
        while True:
            batch = self.get(f"/releases?limit=50&page={page}")
            if not batch:
                return tags
            tags += [item["tag_name"] for item in batch if isinstance(item.get("tag_name"), str)]
            page += 1


def main() -> None:
    component = os.environ.get("COMPONENT", "")
    version = os.environ.get("VERSION", "").strip()
    dry_run = os.environ.get("DRY_RUN", "false") == "true"
    commit = os.environ["GITHUB_SHA"]
    token = os.environ.get("FORGEJO_TOKEN", "")
    if component not in COMPONENTS:
        fail(f"component must be one of {', '.join(COMPONENTS)}, got {component!r}")
    if not VERSION_RE.fullmatch(version):
        fail(f"version must be X.Y.Z or X.Y.Z-pre (e.g. 1.4.0, 1.4.0-rc.1), got {version!r}")
    if not token:
        fail("FORGEJO_TOKEN is empty")
    forgejo = Forgejo(os.environ["GITHUB_SERVER_URL"], os.environ["GITHUB_REPOSITORY"], token)
    tag = f"{component}-v{version}"
    prerelease = "-" in version
    print(f"{tag} from {commit} ({'beta' if prerelease else 'stable'} channel)")

    status, _ = forgejo.request("GET", f"/tags/{urllib.parse.quote(tag, safe='')}")
    if status != 404:
        fail(f"tag {tag} already exists" if status == 200 else f"cannot check tag {tag}: HTTP {status}")

    prefix = f"{component}-v"
    previous = []
    for existing in forgejo.release_tags():
        for stream_prefix in (prefix, LEGACY_PREFIX):
            if existing.startswith(stream_prefix) and VERSION_RE.fullmatch(existing[len(stream_prefix):]):
                previous.append(existing[len(stream_prefix):])
    if previous:
        newest = max(previous, key=semver_key)
        if semver_key(version) <= semver_key(newest):
            fail(f"{version} is not above {newest}, the newest {component} (or pre-split) release")
        print(f"newest earlier release: {newest}")

    combined = forgejo.get(f"/commits/{commit}/status")
    statuses = [s for s in combined.get("statuses") or [] if not str(s.get("context", "")).startswith(OWN_CONTEXT_PREFIX)]
    if not statuses:
        fail(f"commit {commit[:12]} has no CI statuses; push it to main and let CI finish first")
    for item in statuses:
        print(f"  {item.get('status', '?'):8} {item.get('context', '?')}")
    unfinished = [s for s in statuses if s.get("status") != "success"]
    if unfinished:
        fail("CI is not green on this commit: " + ", ".join(f"{s.get('context')} = {s.get('status')}" for s in unfinished))

    if dry_run:
        print(f"dry run: would create release {tag} at {commit}")
        return
    body = (f"{component.capitalize()} release {version} of {commit}.\n\n"
            f"Built and published by the release-builder "
            f"({'images → Flux' if component == 'server' else 'bundles → update channel'}).")
    status, data = forgejo.request("POST", "/releases", {
        "tag_name": tag,
        "target_commitish": commit,
        "name": tag,
        "body": body,
        "draft": False,
        "prerelease": prerelease,
    })
    if status != 201:
        fail(f"creating release {tag} returned {status}: {data}")
    print(f"created {data.get('html_url', tag)}")


if __name__ == "__main__":
    main()
