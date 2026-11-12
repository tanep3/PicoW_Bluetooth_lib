# AVRCP media-control console

[日本語](README_jp.md)

Use a Pico W to **pause and resume the player on a phone or computer** sending it Bluetooth audio. The Pico receives audio as an A2DP Sink and sends commands back to that same source as an AVRCP Controller.

Received audio is discarded: **the Pico makes no sound** and does not record it. Check pause/resume on the source player's screen.

<a id="build-safely"></a>
<a id="switch-to-a-hardware-use-configuration"></a>
## Build and flash

You need a Pico W, a USB data cable, a USB serial terminal, and a phone or computer supporting Bluetooth Classic audio output and AVRCP controls. Follow the [build prerequisites](../../docs/build_guide.md#prerequisites) and keep the full repository layout; this directory alone is insufficient.

Find the [source's Classic Bluetooth address](../../docs/build_guide.md#peer-address) and set the Bash variable `SOURCE_BT_ADDRESS` to it. **Do not use the Pico address or a Wi-Fi MAC address.** Run these commands from the repository root with `PICO_SDK_PATH` set to your prepared SDK directory:

```sh
: "${PICO_SDK_PATH:?Set this to your Pico SDK directory}"
: "${SOURCE_BT_ADDRESS:?Set this to the verified source Bluetooth address}"
cmake -S examples/AvrcpMediaControlSample -B build-avrcp-media-enabled \
  -DAVRCP_MEDIA_ENABLE_RADIO=ON \
  -DAVRCP_MEDIA_ALLOW_PAIRING=ON \
  -DAVRCP_MEDIA_PEER="$SOURCE_BT_ADDRESS"
cmake --build build-avrcp-media-enabled -j2
```

[Flash via BOOTSEL and open the USB terminal](../../docs/build_guide.md#flash) using `build-avrcp-media-enabled/avrcp_media_control_sample.uf2`. If you want to keep the current firmware, first verify its backup and restoration procedure. The console prints `INERT` at boot; radio starts only when you send `START`.

## Operator workflow

Send **one uppercase command per line**, ending with LF or CRLF and no extra spaces. Check each result before sending the next command.

1. Send `START` and wait for `READY` and `LOCAL_ADDRESS`. Verify the address. Both the Bluetooth and USB names are `AVRCP Media Control`.
2. Open Bluetooth settings on the source, then send `PAIR` to the Pico. Select and pair the intended Pico within five minutes. Only the source specified at build time is allowed.
3. Select Pico as the source's audio output and play a track. Check `streaming=1` with `STATUS`. If `ct_cid=0` after A2DP connects, send `CONNECT_AVRCP` once and wait for a nonzero `ct_cid` in `STATUS`. A local return value of zero alone does not mean connected.
4. Send `PAUSE` once. Wait for `CLICK_COMPLETE remote_accepted=1`, then send `QUERY_STATUS`. Check `interpreted=1` and `playback=2` in `PLAY_STATUS`. Wait several seconds and confirm that the player screen shows paused.
5. Once confirmed, send `PLAY` once. Again wait for `CLICK_COMPLETE remote_accepted=1`, then query status. Check `interpreted=1` and `playback=1` and confirm playback on the screen.
6. Send `SHUTDOWN` when finished. Wait for `STATUS` to show `initialized=1 hci=0 running=0`, then restore the original firmware if needed. Restore the source's audio output too.

## Troubleshooting

- **No USB console:** check that you flashed the UF2 built with `AVRCP_MEDIA_ENABLE_RADIO=ON`. The OFF build does not start USB serial.
- **Cannot connect:** verify the configured source address and the selected Pico address. Keys are RAM-only. After rebooting Pico, you may need to remove only its saved pairing on the source and pair again.
- **`DENIED`:** inspect the connection and pending commands with `STATUS`. Wait for the response, then send a fresh line. Line editing is unsupported; do not paste a batch of commands.
- **Disconnect without shutting down:** `DISCONNECT` requests AVRCP and Sink disconnection and closes Sink admission and pairing. Radio stays on, and the configured source may reconnect AVRCP. Use `SHUTDOWN` to end radio activity.
- **Restart after shutdown:** reset is required after `SHUTDOWN`, a fatal error or a deadline. The total run limit is 15 minutes from `START`.

`LISTEN` opens Sink admission without changing discovery or pairing permission. `PAIR` is unavailable in builds with `AVRCP_MEDIA_ALLOW_PAIRING=OFF`. Before startup, `STATUS initialized=0 hci=255` means uninitialized; 255 means unknown, not an observed radio state.

<a id="four-different-meanings-of-success"></a>
## Reading results and limits

- `PRESS_LOCAL rc=0` means the local library admitted the request.
- `CLICK_COMPLETE remote_accepted=1` means **both** matching press and release responses were Accepted (9). Rejected (10) and Not Implemented (8) are not success.
- `PLAY_STATUS ... interpreted=1` means the current query received Implemented/Stable (12). Playback 0 is stopped, 1 playing and 2 paused. Length/position may be unknown (`UINT32_MAX`) or delayed. `interpreted=0` does not confirm state.
- Finally, check the intended player's screen. Protocol acceptance and actual behavior can differ with the OS or player.

The only media commands are `PLAY` (0x44) and `PAUSE` (0x46). Track changes, volume controls and AVRCP STOP (0x45) are not implemented. `SHUTDOWN` stops the Pico application, not the remote player.

**This exact public sample has host/ARM checks but has not been run on hardware (NOT_RUN).** Trials using other applications are separate; see the [Validation scope](../../docs/validation.md).

<details>
<summary>Notes for integrating the sample into an application</summary>

## Implementation choices worth copying

- `Application.h` owns one Sink and one Controller for the entire process, including shutdown. Do not recreate owners after stopping or mix them with the legacy AVRCP plugin.
- Callbacks copy events and borrowed bytes into a fixed 32-entry queue. They do not print, wait for USB, call Bluetooth APIs or retain borrowed pointers. The main loop sends release after the press callback returns, without duplicating a release already in progress.
- The v6 SDK overlay checks transaction labels, operations and press/release bits. Copying an application binding does not replace wire-response correlation.
- Each input line keeps the binding from its first consumed byte. Changed/missing connections and pending commands/queries cause rejection. The application cannot know when text entered a host USB backlog. It reads at most 16 bytes per loop and discards invalid lines through newline.
- Deadlines are checked before SDK polling: startup 15 seconds, Controller response 700ms, application command/query 1500ms, pairing five minutes and total runtime 15 minutes. A five-second watchdog reboots to `INERT` if polling stalls. These are not measured real-time guarantees; slower peers may need policy changes. No automatic retries or held-command repeats are provided.
- USB stdout wait is zero, but not every runtime path is time-bounded. Check missing output, event sequences and queue loss; incomplete logs cannot prove that all responses arrived. HCI dumps and persistent key storage are disabled.
- `SHUTDOWN` powers off the shared radio. This policy is unsuitable unchanged for products with unrelated active profiles.

## Choosing an existing AVRCP example

- This example: Pico Controller sends PLAY/PAUSE to the source player; audio is discarded.
- `examples/DeveloperSamples/avrcp_target.cpp`: observes controls from a remote Controller to the Pico Target. It neither implements a player nor forwards commands to a Controller.
- `examples/AvrcpMainLoopSample`: Controller/Target, browsing, artwork and queue implementation. Its Target uses a rejecting placeholder policy; some configurations require peer browsing support.

Audio Source/Sink and AVRCP Controller/Target are separate roles. Follow their API and shared-runtime contracts when combining them, and never initialize BTstack twice.

</details>

<a id="compile-only"></a>
## Offline verification

These optional development checks are **not a prerequisite for the hardware-use build above**. This is the same sample with radio disabled, not a separate sample. It does not start USB serial and cannot be used to try the controls.

```sh
cmake -S examples/AvrcpMediaControlSample -B build-avrcp-media-offline \
  -DAVRCP_MEDIA_ENABLE_RADIO=OFF -DAVRCP_MEDIA_ALLOW_PAIRING=OFF \
  -DAVRCP_MEDIA_PEER=00:00:00:00:00:00
cmake --build build-avrcp-media-offline -j2
bash tests/host/run_avrcp_media_control_sample_tests.sh
```

Radio and pairing default to OFF. Radio ON requires a nonzero six-octet peer address; pairing ON with radio OFF is rejected. The OFF build still type-checks the code, but does not establish enabled-radio linkage or runtime behavior. Host tests use modeled APIs, not hardware, pairing, the real SDK serializer or a player screen.
