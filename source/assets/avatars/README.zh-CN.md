# Agent 原始头像与固件像素

16 项顺序与根目录 `config/agents.json` 一致。`originals/` 保留实际 SVG 或 PNG，
`manifest.json` 逐项记录下载地址或 Ekko 上游路径、SHA-256、出处类型和已知许可边界。
没有把 PNG 包进 SVG，没有描摹、AI 重画或用字母占位。部分品牌自己的正式图标包含字母，
例如 Kimi/ZCode；这是品牌原图，不是固件生成的首字母。

来源类型区分：

- `first_party_original_svg`：产品方网站或仓库直接提供的 SVG。
- `first_party_inline_svg`：官方网页中的原始内联 SVG，原样提取并记录定位方式。
- `ekko_upstream_adopted_brand_asset`：Ekko 官方上游仓库采用的品牌头像，保留原文件。
  这不声称每个 SVG 都由对应品牌直接创作或亲自提供，也不把上游开源代码许可扩展为商标授权。

Pi 和 Cursor 原有 `pi.svg`、`cursor.svg` 是简化占位，这里分别采用 `pi.dev` 与 `cursor.com`
实际提供的矢量图。其他项目优先沿用 Ekko 已采用的真实头像。没有找到可信纯矢量原件时使用 PNG；
Google Antigravity 官网 favicon 虽是 SVG 容器，其内容包含位图，因此没有将它列为纯矢量原件。

以下命令从本独立固件源码根目录（含 `platformio.ini` 和 `config/agents.json` 的
`source/` 目录）运行。头像工具的 `package.json` 和 `package-lock.json` 独立锁定
`sharp 0.35.3`，与 `generated.json` 记录的生成版本一致。安装工具需要访问 npm，
安装完成后的检查和生成只读取本地原图，不访问 Studio 或设备。

```sh
npm ci --prefix assets/avatars --no-audit --no-fund
node assets/avatars/generate.mjs --check
python3 -B assets/avatars/test_assets.py
```

工具要求 Node.js 20.9.0 以上。`--check` 只校验，不写入生成头、像素清单或预览。
资产测试还需 Python 3.9 以上及
支持 AddressSanitizer、UndefinedBehaviorSanitizer 的 C++17 编译器。
日常编译固件直接使用已保存的生成头，不需要安装 npm 依赖。
只有明确要更新原图或转换参数时，才运行下面的生成命令；它会更新生成头、像素清单和预览：

```sh
node assets/avatars/generate.mjs
```

生成器离线读取已保存原图，只裁去全透明的外部留白并等比缩放。44×44 头像的边距为 4 像素，
铺底为 renderer 的暖浅色 `0xEF3A`，不重画品牌几何。输出 `include/AgentAvatarAssets.h`，
RGB565 以常规 `uint16_t` 值保存，无字节交换；16 张总计 61,952 字节，放在 PROGMEM。
`nativeAgentLogo(index)` 返回 `AgentAvatarAsset {pixels,width,height}`，越界返回 `nullptr`。
`generated.json` 保存原图转换后的像素 SHA；`--check` 检查生成头与源图完全一致。
对于已去除图片元数据的 PNG，`manifest.json` 的 `sha256` 校验实际发布文件，
`downloaded_source_sha256` 则保留下载时原文件的来源哈希；生成头的 `original` 注释
沿用后者，像素计算始终使用已保存且校验通过的本地文件。

`previews/` 是同一批缩放像素的 PNG 预览；它们是渲染派生图，不是官方原始 PNG。
编译与离线测试不能代替实体屏幕的颜色、字节顺序和清晰度验收。

品牌名与商标仍归各自所有者。仓库 MIT 等许可只在元数据中按已知范围注明；
未确认单独头像/商标许可的项目明确标为未确认，不声称获得官方背书或普遍再许可。
