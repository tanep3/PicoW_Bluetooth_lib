# BTstack patch-script attribution

The Python files in this directory include original modification logic and
quoted or adapted portions of BlueKitchen BTstack. **Distributing scripts
without the generated C files does not remove the terms on those upstream
excerpts.** The project MIT license applies only to project-original portions;
it is not a replacement license for a complete script containing upstream code
or for the generated composite source.

The upstream source revision is
[`501e6d2b86e6c92bfb9c390bcf55709938e25ac1`](https://github.com/bluekitchen/btstack/tree/501e6d2b86e6c92bfb9c390bcf55709938e25ac1).
The affected files carry these original notices:

- Copyright (C) 2014 BlueKitchen GmbH: `src/l2cap.c`,
  `src/classic/rfcomm.c`, `src/classic/goep_client.c`.
- Copyright (C) 2016 BlueKitchen GmbH: the AVRCP common/Target/Controller and
  browsing sources, and AVDTP common/acceptor/initiator/sink sources listed in
  the manifests below.
- Copyright (C) 2022 BlueKitchen GmbH: `src/classic/a2dp.c`.
- Copyright (C) 2023 BlueKitchen GmbH: `src/classic/avrcp_cover_art_client.c`.

The [verbatim file-level copyright, conditions and disclaimer](../licenses/BTstack-overlay-source-notices.txt)
are supplied with these scripts. Retain them and the
[BTstack standard license](../licenses/BTstack.txt). Its standard grant includes
a noncommercial/personal-benefit condition. The
[Raspberry Pi supplemental grant](../licenses/BTstack-Raspberry-Pi.txt) is a
separate permission within its defined Customer, Product, Customer Product and
distribution scope; it is not an unrestricted MIT sublicense.

## Script and source mapping

- [AVRCP overlay manifest](../licenses/avrcp-overlay-manifest.json):
  `patch_avrcp_common.py`, `patch_avrcp_target.py`,
  `patch_avrcp_controller.py`, `patch_avrcp_browsing.py`,
  `patch_avrcp_browsing_controller.py`, `patch_avrcp_cover_art.py`,
  `patch_goep_client.py`, and the fragments `avrcp_controller_fragments.py`,
  `avrcp_controller_custom.py`, `avrcp_target_metadata_patch.py`.
- [SPP overlay manifest](../licenses/spp-transport-overlay-manifest.json):
  `patch_spp_ertm_sdk.py`, for L2CAP and RFCOMM.
- [Experimental A2DP manifest](../licenses/a2dp-same-peer-manifest.json):
  `patch_a2dp_same_peer.py`, with `a2dp_media_cid_patch.py`,
  `a2dp_role_lifecycle_patch.py`, `a2dp_transaction_patch.py`.

The manifests identify the upstream file hashes, current generator/fragment
hashes, and source-selection CMake files. Generated files must keep the complete
original copyright/license header. Distribute this notice and the applicable
license files even if copying the scripts independently of the full repository.

Normal AVRCP/SPP helpers select their overlays for opted-in consumer targets.
The A2DP same-peer overlay has an explicit experimental helper and is not
selected by ordinary Source/Sink helpers. Neither choice changes the upstream
licensing conditions. A static license inventory is not a behavioral, hardware,
security, interoperability or release-binary validation.

日本語: Python内の置換元コードにもBTstack由来の部分があります。生成済みCを
配布しなくても、その引用・派生部分の条件は残ります。この通知、元の著作権表示・
条件・免責、該当するライセンスを一緒に維持してください。独自変更をMITで提供する
ことは、上流コードや生成物全体をMITへ変更することを意味しません。

## Public Sink raw SBC configuration overlay

`patch_sink_raw_configuration.py` quotes and modifies the pinned2016
`src/classic/avdtp_acceptor.c`. Its separate
[manifest](../licenses/sink-raw-configuration-manifest.json) records the original,
generator and generated hashes. The complete upstream notice is retained in
output. The ordinary Sink helper selects this overlay per consumer; Source-only
siblings keep the installed original. The licensing terms above also apply to this overlay.

The optional Sink START decision extension in the same generator quotes the
pinned acceptor's START selection and refusal branches. It preserves the complete
upstream notice, keeps Source automatic START, and emits a complete Sink START
refusal body. The manifest identifies the upstream and generator versions. No upstream licensing terms change.
