"""Opt-in real Studio TCP upgrade through the production firmware parser.

No login, Agent run, configuration change or credential is required. This is
host-to-Studio evidence, never proof of M5 Wi-Fi or firmware operation.
"""
import argparse
import base64
import hashlib
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=8748)
    parser.add_argument("--rounds", type=int, default=3)
    options = parser.parse_args()
    if not 1 <= options.port <= 65535 or not 1 <= options.rounds <= 10:
        parser.error("port must be valid and rounds must be between 1 and 10")
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("C++11 compiler required")
    firmware = Path(__file__).resolve().parents[2]
    source = r'''
#include "WebSocketHandshake.h"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  studio_handshake::Parser parser(argv[1]);
  int value;
  while((value=std::getchar())!=EOF)parser.feed(static_cast<uint8_t>(value),0);
  return parser.state()==studio_handshake::State::Complete && parser.error()==studio_handshake::Error::None ? 0 : 1;
}
'''
    with tempfile.TemporaryDirectory(prefix="cardputer-real-upgrade-") as directory:
        folder = Path(directory)
        (folder / "check.cpp").write_text(source)
        subprocess.run([
            compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
            "-I", str(firmware / "include"), str(folder / "check.cpp"),
            "-o", str(folder / "check"),
        ], check=True)
        for _ in range(options.rounds):
            key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
            accept = base64.b64encode(hashlib.sha1(
                (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
            ).digest()).decode("ascii")
            request = (
                "GET /socket.io/?EIO=4&transport=websocket&profile=default HTTP/1.1\r\n"
                f"Host: {options.host}:{options.port}\r\nConnection: Upgrade\r\n"
                "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
                f"Sec-WebSocket-Key: {key}\r\n\r\n"
            ).encode("ascii")
            with socket.create_connection((options.host, options.port), timeout=4) as connection:
                connection.settimeout(4)
                connection.sendall(request)
                header = bytearray()
                while not header.endswith(b"\r\n\r\n"):
                    byte = connection.recv(1)
                    if not byte or len(header) >= 8192:
                        raise RuntimeError("Incomplete or oversized Studio upgrade")
                    header.extend(byte)
                subprocess.run([str(folder / "check"), accept], input=bytes(header), check=True)
    print(f"Real Studio upgrades accepted by production parser: {options.rounds}/{options.rounds}; no login or Agent run")


if __name__ == "__main__":
    main()
