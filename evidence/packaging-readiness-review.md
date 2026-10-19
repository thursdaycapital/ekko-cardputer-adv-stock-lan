# dev10 交付白名单只读审查

审查日期：2026-10-19。只读检查当前 source、公开来源资料和构建证据；没有访问设备、网络、后台或私有配置正文，没有打包、发布或修改源码。认证恢复测试仍由主任务进行，本记录不替它下结论。

## 二进制与冻结范围

- `artifacts/cardputer-adv-v9.0.0-dev.10-ram-lan.bin` 实际为 **3,284,000 字节**，SHA-256 **`e753a1b04f22b249c962ad9fb304a5f193a6cd067bf41deedf6654eb0d8b2fb5`**。
- 当前 `.pio/build/cardputer-adv/firmware.bin` 与该 artifact 的长度、完整哈希一致。
- `evidence/tenth-candidate-source-sha256.json` 的 **140 项全部存在且哈希匹配**：include 59、src 13、tests 65、platformio.ini/build_options.py/partitions.csv 各1。src/include 没有清单外文件。
- 该清单不是最终发布包清单。README、根许可、第三方说明、config、assets、licenses 不在其范围；不得据这140项宣称整个交付包已经冻结或脱敏。
- 新增 `tests/integration/physical_auth_recovery.py`、`test_physical_auth_recovery.py` 不在构建冻结清单；它们是事后集成验收工具，不是固件编译输入。它们及文档后续变动不构成上述二进制失配，但最终发布源包应单独记录它们的最终哈希。
- `platformio.ini` 明确排除 `OfficialRecordingStore.cpp`；该 SD 模块及测试仍作为历史回归源码存在，不代表 dev10 挂载或写入 SD。不能以“源文件存在”取代实际链接范围。

## 建议的显式交付白名单

| 类别 | 纳入范围 | 约束 |
| --- | --- | --- |
| 应用更新 | 上述唯一已核对的 dev10 artifact；最终可重命名为教程中的应用更新文件 | 只写 `0x10000`，不是 `0x0` 全镜像，不附带个人 NVS、otadata 或 SPIFFS dump |
| 构建输入 | `source/src/`、`include/`、`config/agents.json`、`config/README.md`、`platformio.ini`、`partitions.csv`、`build_options.py` | 保留 RAM 模块、生成字库/头像/视频 C++ 数据及 minimp3 源码；保留 SD 排除项，勿为清理而改变构建输入 |
| 说明和许可 | `source/README*`、`LICENSE`、`THIRD_PARTY_NOTICES.md`、`.gitignore`、`licenses/`，最终中文安装教程 | 保留原许可和版本信息，不用根许可覆盖字体、素材和库许可 |
| 字库 | `assets/fusion-pixel/` 的 OFL、EFONT、LICENSES、原始 BDF、provenance、码点清单、网页字体来源、bdfconv 完整原文/源码及转换证据 | 不丢失 efont 180 补字和网页子集的单独来源；字体转换日志仅在已审查为无私密信息时明确列入 |
| 字典/Unicode | `assets/pinyin/`、`assets/chat-symbols/` 的完整上游许可、AUTHORS、原数据、生成器、NOTICE、sources/manifest | 区分字典数据许可、Unicode 数据许可与程序许可 |
| 头像 | `assets/avatars/` 中16个 originals、manifest、生成器、generated 数据、必要预览、README；三项已保存上游仓库许可 | SVG/PNG 原图与生成 LCD 像素都应有可追溯记录；不得标成所有素材均获独立授权 |
| 视频 | `assets/boot-video/` 原片、派生帧/数据、manifest、Remotion 源码/package-lock 和 render 说明 | 保留用户来源及 UNLICENSED/private 元数据的真实含义；不带 node_modules、浏览器缓存或构建缓存 |
| 测试 | 已审查静态 `.cpp/.h/.py/.mjs/.md` 测试源码、fixtures 和 runner，包括两个新认证工具 | 不把参数指定的私有 HOME、seed、运行日志、journal、数据库、音频、屏幕快照当测试源码递归纳入 |
| 验收证据 | 最终逐项选定、已脱敏的摘要及校验清单 | 不默认包含整个 evidence；原始会话、屏幕和设备导出需另审 |
| 回退 | 已发布稳定版的纯应用更新文件或明确链接及哈希 | 不把用户私人8MiB备份作为公共回退包；若分发独立原厂应用，另附其对应源码和许可 |

明确排除：整个工作目录的 `private/`、私有 seed/HOME、认证清单/journal、JWT/账户/模型配置、SQLite及备份、NVS/整机Flash/SD读出、原始串口及服务日志、原始聊天音频/截图；以及 `.pio/`、`.git/`、`.vscode/`、node_modules、`__pycache__`、pyc、`.DS_Store`、编辑器/浏览器缓存、旧失败候选 artifact、retired-tests。不要直接压缩整个工作目录，也不要依赖 `.gitignore` 充当发布白名单。

`source/data/ekko-rec.owner` 是遗留的 SPIFFS marker，不是 dev10 所需资源；建议不放入日常更新包，不执行 uploadfs。若为保留历史源码而带上，明确说明它不参与本版录音路径。

## 来源与安全核对结果

