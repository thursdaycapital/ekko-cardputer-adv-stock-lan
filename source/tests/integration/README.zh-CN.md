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

`physical_auth_recovery.py` 默认只预检：核对原版入口哈希、独立私有目录、PID、端口、原 JWT、机器身份与 M5 双通道及原生在线状态，不重启、不登录、不发模型或语音请求。生产端口 `8748` 被硬性拒绝。此入口只允许已审阅的独立 `19038` 实例，不能用于日常 Studio。所有 HTTP 均关闭环境代理、禁止跟随重定向；每次 GET 加随机 nonce 和禁缓存头，令牌只放请求头。传输层仅允许固定只读接口及测试器自己的 `/api/auth/login`，禁止测试器调用 `/api/auth/mcu-login`、配网、修改密码或设备设置。

目前已完成审阅的是官方 npm `0.7.36`，SHA-256 `2698df3d144d32b435d8b700dc53c0b79879e9041faed557f883d7a9dac93f0a`，以及从官方 `0.8.24-arm64` DMG 取得的干净 App 入口 `c04bfec6333a0e70d763ba93ec2fff7bf3f7b6241b6b75c4d5c1d57a571084e0`。干净 App 完整严格签名校验通过，8107 个封装文件、16 个链接均匹配；实际提取的生产 JWT 函数通过 30 天默认值、45 秒配置、精确到期拒绝及错误签名拒绝检查，普通聊天鉴权使用同一校验链。注意：本机当前安装的同版服务器实际为 `09bbce3f6ea6c214b120f71f0a51a6acf79e8b38e0fb3cb062530b05d6331e5c`，与签名封装不匹配，仍明确拒绝用于官方原包验收。不能用忽略资源的代码签名通过代替服务文件的完整性校验。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 physical_auth_recovery.py \
  --manifest /absolute/private/stock-test/runtime.private.json \
  --device-state /absolute/private/m5-health.private.json \
  --nvs-values /absolute/private/nvs-values.private.json \
  --report /absolute/work/cardputer-stock-lan-20261018/evidence/physical-auth-preflight.json
```

人工复核预检后加 `--execute --stage time-expiry` 执行真实时间过期（执行默认阶段也是 `time-expiry`）：

1. 先暂用测试专属签名密钥和官方 `HERMES_WEB_UI_AUTH_JWT_EXPIRES_IN=45s`。仅修改 TTL 不会缩短设备已有的 30 天令牌，所以先实证原测试器 JWT `401`，等待 M5 自行原生登录取得短令牌。此项记录为 **short-token preparation**，不算时间过期成功。
2. 在 M5 恢复后签发并验证测试器的 45 秒时间见证 JWT；停止独立服务器，在停止状态等待至少 47 秒，保证设备无法在等待期间提前续期。单调时钟与墙钟都必须确认过期，时钟异常不能报成功。
3. 使用完全相同的测试密钥、TTL、私有目录、工作区和机器身份重新启动独立服务器，强制新 TCP／namespace 握手；旧短 JWT 必须 `401`。等待 M5 自动续期恢复，要求双 namespace、持久绑定、同一 Agent、连续 uptime、原生 presence 与新服务器到 M5 的 TCP 全部匹配。每次重启都重新读取官方 `link-info` 的 `device_id / endpoint_kind / http_port`；变化立即拒绝归因。
4. `finally` 恢复原默认签名环境和 30 天有效期，核对原 JWT 重新有效、持久 `.token` 未变、M5 再次恢复，并更新私有运行清单。恢复成功前整体测试不通过。历史的签名切换恢复单独用 `--execute --stage signature-rotation`，不能替代真实时间过期。

子进程显式回收，外部僵尸进程按已退出处理；停止前验证 PID、出生时间、精确 Node/入口、独立目录和端口，启动后还验证完整环境与所传环境一致。官方单次启动预算为 180 秒，等待 M5 恢复最多 180 秒。恢复正常环境先于审计断言；审计缺失不能阻止正常环境启动。中断后可使用相同参数加 `--restore-only` 从 0600 私有 journal 恢复；journal 包含环境和凭据，不能公开，也不能删除尚未恢复的 journal。

独立 broker 的归属检查同时支持原始 Python/script 参数及已审阅 Hermes `venv_sync.relaunch_command` 的 `Python -I -c` 形态。后者只解析有界 argv 字面量，完整 AST 必须与 `import sys, runpy; sys.path.insert(...); sys.argv=...; runpy.run_path(..., run_name='__main__')` 模板一致；script、19039、Agent Root、私有 Hermes Home 和环境均须匹配。额外语句、其他 endpoint/home/run_name 会被拒绝，不执行被检查的命令文本，也不用 substring 判断归属。

能力预检读取实际入口，不能用另一份源码中的日志语句代替发布包合同。已核对的官方 `0.7.36` npm 和签名匹配的 `0.8.24` 干净入口均没有 `[mcu-login] completed` 与 `[mcu-login] phase completed`，因此报告为 `nativeLoginAudit=not_available`，请求次数为 `null`，不能把缺日志写成零次登录或固件失败。人工网页登录次数也未由工具测量，记录为 `null`，由操作者在测试期间排除人工登录。

已审阅入口的普通 `/chat-run` 与 `/api/auth/me` 使用普通用户 JWT 签名及过期校验。无审计日志时采用明确标注的推断：受控签名失效或完整的停服过期窗口后，旧已验证 JWT `401`，同一 M5 在新进程上建立真实 TCP、重新收到两个 namespace ACK、原生 presence 在线、绑定匹配，设备 uptime 连续且 Agent 不变；测试器不调用 MCU 登录，操作期间需由操作者排除人工网页登录或其他测试器的 MCU 请求。`/global-agent` 另有每进程随机 agent token 分支，因此单独 presence 在线不足以证明新用户 JWT。设备 JWT 内容和实际登录次数均未读取，不能把推断写成已取得设备令牌或精确登录次数。

`stock_studio_runtime.py` 保留 npm `0.7.36` 的 `--tarball` 校验，并新增互斥的 `--signed-app` 模式支持精确干净 `0.8.24`。该模式要求 `--package` 是该 App 的 `Contents/Resources/webui`，`--node` 是同 App 的 `Ekko Studio Helper.app/Contents/MacOS/Ekko Studio Helper`；使用 `ELECTRON_RUN_AS_NODE=1` 复用其 Node 24.15.0 和内置 SQLite 3.51.3，不安装或替换依赖。必须全新空目录；每次启动前完整严格签名和封装项均须通过，不修改官方核心。

App 模式的 `--seed-home` 指向日常用户目录，但只投影选定模型和语音设置：YAML 中 model、fallback/custom providers、model catalog、tts/stt/voice；Studio JSON 中模型列表/标签；Ekko JSON 中 schemaVersion/model。不会复制数据库、账号、会话、设备身份、网关/聊天平台账号、MCP、插件或提示词；新网关自动启动关闭。首次官方默认 admin 登录后立即随机改密，密码仅写入 0600 私有清单。HTTP bootstrap 禁代理和重定向，启动检查加 nonce，不发模型或语音请求。

新私有目录（0700）和清单（0600）中的所需环境：`PORT=19038`、`BIND_HOST=0.0.0.0`，`HERMES_WEB_UI_HOME`／`HERMES_HOME`／`HERMES_CODING_AGENT_GLOBAL_HOME`／`TMPDIR` 分别指向新目录下 `webui`／`hermes`／`coding-global`／`tmp`，`HERMES_AGENT_BRIDGE_ENDPOINT=tcp://127.0.0.1:19039`、`HERMES_AGENT_BRIDGE_WORKER_PORT_BASE=19049`、`HERMES_AGENT_BRIDGE_STOP_ON_SHUTDOWN=1`。基线环境不得设置 `AUTH_JWT_SECRET` 和短 TTL；不继承父进程的 provider/voice secrets 来冒充已导入配置，官方更新检查关闭。工作区必须是新目录下 `workspace`。配置了 bridge 端口不代表桥接进程已经启动或 Agent 可用；认证准备完成后仍需分别验收。

