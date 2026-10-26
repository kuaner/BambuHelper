# Direct cloud chamber snapshot probe

This development tool exercises the same bounded TUTK/DTLS/file protocol as
the experimental ESP32-P4 firmware. It does not decode video or forward a
desktop camera stream. Runtime inputs are private, short-lived cloud tickets.
The transport is AGPL-3.0; see `lib/BambuTUTK/NOTICE.md`.

## Host validation

Build the normal P4 environment once for ArduinoJson, then build this tool
with CMake, a C++17 compiler and OpenSSL 3:

```sh
cmake -S tools/p4_cloud_camera -B .cache/p4-cloud-camera-build \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
cmake --build .cache/p4-cloud-camera-build -j 4
ctest --test-dir .cache/p4-cloud-camera-build --output-on-failure
.cache/p4-cloud-camera-build/camera_probe PRIVATE_TICKET.json OUTPUT.jpg
```

`cloud_probe.py --mint --firmware 01.02.00.00 --protocols tutk` can request a
fresh ticket using the current user's locally stored Bambu Studio account.
It requires Python `requests` and `cryptography`, plus an application key,
matching certificate chain and genuine executable signing metadata supplied
privately in `.cache/p4-camera-private/` (or `--cert-dir`). It never
prints the account token, application key or returned ticket values. Optional
`--inspect-device` uses read-only MQTT status/version requests.

The Mbed TLS host option verifies the actual P4 crypto backend:

```sh
cmake -S tools/p4_cloud_camera -B .cache/p4-cloud-camera-mbedtls-build \
  -DBAMBU_CAMERA_MBEDTLS=ON -DMBEDTLS_SOURCE=/absolute/path/to/mbedtls-3.6.6 \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
cmake --build .cache/p4-cloud-camera-mbedtls-build -j 4
ctest --test-dir .cache/p4-cloud-camera-mbedtls-build --output-on-failure
```

Initialize Mbed TLS's `framework` git submodule before configuring. OpenSSL
remains a test dependency for independent MD5 fixture generation.

For a Mac whose TUN VPN supplies fake DNS addresses, process-local development
overrides are available: `BAMBU_CAMERA_IF=en0` and
`BAMBU_TUTK_MASTER_IPS=IP1,IP2,...`. Resolve actual rendezvous hosts for the
current region first. These overrides do not change system routes or compile
into ESP firmware. `BAMBU_TUTK_FORCE_RELAY=1` is a diagnostic option; forced
relay file transfer is not yet validated. Debug logging is opt-in and redacts
UID, password and authkey; wire dumps are always disabled.

## Experimental P4 service

Build/USB flash `ws_p4_lcd_7b_rev1_camera` only for pre-v3 silicon. Normal P4
environments keep the experimental service disabled. Use `/camera/status` to
inspect stages, HTTP status, image generation, allocation minima and stack
headroom. It contains no credentials.

- `POST /camera/credentials`: import a JSON blob containing `certificate`
  (leaf PEM), `private_key` (PEM), `platform`, `device_id`, `firmware`, and
  `executable_info` (JSON object). This is a development provisioning route,
  following the existing device-local configuration model. No token is
  imported here, and there is no secret download route.
- `POST /camera/capture`: start one background capture for the shown cloud
  printer. Concurrent captures and rapid repeated requests are rejected.
- `GET /camera.jpg`: last verified JPEG, only for its owning printer slot and
  serial. It is never served for another selected printer.
- `POST /camera/automatic` with form field `enabled=1` or `0`: toggle periodic
  capture. Sixty seconds between successes, exponential backoff after errors.
- `DELETE /camera/credentials`: disable periodic capture and clear this
  feature's separate NVS namespace, without changing MQTT configuration.

Credential loading/NVS access occurs on the main loop before creating the
PSRAM-stack network worker. The worker only performs signing and network I/O.
The short HTTPS connection is destroyed before TUTK starts. Images stay in
PSRAM and are decoded once per generation into a small cached RGB565 sprite.
No desktop or NAS component is needed after provisioning.

Keep tokens, app credentials, tickets, NVS backups, logs and actual chamber
images out of Git. The repository already ignores `.cache/`.
