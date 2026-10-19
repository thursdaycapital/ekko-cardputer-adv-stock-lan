# Cardputer ADV 官方 Studio 局域网直连固件

兼容基线为 **官方原版 Ekko Studio v0.7.36**，无需电脑补丁或桥接服务。当前固件版本 `cardputer-adv-v9.0.0-dev.10-ram-lan`；开发候选的实际验收状态以交付目录中的记录为准。

## 使用方式

电脑与 M5 连接同一个局域网，电脑保持开机并运行 Studio。第一次在 M5 设备网页选择电脑、登录 Studio 账号并选择 Profile；之后保存连接信息，按保存绑定恢复连接；已验证范围见文末。可直接读取 Studio 的模型配置，不把模型 API Key 预置到固件。

M5 显示 16 个规范 Agent：Ekko、Hermes、Claude Code、Codex、Pi、Grok、OpenCode、DSH、Cursor、Antigravity、Qwen、Kimi、CodeBuddy、Qoder、Copilot、ZCode。某个 Agent 能否实际运行，取决于电脑上的安装、授权、模型及该 Agent 支持的模式。不可用时显示错误，不自动换成其他 Agent。

- Agent 首页方向键切换，OK 进入聊天，聊天页 Esc 返回首页；首页 Esc 返回原厂菜单。
- 直接打字，OK 发送；Ctrl＋空格切换中文／英文，拼音按空格选择候选。
- Tab 进入历史浏览，方向键翻阅，Tab 或 OK 回到输入。
- 按住 Fn＋空格说话，松开发送；短按停止，双击建立新会话，旧记录保留在 Studio。
- Ekko 聊天录音不需要 SD 卡：16kHz 单声道音频在 RAM 中以4bit IMA暂存，单次最长5秒，松开后流式还原 PCM WAV 上传官方 STT。内存不足时明确拒绝录音／上传，文字聊天仍可使用。

当前为**局域网版**。电脑开启远程中继，不会让此版本自动获得跨网连接能力。远程需要后续独立接入和验收。

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
uvx --from platformio pio run -d . -e cardputer-adv
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

## 验证与限制

`tests/integration/` 可启动并逐文件校验独立官方 npm 实例，检查原生登录、后台在线状态、实际 Agent、历史及主机音频往返。测试会产生模型／语音提供方用量。私有种子、账号、日志不得放进源码。

本候选已通过25项主机回归及目标构建；真实M5已在独立官方原版Studio完成无SD短句识别、文字回答及有声播放，采集无溢出、播放无写超时，中文输入与实际屏幕也已核对。已在日常签名App内官方0.7.36取得用户确认：现有DeepSeek配置下的文字回复、识别、回答和实际声音正常。收尾时观察到日常App更新为官方0.7.37，新进程已与M5自动恢复连接；更新后用户已确认M5新消息收到回答，设备新启动记录也显示保存绑定匹配、聊天就绪、已接受1次发送并收到13个回复分片。0.7.37本轮补测为文字，语音不混用前一版本结果。用户反馈5秒自动停止及重开免登录正常；此前08:45UTC的日常遥测仍为上一段1.984秒短录，没有独立取得录满五秒字节数据。新应用启动及保存绑定恢复已观测到，但不能单凭uptime区分断电与重新进入应用。签名失效恢复有新原版进程／双通道／JWT合同证据，真正时间到期、语音中途断网及完整压力矩阵尚未单独实测。不要把开发候选当作已完成全部验收的通用首刷版。原厂工具源码和原有稳定版保留在上一版交付目录；本次普通升级只写 Ekko 应用分区。

保留所有原项目、字体、头像、音频解码库和依赖的许可证与来源说明。字体素材见 `assets/fusion-pixel/`，头像及视频来源见各自 assets 目录。
