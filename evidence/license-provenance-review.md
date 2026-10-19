# 新候选许可及来源核查

2026-10-18。仅修改新候选的许可、来源和证据文件；没有修改旧稳定交付、运行时代码、设备或服务。本文件独立编号，不与主 FINDINGS 并发编辑。

| 编号 | 发现 | 严重性／置信度 | 当前处理／边界 |
| --- | --- | --- | --- |
| LP-001 | 新 source 根目录缺少上游 LICENSE | P1／高 | 已从全部文件校验过的官方 npm 0.7.36 原包复制至 `source/LICENSE` 和 `source/licenses/ekko-studio/LICENSE`，并再次与 tgz 的 `package/LICENSE` 逐字节核对。保留 EKKOLearnAI 的完整 BSL 参数，未改写为统一 MIT/Apache。 |
| LP-002 | 新目录没有独立的候选第三方来源说明 | P2／高 | 新建 `source/THIRD_PARTY_NOTICES.md`、`licenses/README.zh-CN.md`、`licenses/SOURCE-MANIFEST.json`。只保留新候选适用原文，不引入旧电脑修改说明、原厂源码归属或旧镜像的覆盖结论。 |
| LP-003 | M5Cardputer 1.1.1 没有观察到独立 LICENSE；头部有 2025 M5Stack / MIT SPDX | P2／高 | 保留实际解析库头部原字节、原版权行及版本清单。此前稳定资料对固定上游全树也记录了此缺口；本轮不借 M5Unified 的版权行构造一份 M5Cardputer 独立原文。 |
| LP-004 | 多个头像的独立素材／商标许可没有建立 | P2／高 | 原 16 项 manifest、图片和来源声明保留；Hermes/Kimi/Octicons 已有仓库许可原文复制入本候选。来源仓库许可、素材许可与商标许可不混为一项，也不声明品牌背书。 |
| LP-005 | 开机视频是用户提供；其 Remotion 项目仍标 `private: true`、`UNLICENSED` | P2／高 | 保留原片、派生帧、源码和哈希，明确属于来源记录，未据“用户提供”新增普遍公开再分发许可。本轮没有修改视频或为其指定新许可。 |
| LP-006 | 新 SD 录音后端直接使用 FatFs，原候选许可目录未就近保存该声明 | P2／高 | 从实际固定 ESP32-S3 Arduino SDK 的 `fatfs/src/ff.h` 复制第 1–19 行完整 ChaN 版权、条件及免责声明；记录原文件 SHA 和声明节 SHA。 |
| LP-007 | 旧 SDK 审计资料不等于新候选最终二进制审计 | P2／高 | 仅复制原始 SDK/工具链文本和来源元数据，没有复制旧覆盖数字作为本候选结论。外部组件准确 revision、dirty SDK 重建、最终节级组件/notice 和对应源码/重链接材料边界仍保留，不据根许可做完整性结论。 |
| LP-008 | 字体不是纯 Fusion：有 180 个 efont 兼容字及单独网页子集 | P2／高 | 现有完整 OFL、EFONT、Ark/Cubic/Galmuri 文本、BDF、工具许可和各自 provenance 原样保留并索引。没有把 efont 或其他资产归入根 BSL。 |
| LP-009 | 旧图形/字体引用的许可文件 commit 不能证明所有导入代码的 commit | P3／高 | 继承原 LovyanGFX/TFT/TomThumb 原文，保持来源说明里的限制，不把“许可文件固定化”改称“代码全量来源已闭合”。 |
| LP-010 | 源码仍有一条“U8g2 retained”历史注释，而当前依赖清单没有独立 U8g2 库 | P3／高（注释与配置观察）；是否全部底层解码来源同一范围未展开 | 只在本说明区分 bdfconv 字体构建工具与运行时独立显示库，没有改运行时代码。M5GFX 仍有自身字体解码来源及原声明，不能据一条历史注释推断额外库已链接。 |
| LP-011 | 既有头像 README 的生成命令沿用旧 monorepo 路径 | P3／高 | 原声明保留，未为本次许可任务改资产说明；新候选实际构建/生成入口应以当前根 README 为准，不把历史命令当作已验证的独立目录命令。 |

## 本次核验

- 根许可证 2650 字节，与已验证官方 npm 压缩包原条目一致。
- `SOURCE-MANIFEST.json` 的 68 项文件逐项核对长度和 SHA-256；其中 32 项保留了旧稳定来源清单对应的上游元数据并核对原哈希。旧记录中的 ESP32-C3 专用 BT 文本没有纳入本次 ESP32-S3 候选。
- M5/ArduinoJson/IRremote 文本从本次 `.pio/libdeps/cardputer-adv` 实际解析的固定版本复制；minimp3 声明来自现有固定头文件目录。原版权行和换行按字节保留。
- 本次没有生成 ZIP、发布、烧录、最终镜像 SBOM 或法律兼容性结论；实机及最终打包仍由主任务继续验收。
