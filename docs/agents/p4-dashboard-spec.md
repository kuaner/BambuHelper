# P4 dashboard refinement: requested behavior

This document preserves the user's concrete acceptance criteria for the
work-in-progress refinement. It is the originating request, not a generic
upstream specification.

- Follow the approved Android dashboard composition and keep all established
  spacing, font alignment, AMS margins and strong progress animation.
- Only the top info icon opens/closes details. Remove refresh. Use a clear gear
  for settings. Clicking the picture toggles printer artwork and real chamber
  image; default to chamber image and show two selection dots.
- Both pictures retain the same fixed rectangle and full 16:9 content. No crop,
  border or source-dependent movement of any information below the picture.
- Retrieve the chamber image directly from Bambu's cloud via the verified
  TUTK/file protocol; do not require a desktop, NAS, Home Assistant or go2rtc.
  Decode/scale once in a background worker, not in the renderer. Resolve the
  real cloud job name independently; metadata lookup must never block the UI
  or invalidate a successful image.
- MQTT connection and parsing, Wi-Fi management, and cloud camera work each
  have their own persistent owner. Rendering and touch handling must not
  perform DNS, TLS, MQTT or camera waits, task joins or cloud model downloads.
  No second model-preview TLS task, automatic carousel, light automation or
  Tasmota polling is wanted on the P4 dashboard.
- Shared state must be correct without locking the renderer: complete-value
  handoff, monotonic freshness, explicit ownership, and independent consumers.
  Preserve UI notification latches across MQTT snapshots.
- AMS initially follows the active group. Manual selection stays put.
  Disconnection does not display a persistent "retain previous state" message.
  Recover from link and broker failures without requiring power cycling.
- Do not flash until this work and its review/refactor are complete and tested.
  The board is currently disconnected from the computer.

Reference for task structure: /Users/kuaner/Documents/code/xiaoniu/ (single
resource owners plus messages, not a renderer that polls blocking services).
