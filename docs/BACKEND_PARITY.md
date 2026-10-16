# Backend parity with OpenNOW

The backend audit uses OpenNOW revision
`60ce69a07bf94ba484af7406d386ccb28e5a13e6`, recorded as
`OpenNOW-backend-reference` in `upstream-lock.json`. Source paths below are
relative to that repository. This is a source and synthetic-regression audit,
not evidence of authenticated service behavior or console acceptance.

## Transport boundary

PS5 uses OpenNOW's WebSocket/WebRTC compatibility dialect. Its `nvstSdp`
attributes and legacy input envelopes are different from the desktop's native
RTSPS/Mjolnir transport. The implementation must not send indexed RTSP `x-nv`
attributes, `0x020d/0x26` gamepad commands or private Mjolnir NACK-v2 on the
legacy input channel. The WebRTC source is in
`native/opennow-streamer/crates/opennow-streamer-core/src/webrtc/` and
`native/opennow-streamer/crates/opennow-streamer-transport/src/webrtc/`.

The qualified PS5 hardware decoder, fixed native bitrate floor, reference
policy, and stereo default remain unchanged. AV1, 4:4:4, microphone capture,
rich Sony HID attachment, Cloud GSync and L4S are not advertised as supported
settings. Upstream's WebRTC engine rejects HDR at `core/src/webrtc/mod.rs:135-140`;
its working native HDR negotiation is RTSP-specific. Keeping the PS5's existing
Main10/HDR request is not proof that this WebRTC service path delivers HDR.
Decoded SPS metadata remains authoritative.

## Source-backed changes

- Cloud allocation and cleanup follow `native/opennow-core/src/cloudmatch.rs`:
  native HTTP identity at 2259-2285, public learned control endpoints at
  2398-2453, request fields at 1602-1759, discovery at 930-1031, minimal RESUME
  at 1093-1224 and 1540-1600, progress normalization at 2655-2708, and deletion
  validation at 2728-2748. PS5 retains its WebRTC-specific codec, bitrate and
  audio request fields rather than substituting the native RTSPS defaults.
- `src/session_file.hpp` retains the exact authenticated owner, seat ID and
  accepted profile in an atomic owner-only record, following
  `native/opennow-core/src/cloudmatch.rs:456-513`. Recovery uses that profile
  only for the same owner and discovered seat, never another game's defaults.
  A known nonresumable ending also prevents that exact saved seat from being
  automatically resumed after restart. No credentials or endpoints are stored.
- Signaling and reliable/partial input readiness follow
  `core/src/webrtc/signaling.rs:141-218,222-276` and
  `transport/src/webrtc/input.rs:133-256`. A valid nonzero version handshake
  and both input channels are required. An elapsed timer is not negotiation.
- SDP payload selection follows
  `transport/src/webrtc/mod.rs:331-505`. Answer payloads must be offered in the
  selected m-line; feedback is an offered intersection, disabled tracks remain
  disabled, active audio is receive-only, and stale codec/bandwidth declarations
  are removed. RI parsing and local credential checks follow
  `core/src/webrtc/sdp.rs:5-56`. PS5 retains its tested Main10 no-fmtp fallback
  and one active audio section. It does not claim full rejected-section parity
  for offers containing additional microphone sections.
- High-FPS negotiation follows `core/src/webrtc/sdp.rs:60,75-96` through 120 FPS,
  without replacing PS5's existing quality-dependent FEC/pacing or fixed
  resolution/bitrate policy. The unsupported `m=mic` placeholder is removed.
- Slot-zero gamepad state follows `core/src/lib.rs:2973-3002`; its connected
  bitmap is `0x0101`, not `0x0001`. Legacy WebRTC v3 framing remains unchanged.
  Radial per-stick tuning and vibration intensity follow
  `opennow-qt/src/input/ControllerInput.cpp:466-483,777-815,1003-1013`.
  Relative-mouse tuning follows `core/src/lib.rs:3017-3050`.
