#!/usr/bin/env python3
"""Development probe for the direct-cloud protocol; never a runtime bridge.

Read the owner's existing Studio session in memory. Never print credentials,
raw cloud responses, MQTT payloads, or camera URLs. Tickets are mode 0600.
Requires requests and cryptography (available in the project's PIO venv).
"""
import argparse
import base64
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import time
import uuid

import requests
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def studio_session(root):
    raw = (root / "BambuNetworkEngine.conf").read_bytes()
    if len(raw) > 1024 * 1024 or len(raw) % 16:
        raise ValueError("invalid Studio session file")
    cipher = Cipher(algorithms.AES(b"i4crL3LESLnWapLS"), modes.ECB()).decryptor()
    plain = cipher.update(raw) + cipher.finalize()
    session, _ = json.JSONDecoder().raw_decode(plain.decode("utf-8").lstrip())
    config = json.loads((root / "BambuStudio.conf").read_text())
    serial = config.get("app", {}).get("user_last_selected_machine") or session.get("last_monitor_machine")
    if not serial or not session.get("user", {}).get("token"):
        raise ValueError("no saved cloud session or selected printer")
    return session["user"], config, serial


def private_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(value, stream)


def pop_headers(cert_dir):
    cert = x509.load_pem_x509_certificate((cert_dir / "slicer_cert.pem").read_bytes())
    key = serialization.load_pem_private_key((cert_dir / "slicer_key.pem").read_bytes(), None)
    numbers = key.private_numbers()
    if numbers.public_numbers != cert.public_key().public_numbers():
        raise ValueError("application key does not match certificate")
    size = (numbers.public_numbers.n.bit_length() + 7) // 8
    timestamp = str(int(time.time() * 1000)).encode("ascii")
    # Raw PKCS#1 v1.5 over the millisecond timestamp (no hash/DigestInfo).
    encoded = b"\0\1" + b"\xff" * (size - len(timestamp) - 3) + b"\0" + timestamp
    signature = pow(int.from_bytes(encoded, "big"), numbers.d, numbers.public_numbers.n).to_bytes(size, "big")
    serial = format(cert.serial_number, "x")
    if len(serial) % 2:
        serial = "0" + serial
    return {
        "x-bbl-app-certification-id": cert.issuer.rfc4514_string() + ":" + serial,
        "x-bbl-device-security-sign": base64.b64encode(signature).decode("ascii"),
    }


def mqtt_string(text):
    data = text.encode()
    if len(data) > 65535:
        raise ValueError("MQTT string too large")
    return struct.pack(">H", len(data)) + data


def mqtt_packet(kind, body):
    remaining = len(body)
    encoded = bytearray()
    while True:
        digit = remaining % 128
        remaining //= 128
        encoded.append(digit | (128 if remaining else 0))
        if not remaining:
            break
    return bytes([kind]) + encoded + body


def receive_exact(sock, size):
    out = bytearray()
    while len(out) < size:
        data = sock.recv(size - len(out))
        if not data:
            raise OSError("MQTT closed")
        out.extend(data)
    return out


