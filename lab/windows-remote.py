#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import pathlib
import sys
import time

import winrm
from winrm.exceptions import WinRMError


DEFAULT_USER = "lab"
DEFAULT_PASSWORD = "PointerLab1!"
# pywinrm uses PowerShell's encoded-command form, whose Windows command-line
# limit is reached well before WinRM's envelope limit for larger chunks.
UPLOAD_CHUNK_BYTES = 1024


def powershell_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def connect(host: str, user: str, password: str) -> winrm.Session:
    return winrm.Session(
        f"http://{host}:5985/wsman",
        auth=(user, password),
        transport="ntlm",
    )


def run(session: winrm.Session, script: str, quiet: bool = False) -> bytes:
    result = session.run_ps(script)
    if result.status_code != 0:
        sys.stderr.buffer.write(result.std_out)
        sys.stderr.buffer.write(result.std_err)
        raise RuntimeError(
            f"PowerShell exited with status {result.status_code}"
        )
    if not quiet:
        sys.stdout.buffer.write(result.std_out)
        if result.std_err and not result.std_err.startswith(b"#< CLIXML"):
            sys.stderr.buffer.write(result.std_err)
    return result.std_out


def put_file(
    session: winrm.Session,
    local: pathlib.Path,
    remote_path: str,
) -> None:
    payload = local.read_bytes()
    remote = powershell_quote(remote_path)
    chunks = [
        payload[offset : offset + UPLOAD_CHUNK_BYTES]
        for offset in range(0, len(payload), UPLOAD_CHUNK_BYTES)
    ] or [b""]

    for index, chunk in enumerate(chunks):
        encoded = base64.b64encode(chunk).decode("ascii")
        mode = "Create" if index == 0 else "Append"
        run(
            session,
            f"$data=[Convert]::FromBase64String('{encoded}');"
            f"$stream=[IO.File]::Open({remote},"
            f"[IO.FileMode]::{mode},[IO.FileAccess]::Write,"
            "[IO.FileShare]::None);"
            "try{$stream.Write($data,0,$data.Length)}"
            "finally{$stream.Dispose()}",
            quiet=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--user", default=DEFAULT_USER)
    parser.add_argument("--password", default=DEFAULT_PASSWORD)
    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("script")
    run_parser.add_argument("--quiet", action="store_true")

    get_parser = subparsers.add_parser("get")
    get_parser.add_argument("remote")
    get_parser.add_argument("local", type=pathlib.Path)

    put_parser = subparsers.add_parser("put")
    put_parser.add_argument("local", type=pathlib.Path)
    put_parser.add_argument("remote")

    wait_parser = subparsers.add_parser("wait-file")
    wait_parser.add_argument("remote")
    wait_parser.add_argument("--timeout", type=float, default=120.0)

    subparsers.add_parser("ping")
    arguments = parser.parse_args()
    session = connect(arguments.host, arguments.user, arguments.password)

    try:
        if arguments.command == "run":
            run(session, arguments.script, arguments.quiet)
        elif arguments.command == "get":
            remote = powershell_quote(arguments.remote)
            encoded = run(
                session,
                "[Convert]::ToBase64String("
                f"[IO.File]::ReadAllBytes({remote}))",
                quiet=True,
            )
            arguments.local.parent.mkdir(parents=True, exist_ok=True)
            arguments.local.write_bytes(base64.b64decode(encoded.strip()))
        elif arguments.command == "put":
            put_file(session, arguments.local, arguments.remote)
        elif arguments.command == "wait-file":
            remote = powershell_quote(arguments.remote)
            deadline = time.monotonic() + arguments.timeout
            while True:
                result = session.run_ps(
                    f"if (Test-Path {remote}) {{ 'ready' }} else {{ exit 3 }}"
                )
                if result.status_code == 0:
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError(
                        f"timed out waiting for {arguments.remote}"
                    )
                time.sleep(0.25)
        elif arguments.command == "ping":
            run(session, "'ready'", quiet=True)
    except (
        OSError,
        RuntimeError,
        TimeoutError,
        ValueError,
        WinRMError,
    ) as error:
        print(f"winrm: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
