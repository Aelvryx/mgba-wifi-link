#!/usr/bin/env python3
"""State-file-backed fake ``gh`` executable used only by publisher tests."""

import base64
import hashlib
import json
import os
from pathlib import Path
import sys
from urllib.parse import parse_qs, urlsplit


STATE_PATH = Path(os.environ["GBA_WIFI_LINK_FAKE_GH_STATE"])


def _state() -> dict[str, object]:
    return json.loads(STATE_PATH.read_text(encoding="utf-8"))


def _save(state: dict[str, object]) -> None:
    STATE_PATH.write_text(json.dumps(state, sort_keys=True), encoding="utf-8")


def _option(arguments: list[str], name: str) -> str:
    return arguments[arguments.index(name) + 1]


def _release_json(release: dict[str, object]) -> str:
    return json.dumps(release, sort_keys=True, separators=(",", ":"))


def _fail_not_found() -> None:
    print("HTTP 404: Not Found", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    arguments = sys.argv[1:]
    state = _state()
    calls = state.setdefault("calls", [])
    assert isinstance(calls, list)
    calls.append(arguments)
    if state.get("malformed_json"):
        _save(state)
        sys.stdout.write('{"id":1,"id":2}')
        return

    release = state.get("release")
    if not arguments or arguments[0] != "api":
        if arguments[:2] == ["attestation", "create"]:
            _save(state)
            return
        raise SystemExit(64)

    endpoint = ""
    options_with_values = {
        "--field", "--header", "--hostname", "--input", "--method", "--output",
        "--raw-field", "--repo",
    }
    skip_next = False
    for argument in arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        if argument in options_with_values:
            skip_next = True
            continue
        if not argument.startswith("-"):
            endpoint = argument
            break
    method = _option(arguments, "--method") if "--method" in arguments else "GET"
    if endpoint.startswith("repos/") and "/releases/tags/" in endpoint:
        if release is None:
            _save(state)
            _fail_not_found()
        _save(state)
        sys.stdout.write(_release_json(release))
        return

    if endpoint.endswith("/releases") and method == "POST":
        payload = json.loads(Path(_option(arguments, "--input")).read_text(encoding="utf-8"))
        release = {
            "assets": [],
            "body": payload["body"],
            "draft": payload["draft"],
            "id": 1,
            "prerelease": payload["prerelease"],
            "tag_name": payload["tag_name"],
            "target_commitish": payload["target_commitish"],
        }
        state["release"] = release
        state["files"] = {}
        _save(state)
        sys.stdout.write(_release_json(release))
        return

    if "/releases/" in endpoint and endpoint.endswith("/assets") and method == "GET":
        if release is None:
            _save(state)
            _fail_not_found()
        _save(state)
        sys.stdout.write(_release_json(release["assets"]))
        return

    if urlsplit(endpoint).path.endswith("/assets") and method == "POST":
        if release is None:
            _save(state)
            _fail_not_found()
        name = parse_qs(urlsplit(endpoint).query)["name"][0]
        data = Path(_option(arguments, "--input")).read_bytes()
        assets = release["assets"]
        assert isinstance(assets, list)
        asset = {
            "digest": "sha256:" + hashlib.sha256(data).hexdigest(),
            "id": len(assets) + 10,
            "name": name,
            "size": len(data),
        }
        assets.append(asset)
        files = state["files"]
        assert isinstance(files, dict)
        files[name] = base64.b64encode(data).decode("ascii")
        _save(state)
        sys.stdout.write(_release_json(asset))
        return

    if "/releases/assets/" in endpoint and method == "GET":
        if release is None:
            _save(state)
            _fail_not_found()
        asset_id = int(endpoint.rsplit("/", 1)[1])
        asset = next(asset for asset in release["assets"] if asset["id"] == asset_id)
        files = state["files"]
        assert isinstance(files, dict)
        data = base64.b64decode(files[asset["name"]])
        Path(_option(arguments, "--output")).write_bytes(data)
        _save(state)
        return

    if "/releases/" in endpoint and method == "PATCH":
        if release is None:
            _save(state)
            _fail_not_found()
        if _option(arguments, "--field") == "draft=false":
            release["draft"] = False
        _save(state)
        sys.stdout.write(_release_json(release))
        return

    if "/releases/" in endpoint and method == "DELETE":
        state["release"] = None
        _save(state)
        return

    raise SystemExit(64)


if __name__ == "__main__":
    main()
