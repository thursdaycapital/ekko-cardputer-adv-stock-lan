# 官方原版 Studio 集成验证

这些测试工具用于 **LAN 免补丁协议验收**，不修改 Studio 的发布文件。测试中的文字请求会实际调用所选模型，语音往返会实际调用 Studio 已配置的 TTS/STT，可能产生提供方用量。它们不代替 M5 麦克风、扬声器、屏幕、内存和断网恢复验收。

## 1. 启动独立官方服务

`stock_studio_runtime.py` 比较 npm 压缩包中的全部普通文件与解包目录，固定检查版本为 `0.7.36`。目标私有目录必须为空；已经存在的数据绝不覆盖。仅复制明确指定的三个配置文件，不复制生产数据库、账号、设备绑定、历史、云中继身份或自动任务。

```sh
python3 stock_studio_runtime.py \
  --package /absolute/path/to/unmodified/package \
  --tarball /absolute/path/to/ekko-studio-0.7.36.tgz \
  --seed-home /absolute/path/to/reviewed/private/seed \
  --private-dir /absolute/path/to/new/private/stock-test \
  --node /absolute/path/to/node \
  --port 19038 --bridge-port 19039
```

种子目录结构：`hermes/config.yaml`（只保留当前模型提供方）、`webui/config.json`（模型显示配置）、`webui/.ekko/config/config.json`（测试设置，关闭工具、MCP、skills、memory、后台委派及压缩）。该目录含凭据，必须私有保存。服务器从其新工作目录运行；Coding Agent 全局配置和 Hermes home 都隔离，Agent 安装情况、授权状态可能与日常 Studio 不同。

官方空数据库只接受发布文档中的默认账号完成初始化。工具立即把新实例密码改为随机值，保存在 `runtime.private.json`；不要公开该文件。服务监听局域网以供 M5 测试。结束后核对该清单的 PID、Node 和入口，**只停止这个测试进程**，不要批量结束 Node 或日常 Studio。

## 2. 运行真实协议检查

使用一个已安装 `socket.io-client` 的 Node 项目作为 `--modules`。报告只记录状态、事件名、计时、安装结果和音频大小，不保存 JWT、密钥、原始模型响应或完整聊天内容。

```sh
node stock_studio_validation.mjs \
  --manifest /absolute/path/to/private/stock-test/runtime.private.json \
  --modules /absolute/path/to/node/project \
  --report /absolute/path/to/private/stock-test/validation.private.json \
  --run-agents ekko-agent,claude-code,codex \
  --voice yes
```

可选 `--voice-seed /private/selected-voice-settings.json` 通过原生设置 API 导入所选语音配置；结构为 `tts`、`stt`，各自包含 `provider`、`settings`、`secrets`、`activeProvider`。只在测试实例上操作，不直接写 SQLite。默认语音测试依赖 `ffmpeg`，可用 `--ffmpeg /absolute/path/to/ffmpeg` 指定。

检查内容：MCU LAN 登录签发普通用户 JWT、`auth/me`、16 路规范 ID 和 14 个 Coding Agent 安装目录、原生设备注册、同一 Engine.IO 的 `/chat-run` 和 `/global-agent`、管理页局域网在线/离线、实际 Agent 回复及原生分页历史、TTS→STT 音频往返。没有发送自定义 MCU Agent 选择/确认/聊天扩展事件。

STT 使用与固件一致的 `/api/studio/voice/proxy/{profile}/v1/audio/transcriptions?profile={profile}`，multipart 携带 `file` 与 `response_format=text`，由 Studio 选择当前提供方。普通 `/api/studio/stt/transcribe` 要求显式 `provider` 字段，不能直接省略。路径、query 和 header 的 Profile 必须一致。

报告中的“已安装”仅代表 CLI 存在；只有 `executionVerified=true` 才代表本次真正完成调用。某个 Agent 失败不会切换成其他 Agent。`physicalDeviceTest=false` 与 `remoteTest=false` 必须保留，直到分别取得实机和远程证据。

可用 `--run-timeout 60000` 将单次执行限制为一分钟；超时后只对本次测试会话发送官方 `abort`，不停止服务器或其他会话。需要诊断时可指定 `--private-errors /private/errors.json` 保存原始错误，文件权限为 0600，禁止加入发布包。公开结果只保留错误类别。

当前测试的 scoped 路由显式使用 Studio 选定的模型；Cursor、Qoder 按官方能力使用 global 模式和原生配置，不把 Studio 的 provider 强加给它们。模型目录的 `auth_required` 与真实执行结果必须分别记录：隔离全局目录与 CLI 实际运行目录可能不同，不能把一次执行成功当作目录探测成功，也不能为通过测试而跳过固件的授权提示。

## 3. 实机鉴权恢复

`physical_auth_recovery.py` 默认只预检：核对原版入口哈希、独立私有目录、PID、端口、原 JWT 与 M5 双通道及原生在线状态，不重启、不登录、不发模型或语音请求。生产端口 `8748` 被硬性拒绝。此入口只允许已审阅的独立 `19038` 实例，不能用于日常 Studio。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 physical_auth_recovery.py \
  --manifest /absolute/private/stock-test/runtime.private.json \
  --device-state /absolute/private/m5-health.private.json \
  --nvs-values /absolute/private/nvs-values.private.json \
  --report /absolute/work/cardputer-stock-lan-20261018/evidence/physical-auth-preflight.json
```

人工复核预检后加 `--execute` 才会临时更换独立进程的 JWT 签名密钥，验证旧 JWT 返回 `401`、M5 自动恢复，再在 `finally` 恢复原环境，核对持久令牌未变和默认 30 天有效期，并更新私有清单。子进程显式回收，外部僵尸进程按已退出处理，官方启动预算为 180 秒。恢复正常环境先于审计断言；审计缺失不能阻止正常环境启动。中断后可使用相同参数加 `--restore-only` 从 0600 私有 journal 恢复，禁止公开 journal 或运行清单。

能力预检读取实际 npm 入口，不能用另一份源码中的日志语句代替发布包合同。已核对的官方 `0.7.36` npm 入口没有 `[mcu-login] completed` 与 `[mcu-login] phase completed`，因此报告为 `nativeLoginAudit=not_available`，请求次数为 `null`，不能把缺日志写成零次登录或固件失败。

该固定哈希版本的普通 `/chat-run` 与 `/api/auth/me` 共用 JWT 签名和过期校验。无日志时采用明确标注的推断：受控更换签名后旧 JWT `401`，同一 M5 在新进程上建立真实 TCP、重新收到两个 namespace ACK、原生 presence 在线、绑定匹配，设备 uptime 连续且 Agent 不变；测试器不调用 MCU 登录，操作期间需排除人工网页登录或其他测试器的 MCU 请求。`/global-agent` 另有每进程随机 agent token 分支，因此单独 presence 在线不足以证明新用户 JWT。设备 JWT 内容和实际登录次数均未读取。

本入口尚未实现或执行真实 `45` 秒过期后新握手阶段。空闲 WebSocket 不一定在 JWT 到期时断开；签名失效恢复通过也不能替代时间到期后的重新握手验收，更不能替代模型回答或实际扬声器听感。离线安全回归：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v test_physical_auth_recovery.py
```