def inspect_device(user, serial, region):
    broker = "cn.mqtt.bambulab.com" if region == "cn" else "us.mqtt.bambulab.com"
    username = str(user["user_id"])
    if not username.startswith("u_"):
        username = "u_" + username
    context = ssl.create_default_context(cafile=requests.certs.where())
    result = {}
    with socket.create_connection((broker, 8883), timeout=15) as raw:
        with context.wrap_socket(raw, server_hostname=broker) as sock:
            sock.settimeout(10)
            body = mqtt_string("MQTT") + bytes([4, 0xC2]) + struct.pack(">H", 30)
            body += mqtt_string("p4-camera-probe-" + uuid.uuid4().hex[:8]) + mqtt_string(username) + mqtt_string(user["token"])
            sock.sendall(mqtt_packet(0x10, body))
            if receive_exact(sock, 4) != b"\x20\x02\x00\x00":
                raise ValueError("MQTT authentication failed")
            print(json.dumps({"mqtt_connected": True}), flush=True)
            sock.sendall(mqtt_packet(0x82, b"\0\1" + mqtt_string("device/" + serial + "/report") + b"\0"))
            for payload in ({"pushing": {"command": "pushall", "sequence_id": "736001"}}, {"info": {"command": "get_version", "sequence_id": "736002"}}):
                sock.sendall(mqtt_packet(0x30, mqtt_string("device/" + serial + "/request") + json.dumps(payload).encode()))
            until = time.monotonic() + 10
            while time.monotonic() < until:
                try:
                    kind = receive_exact(sock, 1)[0]
                    length, scale = 0, 1
                    for _ in range(4):
                        b = receive_exact(sock, 1)[0]
                        length += (b & 127) * scale
                        if not b & 128:
                            break
                        scale *= 128
                    else:
                        raise ValueError("invalid MQTT frame")
                    if length > 256 * 1024:
                        raise ValueError("MQTT frame too large")
                    data = receive_exact(sock, length)
                except socket.timeout:
                    break
                if kind >> 4 != 3:
                    continue
                topic_length = int.from_bytes(data[:2], "big")
                start = topic_length + 2
                if ((kind >> 1) & 3) == 1:
                    sock.sendall(b"\x40\x02" + data[start:start + 2])
                    start += 2
                try:
                    payload = json.loads(data[start:])
                except (ValueError, UnicodeError):
                    continue
                for key in ("cfg", "fun"):
                    if key in payload.get("print", {}):
                        result[key] = payload["print"][key]
                status = payload.get("print", {})
                result["print_fields"] = sorted(set(result.get("print_fields", [])) | set(status))
                for key in ("subtask_name", "gcode_file", "task_name", "project_name", "plate_name",
                            "task_id", "subtask_id", "project_id", "profile_id", "plate_idx"):
                    if key in status:
                        result.setdefault("job", {})[key] = status[key]
                camera = payload.get("print", {}).get("ipcam", {})
                if camera:
                    result["ipcam"] = {k: camera[k] for k in ("tutk_server", "liveview_remote", "liveview_local", "resolution", "ipcam_dev") if k in camera}
                for module in payload.get("info", {}).get("module", []):
                    if module.get("name") == "ota":
                        result["firmware"] = module.get("sw_ver", "")
            sock.sendall(b"\xe0\0")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--studio-dir", type=Path, default=Path.home() / "Library/Application Support/BambuStudio")
    parser.add_argument("--cert-dir", type=Path, default=Path(".cache/p4-camera-private"))
    parser.add_argument("--region", choices=("cn", "us"), default="cn")
    parser.add_argument("--firmware", default="")
    parser.add_argument("--inspect-device", action="store_true")
    parser.add_argument("--mint", action="store_true")
    parser.add_argument("--protocols", default="tutk,agora")
    parser.add_argument("--output", type=Path, default=Path(".cache/p4-camera-private/ticket.json"))
    args = parser.parse_args()
    user, config, serial = studio_session(args.studio_dir)
    if args.inspect_device:
        status = inspect_device(user, serial, args.region)
        private_json(args.cert_dir / "device-capabilities.json", status)
        print(json.dumps({"firmware": status.get("firmware"), "camera": status.get("ipcam"), "has_cfg": bool(status.get("cfg"))}), flush=True)
        if not args.firmware:
            args.firmware = status.get("firmware", "")
    if not args.mint:
        return 0
    headers = {
        "Authorization": "Bearer " + user["token"],
        "User-Agent": "bambu_network_agent/02.08.02.60",
        "X-BBL-Client-Name": "BambuStudio", "X-BBL-Client-Type": "slicer",
        "X-BBL-Client-Version": "02.08.02.61", "X-BBL-OS-Type": "macos",
        "X-BBL-Agent-OS-Type": "macos", "X-BBL-Agent-Version": "02.08.02.60",
        "X-BBL-Device-ID": config.get("app", {}).get("slicer_uuid", ""),
    }
    headers.update(pop_headers(args.cert_dir))
    info = args.cert_dir / "executable-info.json"
    if info.exists():
        headers["X-BBL-Executable-info"] = json.dumps(json.loads(info.read_text()), separators=(",", ":"))
    host = "https://api.bambulab.cn" if args.region == "cn" else "https://api.bambulab.com"
    response = requests.post(host + "/v1/iot-service/api/user/ttcode", headers=headers,
                             json={"dev_id": serial, "dev_version": args.firmware, "protocols": args.protocols.split(",")}, timeout=25)
    try:
        body = response.json()
    except ValueError:
        body = {}
    ticket = body.get("data", body)
    good = response.status_code == 200 and isinstance(ticket, dict) and bool(ticket.get("ttcode"))
    print(json.dumps({"ticket_http": response.status_code, "code": body.get("code"), "transport": ticket.get("type") if isinstance(ticket, dict) else None, "ticket_ready": good, "fields": list(ticket) if isinstance(ticket, dict) else []}), flush=True)
    if response.status_code == 200:
        private_json(args.cert_dir / "session-response.json", body)
    if good:
        private_json(args.output, ticket)
        return 0
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, requests.RequestException) as error:
        # Exception messages can include URLs, account data or source JSON.
        print(json.dumps({"probe_error": type(error).__name__}), flush=True)
        raise SystemExit(2)