- Bounded haptic records follow `transport/src/nvst_haptics.rs:20-70`.
  Server idle-warning and termination decoding follow
  `transport/src/nvst_server_control.rs:22-78,87-201`. These decoders do not
  establish that a legacy server emits the native control messages. Unknown
  termination codes retain unknown resumability instead of being called normal
  game exit. Physical motors stop on expiry, capture loss, disconnect and
  stream replacement/stop/failure.
- QoS follows `transport/src/nvst_control.rs:114-154` and
  `transport/src/nvst.rs:1140-1174`. The client elapsed clock is 90 kHz, reports
  run every 50 ms, and only successful admission advances the report sequence.
  Unavailable sender-frame, bandwidth, delay, jitter and loss fields stay zero.
  The old completed-AU counter and byte totals are not substituted into those
  unrelated wire fields. Actual RTP/decoder counters and RTCP remain separate.
- Auth renewal follows `native/opennow-core/src/gfn.rs:1037-1168,2842-3006`.
  A separate transport and private credential snapshot keep blocking HTTPS off
  media polling. Only the owner publishes and persists a completed refresh.
  Transient verification failures preserve already verified identity and rotated
  tokens; a verified changed identity is rejected before persistence. Access and
  service-ID expiry remain distinct, following `gfn.rs:258-265,3069-3085`.

The `core/` and `transport/` aliases above are under
`native/opennow-streamer/crates/opennow-streamer-{core,transport}/src/`.

## Settings and UI contract

All new fields are part of `StreamSettings`, survive V4 settings-file writes,
and default when reading V1-V3. Invalid records publish no partial state.
Existing atomic replacement, owner-only permissions and account-file isolation
remain in place. Video presets preserve these non-video preferences.

| Field | Type | Accepted range | Default | Suggested label |
| --- | --- | --- | --- | --- |
| `controller_left_deadzone` | `int` | 0-50 percent | 5 | Left stick deadzone |
| `controller_right_deadzone` | `int` | 0-50 percent | 5 | Right stick deadzone |
| `vibration_intensity` | `int` | 0-100 percent | 100 | Controller vibration |
| `mouse_sensitivity` | `int` | 10-300 percent | 100 | Mouse sensitivity |
| `persist_game_settings` | `bool` | false/true | true | Remember in-game settings |

The persistence request is enabled only for a title whose actual GFN feature
metadata includes `IN_GAME_SETTINGS_PERSISTENCE_ENABLED`, as in
`native/opennow-core/src/gfn/catalog.rs:822-825`. Unknown capability is false.
Codec, resolution, FPS, bitrate, quality, network policy, HDR/bit-depth mode and
audio fields retain their existing validated types and hardware bounds.

The television UI files are intentionally untouched. A redesigned UI can bind
the fields above and use `Cloud::recover`, `CloudView::sessionConflict`,
`Stream::sessionTimer`, and `Stream::serverTermination`. Startup invokes
recovery before another allocation. A local stream failure retains its remote
session rather than treating EOF as permission to delete it. Explicit stop
still performs cleanup; a rejected cleanup keeps the exact seat for retry.

## Acceptance still required

Run the packaged candidate on a PS5 and verify fresh sign-in/start, every queue
and seat-setup transition, app restart with an existing session, conflicts with
another game's seat, transient poll/stop failures, and explicit cleanup.
Verify that the service acknowledges the separate input channels and handshake,
controller disconnect/reconnect releases held state, the keyboard overlay keeps
the controller connected-neutral, and real server feedback reaches DualSense
motors with correct intensity, expiry and lifecycle stops.

Exercise idle warnings, known and unknown server terminations, a long session
crossing both token expiries, and video stalls followed by recovery or bounded
failure. Check actual decoded HDR/color metadata and measured cadence separately
from requested profiles. Host fixtures and native import auditing cannot prove
these properties.
