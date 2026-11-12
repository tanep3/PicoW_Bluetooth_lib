# Third-party notices and license scope

The [MIT license](LICENSE), copyright **2026 Tane Channel Technology**, applies to the original portions of PicoW_Bluetooth_lib. It does not relicense third-party source excerpts, SDK dependencies, toolchain runtimes or firmware resources. Existing file-level copyright and license notices govern their respective portions.

This repository contains library source, examples, tests, build helpers and BTstack-derived patch scripts. It does not include the complete Pico SDK/toolchain, legacy research-source copies, recordings or compiled firmware. The upstream excerpts in patch scripts still carry their original terms, even though generated C files and the full SDK are not bundled.

日本語: 独自部分はMITですが、BTstack由来の生成スクリプト、SDKやリンクされるライブラリなどには別の条件があります。Pico向けの追加許諾には対象機器・利用者・配布形態の条件があり、第三者コードをMITへ変更するものではありません。以下の原文・著作権表示・出典を保持してください。

[The license inventory](licenses/manifest.json) identifies the included license texts, upstream dependency versions, patch-script mappings and SDK-derived example sources. It is an attribution inventory, not a complete per-symbol firmware SBOM or a guarantee for every downstream use.

## 1. SDK and firmware dependencies

These libraries are supplied by the user's SDK/toolchain; their complete source
is not vendored in this repository. The CMake helpers compile/link them into
applications. License copies and file-specific author notices are included here
so that downstream firmware distributions can retain them.