- `licenses/SOURCE-MANIFEST.json` **68项**长度/哈希全部匹配现有文件。
- `assets/avatars/manifest.json` **16项**原图均存在且哈希匹配；其中**15项**仍明确写着独立素材许可未建立。这是来源/许可状态，不是本审查作出的法律结论。
- 根 BSL 1.1、M5/ArduinoJson/IRremote、minimp3 CC0、Arduino LGPL、SDK/toolchain、字体 OFL/efont、字典 Apache-2.0、Unicode V3 等原文均有对应目录；M5Cardputer 仅有其实际 SPDX/版权节，没有伪造独立 LICENSE。
- 原片及视频项目来源已经记录，但项目 package 仍为 private/UNLICENSED。来源记录不能自动补成普遍再分发许可。
- 排除缓存后没有观察到 source 内的符号链接；没有发现运行 HOME/seed/数据库目录。限定的 JWT/key 形状文本扫描未检出；唯一“本机用户路径”命中是 `/Users/PRIVATE/...` 的合成安全测试，不是真实用户路径。此有限扫描不等于最终压缩包已完成全量秘密审计。

## 发现（未过滤低严重性或不确定项）

| 编号 | 严重性／置信度 | 发现与影响 | 建议 |
| --- | --- | --- | --- |
| PK-01 | P2／高 | 最终发布清单、`SHA256SUMS.txt`、教程所指 firmware 路径和公共纯应用回退材料尚未组装；安装教程已诚实标为草稿，因此不是当前二进制错误，但不能直接作为完成交付发布。 | 最终白名单组装后校验每个实际文件、可解压性及教程路径；保留当前构建冻结清单，新增完整包清单，不覆盖原证据。 |
| PK-02 | P1／高（风险明确，尚未观察到泄漏） | 工作根同时保存私有运行数据；递归压缩整个 work 会跨越公开源码边界。`.gitignore` 也不能覆盖任意名称的 `--private-dir`。 | 只按白名单复制静态源码与精选摘要；包内重新检查无私有配置、备份、日志、数据库和不受控链接。 |
| PK-03 | P2／高（元数据事实） | 15项头像独立素材许可仍未建立；用户提供的视频项目仍标 UNLICENSED/private。此前 LP-004/005 边界仍在。不能把“来源正确、用户曾使用”写成全部公共授权已闭合。 | 保留真实说明及权利边界；公开分发范围由权利方/维护者确认，本次没有新增许可或品牌背书。 |
| PK-04 | P2／高（材料范围）；最终义务是否充分未作法律判断 | 现有 SDK/框架许可包自己明确不是最终链接节审计、完整对应源码或重链接材料。保留许可文本并不证明二进制分发材料已完全闭合。 | 发布前按实际链接范围完成材料清单；不要为方便把整个 `.pio` 当公共源码包，也不要声称当前清单是最终SBOM。 |
| PK-05 | P3／高 | `THIRD_PARTY_NOTICES.md` 仍以“new ... SD recording ... adapters”和“FatFs ... used by SD recording”描述当前候选；实际 dev10 为 RAM录音且SD cpp被排除。 | 文档区分“保留的旧SD源/回归”与“本次实际固件”；修改文档无需重建已匹配的二进制。 |
| PK-06 | P3／高 | 头像 README 仍要求从旧 monorepo 路径运行 `npm ci`、`node packages/...`；当前独立source根没有对应package.json/目录。固件可用现成生成头构建，但素材重生成说明不能直接照抄。 | 保留来源原文，同时为当前目录补充准确生成入口及sharp等工具依赖；不要把来源文件修改称作已重新验证所有素材。 |
| PK-07 | P3／高 | `source/data/ekko-rec.owner`、旧SD模块和非默认旧协议测试仍在源码树，单独看文件名可能误解为本版仍依赖SD/远程。 | 运行包排除遗留 data；源码若保留历史回归，明确其未链接/未启用范围，不删除其许可证来源或改变当前build filter。 |
| PK-08 | P3／中 | 教程正确限制到已装本项目双固件布局，但“确认分区一致”缺少可重复的分区表检查步骤。熟悉原设备的维护者可依据既有记录操作，陌生读者可能只按8MiB容量误判。 | 最终公开教程增加只读布局核对及不匹配即停止的方法；不将本应用更新文件当通用首刷镜像。 |
| PK-09 | P3／中（措辞风险） | 中文README“重启或断网后自动恢复”出现在前部功能说明，后部又明确当前只取得部分用户反馈、自动鉴权仍待验收。整体边界已写出，但截取前段可能变成超出当前证据的承诺。 | 待主任务认证实验结束后统一刷新README、教程、PROGRESS和候选JSON；若仍未通过，前段注明设计行为/已验收范围。 |

## 安装文档已确认的准确边界

中文README与教程均明确：官方**原版0.7.36**、**同一局域网**、电脑持续开机与Agent安装/授权前提、首次原生登录/Profile；当前不承诺跨网远程；Ekko **无SD、单次最长5秒**、RAM有损IMA暂存后还原16k单声道PCM上传、取消/重启不保留；内存不足不影响文字聊天。原厂SD录音回放独立保留。

教程分区地址与 `partitions.csv`/`FactoryFirmware.cpp` 一致：8MiB，Ekko `0x10000`/`0x330000`，factory `0x340000`/`0x400000`，只更新Ekko应用，不擦整片，不写0x0，不提供未知布局新机首刷；私人整机备份不公开。短句真机成功与5秒边界/重启、认证恢复的证据范围有区分，没有把编译或主机模拟冒充全部实机完成。

本轮没有生成交付包，也没有将以上未闭合项标成完成。二进制冻结匹配是已核实事实，完整公共发布就绪是后续独立关口。
