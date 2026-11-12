# Pinned AVRCP SDK overlay

[日本語](avrcp_sdk_overlay_jp.md) / [Target features](avrcp_target_features.md) / [Controller](avrcp_controller.md)

The AVRCP CMake helper generates reviewed Target and Controller source variants in the build directory. It never modifies the installed Pico SDK. Each consumer target opts in through the `PICOW_AVRCP_SDK_OVERLAY` target property and compile definition; other executables retain the original SDK sources. Python 3 is required. Do not copy generated files over the SDK.

The baseline is Pico SDK `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`, BTstack `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`. Both generators reject unknown input SHA-256 hashes and missing/ambiguous replacement anchors. Upgrade by reviewing upstream changes, adapting patches and rerunning tests, not by disabling the checks. See the [manifest](../licenses/avrcp-overlay-manifest.json).

## Changes from upstream

- Target: binding-aware SetAddressedPlayer, standard player-settings queries/text/atomic SET, six additional notification types and copied changed responses; item response role/CID lookup correction; CompanyID network byte order; vendor length, notification ID and metadata attribute bounds.
- Controller: settings responses forwarded in bounded raw chunks; owned continuation reassembly capped at 4096 bytes, explicit continuation/abort, remote rejection and release cleanup; transaction/PDU and length checks; charset count/network order/length fixes; setter pair count and value-text request format fixes; parameterized settings helpers.
- This is a repository-local overlay, not an upstream release or a full AVRCP compliance claim. Browsing database, cover art, vendor settings, automatic application behavior and hardware interoperability remain outside this work.

## Notification cancellation boundary

The Controller overlay rejects queued or sent-but-unconfirmed registration cancellation with `ERROR_CODE_COMMAND_DISALLOWED` instead. It uses existing queue/notification tracking, retires that tracking on ordinary terminal notification responses, and preserves established-registration deferred disable and fully inactive no-op behavior. There is no new API, immediate wire cancellation, automatic retry or timeout. This fix applies through the generated Controller overlay; manually linking the pristine SDK does not select it. Original SDK sources, common/Target output and their licenses remain unchanged.

## License and redistribution

The original BTstack copyright/license header remains in every generated file. Upstream excerpts in patch scripts and generated source remain under BTstack terms, not this repository's MIT license. Read [BTstack terms](../licenses/BTstack.txt), [Raspberry Pi BTstack permission](../licenses/BTstack-Raspberry-Pi.txt), and [third-party notices](../THIRD_PARTY_NOTICES.md). The Raspberry Pi permission is limited to the Products/Customer Products defined there; it is not a general unrestricted MIT grant. Preserve applicable notices when distributing modified source or firmware.

## Verification and limits

Parser/serializer coverage uses host tests with mocked transport and ARM linkage. Unknown SDK hashes fail configuration. Mocked framing does not establish real transport fragmentation or interoperability of every extended API. Test commands and chronology are in the development record.

Browsing consumers additionally use hash-pinned `avrcp_browsing.c` (empty SDU guard and service-registration outcome) and `avrcp_browsing_controller.c` (no-attributes request count/length correction). Their original licenses remain unchanged. See [Browsing Controller](avrcp_browsing.md).

Cover Art consumers additionally select generated `avrcp_cover_art_client.c` and `goep_client.c` for owned cleanup, failure/cancel handling and checked OBEX request capacity. Their original notices and licenses remain intact; [Cover Art Client](avrcp_cover_art.md) documents scope and limitations.

The Controller overlay provides owned custom requests, identity-checked raw responses and bounded AVCTP reconstruction via tools/avrcp_controller_custom.py. The Target overlay adds pass-through response policy and removes the original serializer’s extra two padding bytes. Installed originals and licenses remain unchanged. See the role guides and deadline/custom evidence for scope and remaining work.

The common transport provides owned AVCTP sends, vendor continuation and tracked-notification interleaving. tools/patch_avrcp_common.py patches avrcp.c for pending cancellation and common frame capacity. CT/TG double header subtraction and TG metadata attribute-count offset are fixed and tested against the actual common helper and both serializers. Installed sources and licenses remain unchanged.

Target metadata continuation additionally separates vendor-message offsets from AVCTP-frame offsets, bounds each vendor response, preserves retained attribute bytes, and defers notifications until completion/abort. The implementation is generated by `tools/avrcp_target_metadata_patch.py`; installed SDK sources remain unchanged.

[Validation scope](validation.md)
