#!/usr/bin/env python3
"""Exercise a WebSocket text frame larger than one TLS record."""

from __future__ import annotations

import argparse
import base64
import hashlib
import socket
import ssl
import subprocess
import sys
from pathlib import Path


EXPECTED_SIZE = 32 * 1024
WEBSOCKET_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def receive_exact(stream: ssl.SSLSocket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = stream.recv(remaining)
        if not chunk:
            raise RuntimeError(
                f"connection ended with {remaining} WebSocket bytes missing"
            )
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def receive_headers(stream: ssl.SSLSocket) -> bytes:
    data = bytearray()
    while b"\r\n\r\n" not in data:
        data.extend(stream.recv(4096))
        if len(data) > 32 * 1024:
            raise RuntimeError("WebSocket upgrade headers are too large")
    return bytes(data)


def websocket_key(headers: bytes) -> bytes:
    for line in headers.split(b"\r\n"):
        name, separator, value = line.partition(b":")
        if separator and name.lower() == b"sec-websocket-key":
            return value.strip()
    raise RuntimeError("WebSocket upgrade omitted Sec-WebSocket-Key")


def accept_websocket(stream: ssl.SSLSocket) -> None:
    key = websocket_key(receive_headers(stream))
    accept = base64.b64encode(hashlib.sha1(key + WEBSOCKET_GUID).digest())
    stream.sendall(
        b"HTTP/1.1 101 Switching Protocols\r\n"
        b"Upgrade: websocket\r\n"
        b"Connection: Upgrade\r\n"
        b"Sec-WebSocket-Accept: "
        + accept
        + b"\r\n\r\n"
    )
    # Production sends its welcome message immediately after the upgrade.
    # Keep the test's first application message a control frame so only the
    # explicit acknowledgement reaches the client's onMessage callback.
    stream.sendall(b"\x89\x00")


def receive_frame(stream: ssl.SSLSocket) -> tuple[int, bytes]:
    first, second = receive_exact(stream, 2)
    if not second & 0x80:
        raise RuntimeError("client WebSocket frame is not masked")

    length = second & 0x7F
    if length == 126:
        length = int.from_bytes(receive_exact(stream, 2), "big")
    elif length == 127:
        length = int.from_bytes(receive_exact(stream, 8), "big")

    mask = receive_exact(stream, 4)
    encoded = receive_exact(stream, length)
    decoded = bytes(value ^ mask[index % 4] for index, value in enumerate(encoded))
    return first & 0x0F, decoded


def receive_text_frame(stream: ssl.SSLSocket) -> str:
    # The client answers the post-upgrade ping concurrently with its onOpen
    # send, so the pong may arrive before or after the text frame.
    while True:
        opcode, payload = receive_frame(stream)
        if opcode == 0x1:
            return payload.decode("utf-8")
        if opcode != 0xA:
            raise RuntimeError(f"expected a text frame or a pong, got opcode {opcode:#x}")


def run(client: Path, cert: Path, key: Path) -> None:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    # Python/OpenSSL emits TLS 1.3 post-handshake tickets immediately. The
    # libdatachannel 0.24 receive loop may defer application data behind those
    # tickets, which is unrelated to the partial-write regression under test.
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)

    with socket.create_server(("127.0.0.1", 0)) as listener:
        listener.settimeout(10)
        port = listener.getsockname()[1]
        process = subprocess.Popen(
            [str(client), f"wss://127.0.0.1:{port}/regression"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            connection, _ = listener.accept()
            with connection, context.wrap_socket(connection, server_side=True) as stream:
                stream.settimeout(10)
                accept_websocket(stream)
                payload = receive_text_frame(stream)
                prefix = '{"type":"probe","padding":"'
                suffix = '"}'
                padding_size = EXPECTED_SIZE - len(prefix) - len(suffix)
                expected = prefix + "a" * padding_size + suffix
                if payload != expected:
                    raise RuntimeError("large WebSocket text payload was corrupted")
                stream.sendall(b"\x81\x02ok")
                # Closing with the client's pong (or close frame) still
                # unread turns the close into a TCP reset, which can destroy
                # the acknowledgement before delivery. Drain until the client
                # closes: it only does so after receiving the acknowledgement.
                try:
                    while receive_frame(stream)[0] != 0x8:
                        pass
                except (RuntimeError, ssl.SSLError, OSError):
                    pass

            stdout, stderr = process.communicate(timeout=10)
            if process.returncode:
                raise RuntimeError(
                    f"WSS client failed with {process.returncode}\n{stdout}{stderr}"
                )
        except Exception:
            process.terminate()
            try:
                stdout, stderr = process.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                stdout, stderr = process.communicate()
            if stdout:
                print(stdout, file=sys.stderr, end="")
            if stderr:
                print(stderr, file=sys.stderr, end="")
            raise


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True, type=Path)
    parser.add_argument("--cert", required=True, type=Path)
    parser.add_argument("--key", required=True, type=Path)
    args = parser.parse_args()
    run(args.client.resolve(), args.cert.resolve(), args.key.resolve())


if __name__ == "__main__":
    main()
