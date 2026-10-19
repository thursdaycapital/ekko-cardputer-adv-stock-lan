# Candidate source, licenses and asset provenance

This notice describes the **Cardputer ADV official Studio 0.7.36 LAN compatibility development candidate** in this source directory. It is not a notice for the previous combined release, a final binary SBOM, an endorsement by any upstream project, or a legal compatibility determination. Physical acceptance and final packaging are recorded separately.

## Ekko Studio origin

The firmware derives from Ekko Studio ESP32 firmware at commit **`1242ac63556491c92ba6dd49d89fd19c91a6bdd5`**, official version **0.7.36**, with the existing Cardputer ADV hardware/UI adaptation and new standard LAN chat, selection, history, RAM-compressed short recording and streaming audio adapters. The original repository is <https://github.com/EKKOLearnAI/ekko-studio>.

`LICENSE` and `licenses/ekko-studio/LICENSE` are unchanged copies of the verified official npm `ekko-studio@0.7.36` `LICENSE`. Both copies were compared against the original tarball entry. Preserve the complete **Business Source License 1.1** text, its EKKOLearnAI attribution, Additional Use Grant, Change Date and Change License. Do not replace these parameters with a blanket MIT/Apache declaration. Separately licensed components and assets below retain their own notices.

The change consists of firmware adaptation to the existing official LAN endpoints. The independent factory application already installed on the device is outside this candidate source directory; its own source and notices must accompany it if a later distribution includes that separate application.

## Libraries and framework

| Component | Pinned source/version | Preserved text |
| --- | --- | --- |
| M5Unified | `m5stack/M5Unified` 0.2.26 | `licenses/libraries/M5Unified/LICENSE`, original MIT text and version manifest |
| M5GFX | `m5stack/M5GFX` 0.2.33 | `licenses/libraries/M5GFX/LICENSE`, original font notices below that directory |
| M5Cardputer | `m5stack/M5Cardputer` 1.1.1 | Original 2025 M5Stack Technology CO LTD / MIT SPDX header section and version manifest in `licenses/libraries/M5Cardputer/`; no standalone upstream LICENSE is invented |
| ArduinoJson | `bblanchon/ArduinoJson` 7.4.3 | Original Benoit BLANCHON MIT text in `licenses/libraries/ArduinoJson/LICENSE.txt` |
| Arduino-IRremote | 4.7.1, declared PlatformIO dependency | Original MIT text in `licenses/libraries/IRremote/LICENSE`; declaration is not proof that every library section is linked |
| minimp3 | `lieff/minimp3` commit `ea99364f61c14656440e8d77e9c233ccf3124633` | Original CC0 text, source header and provenance in `include/third_party/minimp3/`; duplicate notice files in `licenses/minimp3/` |
| Arduino ESP32 | 2.0.17; PlatformIO framework `3.20017.241212+sha.dcc1105b` | Original LGPL 2.1 text in `licenses/supplemental/arduino-esp32-2.0.17/LICENSE.md`; component notices remain distinct |
| ESP-IDF SDK and toolchain | Arduino's pinned 4.4.7 SDK; Xtensa GCC runtime 8.4.0-2021r2-patch5 | Original root/component texts and notices in `licenses/sdk-4.4.7/`, `licenses/supplemental/esp-idf-v4.4.7/` and `licenses/toolchain-8.4.0-2021r2-patch5/` |
| FatFs | R0.13c, pinned ESP32-S3 SDK header referenced by retained legacy SD recording source; that adapter is not linked in the dev10 RAM build | Complete unchanged ChaN copyright/condition/disclaimer section in `licenses/sdk-4.4.7/fatfs/ff.h.notice.txt` |

The dev10 candidate records at most five seconds into a RAM IMA ADPCM buffer, then streams decoded PCM to official Studio. Its Ekko voice path does not mount, format or write an SD card. `OfficialRecordingStore.cpp` and its regression tests remain as legacy SD source; `platformio.ini` excludes that adapter from this build. Retaining its FatFs notice does not represent it as a linked dev10 recording dependency.

The M5GFX material also retains the original efont, IPA and GFXFF font texts. Original LovyanGFX, TFT_eSPI and TomThumb referenced notices are retained under `licenses/supplemental/`; a license-file revision is not represented as the exact revision of all code or font data originally imported by M5GFX.

`platformio.ini` pins the build dependencies. `licenses/SOURCE-MANIFEST.json` records copied paths, hashes, byte counts and retained source metadata. SDK notices retained from the previous verified public source set are original texts, not a re-use of that earlier build's coverage counts. This candidate has not received an exhaustive final-section SDK audit, and these texts alone do not establish complete corresponding-source or relinking materials for a future binary distribution.

## 12px fonts, dictionary and Unicode data

`assets/fusion-pixel/` retains the complete original notices for **Ekko Pixel12 Chinese Compatible**, derived from **Fusion Pixel Font 2026.09.01 12px monospaced zh_hans** and 180 unchanged efont compatibility glyphs. Preserve `OFL.txt`, `EFONT-LICENSE.txt`, `LICENSES/`, `NOTICE.md`, source BDFs, provenance and conversion evidence together. Copyright notices include TakWolf and /efont/ The Electronic Font Open Laboratory. The derived family name is distinct from upstream names; no upstream endorsement is implied.

The web font **Cardputer Pixel Web** has its separate source/subset provenance in `assets/fusion-pixel/web/provenance.json` and shares the retained Fusion notices. The unmodified bdfconv 2.36.18 source and its original license remain under `assets/fusion-pixel/upstream/u8g2-bdfconv-2.36.18/`; it is a font build tool, not an assertion that a separate U8g2 display driver is linked into this candidate.

Pinyin data derives from `rime/rime-pinyin-simp` commit `0c6861ef7420ee780270ca6d993d18d4101049d0`. Its Apache 2.0 text, AUTHORS, original dictionary, source metadata and modification notice remain under `assets/pinyin/`. Emoji property ranges derive from Unicode 17.0.0 data; the complete Unicode License V3, source and generated-data notice remain under `assets/chat-symbols/`. These data notices do not license unrelated program code or brand artwork.

## Avatars and opening video

All 16 saved original SVG/PNG avatars, per-asset URLs, source kind, SHA-256 and known permission limits remain in `assets/avatars/manifest.json`, `README.zh-CN.md` and `originals/`. The original images and generated LCD pixels are distinct records. Repository license copies for the identified Hermes, Kimi and Octicons sources are retained in `licenses/supplemental/assets/`; they are not represented as separate trademark permissions. Several asset-specific permissions remain unestablished in the original manifest. Brand names and marks remain those of their respective owners; no general sublicensing permission or endorsement is added here.

The four-second opening is derived from the **user-provided** `Ekko-Studio-Opening-16Agents-4s-2K60.mp4`. Its archived source, SHA-256 `7d03587837e13a17145a02b973026614545e3ab8142dc79e107369dba67f12fc`, Remotion 4.0.535 source project and render metadata remain under `assets/boot-video/`. The embedded output is a 240×135, 15fps, 60-frame JPEG sequence; it is not an MP4 decoder or a bundled Remotion runtime. Source and payload hashes are recorded in `asset-manifest.json` and `remotion-build-report.json`.

The video project retains its original `private: true` / `UNLICENSED` package metadata. User provision and prior use are recorded provenance; this notice does not create a new public redistribution permission for that video or its contained brands. Rendering-tool versions and package-lock data identify the build tools, not a transfer of their licenses to the rendered images.

Preserve these original notices and the manifest alongside candidate source. Final delivery scope, unresolved provenance items and physical verification must be checked for the actual release; this document does not mark them all complete.