空的隔离配置根不等于本机没有安装 Hermes。若要复用已验证的本机 User CLI，可仅在测试进程中使用官方桌面运行时选择机制：`HERMES_DESKTOP=true`、`HERMES_RUNTIME_SELECTION_LOCKED=true`，以及从实际已验证安装取得的 `HERMES_RUNTIME_SOURCE`、`HERMES_RUNTIME_VERSION`、`HERMES_BIN`、`HERMES_AGENT_ROOT`、`HERMES_AGENT_CLI_PYTHON`／`HERMES_AGENT_BRIDGE_PYTHON`。不能捏造版本或安装状态；所有数据根、端口、workspace 仍指隔离目录。通过官方环境重启后，原生 Agent 注册表和 bridge health 必须分别确认。此操作不安装新 Agent，也不修改日常进程或官方核心。

Native CLI 登录状态与 Studio 的模型配置是两套存储。经操作者明确授权，可只将本机 Codex 的必要 `auth.json` 和模型配置投影放到隔离 `coding-global/.codex`（700/600、拒绝覆盖）；不能复制原 sessions/history/DB，也不带原 hooks、MCP、plugins、projects 或通知配置。Hermes OAuth 的 `hermes/auth.json` 又是另一套存储，默认不导入。官方模型目录可能在首选 provider 不可用时按相同 model 选择其他已配置 provider；必须记录这种隔离配置差异，不能宣称与日常 provider 一致或默默修改日常默认值。

私有 `runtime.private.json` 需包含隔离进程 `pid`、上述 Node/入口/工作区、`baseUrl=http://127.0.0.1:19038`、`port=19038`、`bridgePort=19039`、精确 `version/serverSha256`，以及仅此隔离实例的新账号 `username/password`、官方已验证的 `userJwt` 和 `profile`。M5 先通过原生设备网页登录此隔离实例一次并选 Profile；这是首次绑定准备，不是恢复阶段的重复登录。证据路径仍限制为该工作目录的 `evidence/physical-auth-*.json`。恢复工具只使用已准备的私有运行数据，不负责初始化新账号或覆盖现有数据。

空闲 WebSocket 不一定在 JWT 到期时断开，故时间阶段明确强制新握手。离线安全测试通过不代表已执行实机过期阶段；签名恢复、时间过期恢复也都不能替代模型回答、麦克风采集或实际扬声器听感。离线安全回归：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v test_physical_auth_recovery.py
```
