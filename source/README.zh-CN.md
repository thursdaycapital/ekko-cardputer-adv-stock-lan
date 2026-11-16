# Cardputer-Adv v10 应用源码

本目录对应 cardputer-adv-v10.0.0-alpha.6 本地发布候选；本版实机验收必须以新记录为准，不能继承旧 dev.10/dev.12 结论。应用策略、手势、展示、异步连接、自动鉴权和未知发送恢复已拆为独立生产模块，旧的有界聊天／音频解析及硬件驱动继续复用。

首页左右只浏览，Tab 选择并保存 Studio模型／原生CLI，OK 激活后自动聊天；短 G0／Esc 返回，长 G0 或 Fn＋空格录音。正文、输入框和状态均保持中文 12px。Ctrl＋空格切换中英文；Fn＋上下直接翻记录，Tab 模式可继续使用。

录音只在 RAM 以 4bit IMA 暂存 16kHz 单声道，最多五秒，松开后流式还原 WAV 上传。未确定发送先同步和人工核对，绝不自动重发。完整日常操作见根目录 README。

## 协议和保存边界

- 官方 LAN `/api/auth/mcu-login` 获取普通用户 JWT；标准 `/chat-run` 完成运行、流式回答和恢复。
- 同一 Engine.IO 的 `/global-agent` 仅维护原生设备后台在线状态。状态通道故障独立重试，不关闭正常聊天。
- 官方 HTTP 分页读取历史，单页 12 条；长消息用有界 UTF-8 窗口，不把完整历史放入内存。
- STT 使用官方当前选定提供方的 voice proxy，TTS 使用官方 synthesize；请求自描述 WAV，兼容返回 MP3。裸 PCM 仅在采样率／声道明确时播放。
- 新连接设置保存在 `stockLan`，旧 `mcu` 绑定保留用于回退。仅导入原来的局域网绑定；原有远程记录不会被解释成局域网入口。
- 设备码沿用原值；连接实例 ID 独立生成。网络波动不清空账号或绑定。新账号登录后，选择 Profile 才允许自动恢复。
- Ekko 录音只使用 RAM 临时缓存，不挂载 SD，不写录音文件到内置 Flash；取消或完成后释放缓存，重启不会保留未发送的录音。原厂「录音回放」仍按原有流程使用 SD，独立于 Ekko。

官方模型接口会把提供方密钥包含在授权响应中。设备流式过滤这些字段，不将其写入日志、NVS、聊天请求或发布包；不能声称密钥从不经过设备。账号、JWT、Wi-Fi 配置和整机备份属于私人数据，不能分享。

本候选不读取或清扫旧版本 `/EKKO/TMP/` 中的 OWNER、NEW、BAK 或 PCM 文件，也不格式化 SD 或内置存储分区。已有文件由用户保留。

## 构建

目标为 Cardputer ADV／ESP32-S3／8MiB Flash／无 PSRAM。依赖版本固定在 `platformio.ini`；分区与现有双固件布局一致。

在解压后的交付包根目录先进入 `source/`；如果已在源码目录，无需重复 `cd`。

```sh
cd source
uvx --from platformio==6.2.0 pio run -d . -e cardputer-adv
python3 -B tests/run_tests.py
python3 -B tests/run_http_transport_tests.py
python3 -B tests/run_chat_tests.py
python3 -B tests/run_render_tests.py
python3 -B tests/run_official_voice_tests.py
python3 -B tests/run_localization_tests.py
```

也可在 Python 虚拟环境安装 PlatformIO，再运行 `pio run -e cardputer-adv`。主机声音测试需要 ffmpeg；其余检查需要 C++ 编译器。

`tests/run_http_transport_tests.py` 直接编译选取与历史模块中的 Arduino HTTP 传输代码，仅替换 SDK 边界，检查 HTTP／HTTPS 各自只构造一个客户端，以及借用生命周期、返回码、取消和超时。先构建一次，以取得固定版本的 ArduinoJson 头文件；测试不会连接网络。

音频 HTTP、MP3／WAV／PCM 解码及异步播放任务的回归入口以 `tests/run_official_voice_tests.py` 为准；它只使用本地合成音频，不调用模型或语音服务。`tests/run_audio_queue_tests.py` 仍用于底层 PCM 缓冲所有权检查，录音存储和麦克风队列由默认 `run_tests.py` 覆盖。本局域网版不包含旧 MCU ADPCM 合同或自动监听测试入口。

`build_options.py` 统一固定 M5GFX 的 C++ 包含顺序，并启用编译及链接 LTO，以装入 `0x330000` 应用分区；不修改依赖文件、不压缩或改变显示字体。

## 明确的模型模式

Studio 模式采用官方新对话的兼容选择规则：先保留当前 Profile 合法且可见的默认模型；默认提供方不在当前可用分组或与 Agent 不兼容时，按官方分组顺序选择第一个兼容分组，优先同名默认模型，否则首个可见模型。逐模型接口模式必须明确，显示一次实际选择，不切换其他 Agent，也不修改电脑配置。原生CLI模式只在该 Agent 支持时读取官方安装／原生模型接口，由电脑的 CLI 使用自身登录与配置。选择只保存在设备，电脑设置不变。两模式的会话绑定分开保存。明确 Native 且未指定模型时，完整 HTTP200 目录中的 auth_required/error 仅作为提示，允许尝试 CLI 默认模型；HTTP401/403、未安装、超时或坏响应仍拒绝，实际运行失败仍如实显示。

## 本版验证边界

默认 host suite 包含 ExperienceController、Gesture/Mailbox、SendRecovery、Wi-Fi 重试、严格握手、自动身份验证、会话／历史／模型、录音和采集队列。实际生产模块通过固定接口注入 host transport；主机的内存／音频边界不代表 M5 实机成功。

可选真实 Studio 握手检查（无登录、无 Agent 用量）：

```sh
python3 -B tests/integration/test_studio_upgrade.py 127.0.0.1 --port 8748
```

Management 请求等待限制为750ms、头2KiB、总4KiB；显式网页处理和响应仍同步。TLS SDK 的单次写入仍可能晚返回；此版验收优先明文 HTTP 的同网 Studio。RAM 和任务栈的门槛只能拒绝明显不足，必须进一步取得实机水位。

当前仅更新现有 `0x330000` 应用槽，不格式化或初始化 SD／存储；原厂独立 SD 录音工具保留。

## alpha6 发布状态

标准 PEM 公钥的 LF／CRLF 换行已兼容。真实令牌时间过期与普通 Studio 重启恢复已由真实 M5 验证，恢复后聊天、Wi-Fi断开、断电及16 Agent实体矩阵仍未完整完成。当前源码未发布，不能把历史桌面或 alpha5 记录标为本版实机通过。