| Component | Where used | Terms and notices |
| --- | --- | --- |
| Raspberry Pi Pico SDK | `pico_stdlib`, CYW43 integration, hardware/async helpers and build support | BSD-3-Clause for SDK-original code, with separately licensed components below. Copyright Raspberry Pi (Trading) Ltd. [Text](licenses/Pico-SDK-BSD-3-Clause.txt). |
| BlueKitchen BTstack | Bluetooth Classic, A2DP, AVRCP, SPP, link-key storage and SBC adapters | BlueKitchen's standard license includes a **noncommercial/personal-benefit condition**. A separate Raspberry Pi supplemental license applies to the specified products. Both [standard terms](licenses/BTstack.txt) and [Raspberry Pi terms](licenses/BTstack-Raspberry-Pi.txt) are retained. |
| George Robotics CYW43 driver | Pico W radio control | Standard driver terms include a noncommercial condition; an alternative license permits use and redistribution **only in conjunction with RP2040 or other Raspberry Pi Ltd semiconductor devices**. [Standard](licenses/CYW43.txt), [Raspberry Pi](licenses/CYW43-Raspberry-Pi.txt). |
| SDK-distributed CYW43 firmware resources | Radio initialization; the inspected map includes `cyw43_btfw_43439` and Wi-Fi/NVRAM resources | Supplied from the CYW43 driver dependency, not authored or relicensed by this project. Retain the SDK/driver distribution's notices. Broadcom's NVRAM notice is reproduced in [source notices](licenses/SDK-SOURCE-NOTICES.txt). Do not treat these binary resources as project MIT code; independently audit any replacement blob. |
| Bluedroid/Android SBC codec | `pico_btstack_sbc_encoder`; decoder in Sink applications | Apache-2.0. Copyright Broadcom Corporation (1999–2012), The Android Open Source Project (2014), and Open Interface North America, Inc. (years in individual source files). [Apache text](licenses/Apache-2.0.txt), [file notices](licenses/SDK-SOURCE-NOTICES.txt). BTstack's wrapper code retains BTstack terms. |
| TinyUSB | USB stdio and custom USB CDC descriptors in examples | MIT. Copyright hathach/Ha Thach and the individual contributors, including Raspberry Pi's RP2040 port. [Text](licenses/TinyUSB-MIT.txt), [file notices](licenses/SDK-SOURCE-NOTICES.txt). UART-only builds may omit it. |
| Pico SDK `pico_printf` / Marco Paland printf | SDK formatted output | MIT. Copyright Marco Paland, 2014 (source also credits 2014–2019 PALANDesign). [Text](licenses/Pico-printf-MIT.txt), [source notice](licenses/SDK-SOURCE-NOTICES.txt). |
| micro-ecc | Included among `pico_btstack_base` compilation inputs | BSD-2-Clause, copyright Kenneth MacKay, 2014. [Text](licenses/micro-ecc-BSD-2-Clause.txt). |
| SEGGER RTT | Included among `pico_btstack_base` compilation inputs | BSD-3-Clause-style terms, copyright SEGGER Microcontroller GmbH; see [license](licenses/SEGGER-RTT.txt) and [file notices](licenses/SDK-SOURCE-NOTICES.txt). |
| Yxml | Included among `pico_btstack_base` compilation inputs | MIT, copyright Yoran Heling, 2013–2014. [Text](licenses/Yxml-MIT.txt). |
| Rijndael AES implementation | Included among `pico_btstack_base` compilation inputs | Public-domain statement by Philip J. Erdelsky in the source. [Verbatim notice](licenses/SDK-SOURCE-NOTICES.txt). |
| MD5 implementation | Included among `pico_btstack_base` compilation inputs | Public-domain dedication by Alexander Peslyak (Solar Designer), 2001, with a permissive fallback and no-warranty notice. [Verbatim notice](licenses/SDK-SOURCE-NOTICES.txt). |
| Arm GCC `libgcc` and `libstdc++` | Runtime archives selected by `arm-none-eabi-g++` | GPL-3.0-or-later with the GCC Runtime Library Exception 3.1. The exception permits eligible compiled combinations under the independent modules' terms; this is not a claim that GCC itself is MIT. [GPL-3.0](licenses/GPL-3.0.txt), [exception](licenses/GCC-Runtime-Library-Exception-3.1.txt). |
| newlib C/math runtime | C runtime archives in the Arm firmware link | Multiple permissive licenses and target-specific terms. Retain the [installed package's collected notices](licenses/newlib-notices.txt); applicability depends on the objects used. This product includes software developed by the University of California, Berkeley and its contributors. |

### BTstack's Raspberry Pi permission is not unrestricted relicensing

The inspected supplemental license is in **Pico SDK
`src/rp2_common/pico_btstack/LICENSE.RP`**, not in the BTstack submodule. It names
Pico W, Pico WH, Pico 2 W, Pico 2 WH, and RM2, together with defined customer
products using or derived from those products. Its grant and conditions must be
read in full. It does not give this project authority to sublicense all BTstack
code as MIT or to promise the same rights on unrelated hardware.

The grant is to a “Customer”, defined as a purchaser of one of the named
products. It expressly permits distribution as part of Products, Customer
Products, or their related documentation or SDKs; it is non-transferable and
non-sublicensable. This notice does not assert that every publisher or recipient
is such a Customer, or that an unrestricted general-purpose bundle meets that
scope.

Outside that supplemental scope, BTstack's standard noncommercial license may
apply if all its conditions are met. Commercial use outside the supplemental
scope needs an appropriate separate grant. Likewise, CYW43 use must satisfy
either its standard noncommercial terms or its device-restricted alternative. References: the exact upstream versions are linked in the
manifest; the complete inspected grants are copied above.

### Versions observed

- Pico SDK: `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`
- BTstack: `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`
- CYW43 driver: `c1075d4bc440422cf2b2fd12c64a1f53f77660ee`
- TinyUSB: `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`
- lwIP: `0a0452b2c39bdd91e252aef045c115f88f6ca773`
- Arm GCC package: `15:13.2.rel1-2`; newlib: `4.4.0.20231231-2`;
  Arm libstdc++ package: `15:13.2.rel1-2+26`.

These identify the audited environment, not mandatory dependency versions.
Recheck notices if upgrading SDKs, changing target hardware or changing linked
components. Including an SDK submodule in the checkout alone does not establish
that every library in it is part of a firmware image; for example, no mbedTLS or
LC3 inputs appeared in the inspected seven sample links.


## 2. SDK-derived example sources

The public Sink example's [`usb_descriptors.c`](examples/A2dpSinkPcmConsumer/usb_descriptors.c)
and [`tusb_config.h`](examples/A2dpSinkPcmConsumer/tusb_config.h) are modified Pico SDK
sources at commit `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`, separate from any
A2DPSourcePlayer USB example or historical research inventory. Both retain their
complete upstream MIT permission and disclaimer. Copyright is Raspberry Pi
(Trading) Ltd., 2020, and Damien P. George, 2019 for the descriptors and 2020 for
the configuration header. These upstream portions are not project-original code.
The descriptor guard is enabled for the example's capture-ON source selection;
the configuration adds `sample_config.h` and enables capture ON while preserving
the original exclusion condition otherwise. See [exact provenance](examples/A2dpSinkPcmConsumer/PROVENANCE.md)
and `current_example_source_inventory` in [the manifest](licenses/manifest.json)
for upstream paths, URLs, hashes and bounded changes. Preserve each file's MIT
notice when redistributing it. This mapping records source attribution; firmware and audio-content redistribution require separate checks.

## 3. Host tools and optional system packages

Some diagnostics invoke installed BlueZ, PipeWire, FFmpeg, `sbcdec`, Python,
pyserial, D-Bus/GLib bindings, and the Linux socket API. Compiler, CMake, Make,
Ninja and picotool are build/deployment tools. Invoking a separately installed
tool does not copy that tool into this source repository. Its package license
still applies if a distributor bundles the tool, its libraries or an OS image.

Do not infer a uniform license for these packages from this project's MIT
license. In particular, FFmpeg licensing depends on its configured components,
BlueZ includes both GPL and LGPL portions, and PipeWire has separately licensed
plugins. For host-image redistribution, use the exact package versions and
package copyright files to prepare a separate complete notice/source bundle.
The separately installed tools are not relicensed or bundled by this source repository.

## 4. Distribution checklist

1. Include `LICENSE`, this notice file and the applicable files under `licenses/`. Preserve upstream headers in source copies, snippets and generated outputs; do not replace them with project-only attribution.
2. Check that the applicable BTstack and CYW43 grants cover the publisher, use, target hardware and distribution form. The standard personal/noncommercial grant and the separate Raspberry Pi grant have different conditions. The project MIT license does not impose a noncommercial restriction on project-original code.
3. For firmware/UF2 distribution, inspect the exact build and linker map, then retain notices for all linked components and embedded firmware resources. Do not advertise the whole firmware as unrestricted MIT software.
4. If bundling extra SDK modules, tools, fonts, images, codecs or audio content, establish their redistribution rights separately. Retaining this notice does not grant rights to added material.

The original license texts control. Seek appropriate rights clarification for a use whose coverage is uncertain.

## 5. BTstack overlays and patch scripts

Current AVRCP and SPP CMake helpers select modified BTstack sources generated
inside each consumer's build directory. The explicit A2DP same-peer experiment
has a separate helper; ordinary A2DP Source/Sink helpers do not select it.
Installed SDK files remain separate. These build choices do not alter licenses.

**The generators themselves contain upstream source excerpts.** Omitting the
generated C files from a source archive does not make that archive MIT-only.
Project-original Python logic/additions may use the project MIT license, while
quoted/adapted BTstack portions and generated composite source retain applicable
BTstack terms. Retain [the tools notice](tools/NOTICE.md),
[verbatim upstream file notices](licenses/BTstack-overlay-source-notices.txt),
[BTstack standard terms](licenses/BTstack.txt) and the
[Raspberry Pi supplemental grant](licenses/BTstack-Raspberry-Pi.txt).

The current BTstack overlay inventories cover 14 unique upstream source files and
16 Python generator or fragment files. The separate opt-in persistent-storage
inventory below also maps an SDK-original startup transformation:

- [AVRCP manifest](licenses/avrcp-overlay-manifest.json): common, Target,
  Controller, Browsing, Browsing Controller, Cover Art and GOEP, including
  controller/custom/metadata fragments. See [scope](docs/avrcp_sdk_overlay.md).
- [SPP transport manifest](licenses/spp-transport-overlay-manifest.json): L2CAP
  and RFCOMM. The original 2014 BlueKitchen notices apply to upstream portions.
- [Experimental A2DP manifest](licenses/a2dp-same-peer-manifest.json): A2DP and
  AVDTP common/acceptor/initiator/sink, including media-CID, role-lifecycle and
  transaction fragments. This is not a production-readiness claim.
- [Public Sink manifest](licenses/sink-raw-configuration-manifest.json): the
  ordinary Sink raw-configuration/recovery and optional START-decision acceptor
  overlay. Its upstream `avdtp_acceptor.c` is already among the 14 files above.
- [Persistent storage manifest](licenses/persistent-storage-manifest.json): the
  opt-in committed-TLV and persistent-bond integration. The generated
  `btstack_cyw43.c` startup composite retains Copyright (c) 2023 Raspberry Pi
  (Trading) Ltd., BSD-3-Clause, covered by [SDK source notices](licenses/SDK-SOURCE-NOTICES.txt)
  and [SDK license](licenses/Pico-SDK-BSD-3-Clause.txt). It is not blanket
  BTstack-licensed; other SDK/BTstack dependencies keep their own terms.

Each manifest records upstream identities, generator/fragment hashes and CMake selection files. Saved generated-output hashes identify previously inspected outputs; they are not a claim that every later build has been reproduced or tested. Retain complete upstream copyright, conditions and disclaimers in generated sources.

The Raspberry Pi permission is limited to the Customer, products and distribution forms defined in its text. Omitting generated C or legacy research material does not remove the BTstack conditions from the active patch scripts.
