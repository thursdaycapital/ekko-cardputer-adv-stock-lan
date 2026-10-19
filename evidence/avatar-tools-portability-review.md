# 头像工具独立目录与交付文案收尾

范围仅为交付文档、头像生成／检查工具及其独立 npm 锁文件。本轮未修改 src、include、platformio、原图、预览、generated.json、许可原文或固件，未访问 M5 或 Studio 后台，也未打包发布。

- **PK-05（P3／高，已修）／LP-006 范围澄清**：THIRD_PARTY_NOTICES 现按 dev10 描述五秒 RAM IMA 暂存、流式还原 PCM。旧 SD 源和 FatFs notice 仍保留，明确该录音适配器不链接进 dev10；不改上游许可文本。
- **PK-06（P3／高，已修）／LP-011**：generate.mjs 与 test_assets.py 直接读独立 source/config/agents.json。assets/avatars/package.json 精确指定 sharp 0.35.3，package-lock.json 锁定依赖及完整性值；README 给出当前目录 npm ci、只读 --check、资产测试及主动生成入口。
- **AT-01（P3／高，已修）**：首次严格检查失败，差异仅为四个 PNG 的 original 来源注释。元数据清理后的实际文件哈希记录为 sha256，下载原文件哈希保留于 downloaded_source_sha256；冻结头原注释与后者四项全部吻合。生成器现按这条既有来源记录生成注释，仍用 sha256 严格验证实际文件；16 项像素 SHA 无差异。未放宽逐字节检查，也未重新生成资产。

实际 `node assets/avatars/generate.mjs --check` 和 `python3 -B assets/avatars/test_assets.py` 均为退出码 0。后者覆盖 16 项目录顺序、来源及文件哈希、矢量／位图检查和 C++17 ASan／UBSan 边界检查。安装的 sharp 为 0.35.3，当前 Darwin arm64 原生组件为 libvips 8.18.3、librsvg 2.62.90。测试后仅删除本轮新建的 assets/avatars/node_modules。

36 个头像冻结文件（生成头、generated.json、manifest、16 原图及预览）全部与修改前相同。

- include/AgentAvatarAssets.h：`e38d88a2b57383035dc730237b41c84bae46490a39e8b1416acf53a43bc27aeb`
- assets/avatars/generated.json：`2da43a04c99ec3fc81c8556192d834950aae59a1fdd4c6313946f7024ac3c560`
- dev10 固件仍为 3,284,000 字节，SHA-256：`e753a1b04f22b249c962ad9fb304a5f193a6cd067bf41deedf6654eb0d8b2fb5`

旧 140 项源码冻结清单中 139 项一致；唯一差异是 tests/integration/README.zh-CN.md。root 已确认该文件由另一任务更新认证审计说明，不是固件编译输入；本轮未改它，也未覆盖旧清单。src、include、config 和构建输入未改。新文件及各修改文件哈希、只读差异证据和完整头像冻结表见 avatar-tools-portability-check.json。

残余 **AT-02（P3／高）**：source/.gitignore 尚未包含 node_modules 规则。当前本轮依赖已清理，既有发布白名单明确排除 node_modules；将来重新安装后仍应按白名单收包。本轮不扩展修改范围。PK-01—PK-04 的发布材料／来源许可边界没有因本次工具验证而自动闭合。

