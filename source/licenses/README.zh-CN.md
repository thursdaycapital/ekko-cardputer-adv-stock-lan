# 本候选的许可与来源原文

本目录只为新的 Cardputer ADV 官方 Studio 0.7.36 局域网候选保存原文、版本和来源记录，不复制旧交付的电脑端修改说明，也不覆盖设备上独立原厂应用的许可。

- `../LICENSE`、`ekko-studio/LICENSE`：已与官方 npm 原压缩包逐字节比对的 BSL 1.1 原文。
- `libraries/`：本次实际解析的 M5Unified、M5GFX、M5Cardputer、ArduinoJson、IRremote 原声明或许可，以及版本清单。M5Cardputer 只有原始 MIT SPDX 头部记录，不伪造独立上游许可证。
- `minimp3/`：原 CC0 许可与固定来源；源码位置为 `../include/third_party/minimp3/`。
- `sdk-4.4.7/`、`toolchain-8.4.0-2021r2-patch5/`、`supplemental/`：保留的 SDK、工具链、Arduino、图形字体和三项头像来源仓库原文。FatFs 头部许可直接取自本机固定 Arduino SDK。
- 字体、字典、Unicode、全部头像和视频的原始许可/来源仍就近保存在 `../assets/`，清单以原路径记录，不移走其原声明。

`SOURCE-MANIFEST.json` 保存本次 68 项原文、版本及既有资产资料的 SHA-256、长度和来源。条目数不是独立许可证数量，也不代表完整 SBOM。保留的 SDK 原文不是旧构建覆盖结论；最终二进制对应源码、组件范围和重新链接材料仍需随实际交付核对。

范围说明见 `../THIRD_PARTY_NOTICES.md`；原文与来源记录不构成法律结论，不新增素材许可或品牌背书。
