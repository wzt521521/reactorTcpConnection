#!/usr/bin/env python3

import concurrent.futures
import socket
import struct
import subprocess
import sys
import time


HOST = "127.0.0.1"
PORT = 8080
HEADER_LENGTH = 6
CMD_ECHO = 1
CMD_LOGIN = 2
CMD_CHAT = 3
CMD_ERROR = 0xFFFF


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def encode_frame(command, body):
    if isinstance(body, str):
        body = body.encode("utf-8")
    return struct.pack("!IH", HEADER_LENGTH + len(body), command) + body


def recv_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("peer closed before a complete frame arrived")
        data.extend(chunk)
    return bytes(data)


def recv_frame(sock):
    header = recv_exact(sock, HEADER_LENGTH)
    total, command = struct.unpack("!IH", header)
    require(total >= HEADER_LENGTH, "response total length is invalid")
    body = recv_exact(sock, total - HEADER_LENGTH)
    return command, body


def connect_client():
    sock = socket.create_connection((HOST, PORT), timeout=2)
    sock.settimeout(2)
    return sock


def wait_until_ready(process):
    deadline = time.monotonic() + 5
    last_error = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate(timeout=1)
            raise RuntimeError(
                f"server exited during startup ({process.returncode})\n"
                f"stdout:\n{stdout}\nstderr:\n{stderr}"
            )
        try:
            with connect_client():
                return
        except OSError as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"server did not listen on {HOST}:{PORT}: {last_error}")


def test_login_echo_and_chat():
    with connect_client() as sock:
        sock.sendall(encode_frame(CMD_LOGIN, "codex"))
        command, body = recv_frame(sock)
        require(command == CMD_LOGIN, "login response command mismatch")
        require(body.decode("utf-8") == "welcome codex", "login response mismatch")

        sock.sendall(encode_frame(CMD_ECHO, "round0"))
        command, body = recv_frame(sock)
        require(command == CMD_ECHO and body == b"round0", "echo response mismatch")

        sock.sendall(encode_frame(CMD_CHAT, "hello"))
        command, body = recv_frame(sock)
        require(command == CMD_CHAT, "chat response command mismatch")
        require(body.decode("utf-8") == "codex 说: hello", "chat response mismatch")


def test_chat_requires_login():
    with connect_client() as sock:
        sock.sendall(encode_frame(CMD_CHAT, "hello"))
        command, body = recv_frame(sock)
        require(command == CMD_ERROR, "unauthenticated chat should return CMD_ERROR")
        require(body == b"not logged in", "unauthenticated chat error mismatch")


def test_fragmented_and_sticky_frames():
    with connect_client() as sock:
        fragmented = encode_frame(CMD_ECHO, "fragmented")
        sock.sendall(fragmented[:3])
        time.sleep(0.02)
        sock.sendall(fragmented[3:])
        require(recv_frame(sock) == (CMD_ECHO, b"fragmented"),
                "fragmented frame response mismatch")

        sock.sendall(
            encode_frame(CMD_ECHO, "sticky-one")
            + encode_frame(CMD_ECHO, "sticky-two")
        )
        require(recv_frame(sock) == (CMD_ECHO, b"sticky-one"),
                "first sticky frame response mismatch")
        require(recv_frame(sock) == (CMD_ECHO, b"sticky-two"),
                "second sticky frame response mismatch")


def test_invalid_frame_is_isolated():
    with connect_client() as sock:
        sock.sendall(struct.pack("!IH", 5, CMD_ECHO))
        require(sock.recv(1) == b"", "invalid frame connection should be closed")

    with connect_client() as healthy:
        healthy.sendall(encode_frame(CMD_ECHO, "still-alive"))
        require(recv_frame(healthy) == (CMD_ECHO, b"still-alive"),
                "server should survive another client's invalid frame")


def concurrent_echo_worker(worker_id):
    with connect_client() as sock:
        for sequence in range(10):
            payload = f"worker-{worker_id}-{sequence}".encode("ascii")
            sock.sendall(encode_frame(CMD_ECHO, payload))
            command, body = recv_frame(sock)
            require(command == CMD_ECHO and body == payload,
                    f"concurrent echo mismatch for worker {worker_id}")


def test_concurrent_clients():
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        futures = [executor.submit(concurrent_echo_worker, worker) for worker in range(16)]
        for future in futures:
            future.result(timeout=5)


def terminate_process(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/reactor_server", file=sys.stderr)
        return 2

    process = subprocess.Popen(
        [sys.argv[1]],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    try:
        wait_until_ready(process)
        test_login_echo_and_chat()
        test_chat_requires_login()
        test_fragmented_and_sticky_frames()
        test_invalid_frame_is_isolated()
        test_concurrent_clients()
        print("reactor_protocol_integration passed")
        return 0
    finally:
        terminate_process(process)


if __name__ == "__main__":
    raise SystemExit(main())
