# 聊天页表情降级显示

字形审计已将近期消息的缺字定位到 Emoji，未发现这些消息中的汉字缺失。
本模块在显示时将默认 Emoji 显示字符转换为短中文标签，不增加字库、不扩展正文缓存。
常见问候表情使用 `[招手]`、`[微笑]`、`[汗笑]`、`[举手]`、`[敬礼]`、`[勾选]`；
其他 `Emoji_Presentation=Yes` 字符使用 `[表情]`。

数据锁定 [Unicode 17.0.0 emoji-data.txt](https://www.unicode.org/Public/17.0.0/ucd/emoji/emoji-data.txt)，
版本稳定，来源 URL、SHA-256、完整未修改源文件及 Unicode License V3 均保存在此目录。
`generate.py` 离线提取 `Emoji_Presentation`、`Emoji_Modifier` 和 Emoji_Component 中的 tag 子集，
合并连续范围并生成 1,856 字节的只读表。表情组合的 ZWJ 另使用非 ASCII/非肤色 Emoji base 判断。
它没有使用粗略的 `2600..27FF` 整段判断，也没有使用包含
ASCII `#`、`*`、数字的宽泛 `Emoji=Yes` 属性来决定标签。

## API 与保留语义

`include/ChatSymbols.h` 提供纯 C++11、零堆分配接口：

- `nativeChatCodepoint(const char*, size_t)` 解码恰好一个 UTF-8 码点；非法编码返回 U+FFFD。
- `nativeChatSymbolLabel(uint32_t)` 返回只读中文显示标签，普通字、ASCII、数学与音乐符号返回空指针。
- `nativeChatNormalizeSymbols(char*, size_t)` 在 `bytes+1` 的可写显示副本中压缩组合控制，
  返回新字节数并写入 NUL，长度不会增加。不会展开标签，不自行修改服务器/数据库。

会删除 FE0E、FE0F、Emoji_Modifier 肤色、emoji tag 及 keycap 组合标记 U+20E3。
ZWJ 仅在两侧均为 Unicode 非 ASCII/非肤色 Emoji base 时删除，普通中文、Latin、Indic 等
文字的 ZWJ 会保留；含肤色或 VS16 的真实 Emoji 组合也可识别。
保留 base 字符，因此 `1️⃣` 变成数字 `1`，不会丢失数字，`*️⃣` 和 `#️⃣` 同理。
乘除号 `×÷`、运算符 `±∑∫∞⊕`、代码 `*/`、音符 `♪♫`、谱号 `𝄞` 和组合重音不会被删除或误归为表情。
Unicode 默认以 Emoji 显示的重型加减除/等号 U+2795..2797、U+1F7F0 另作数学语义保留。
默认文本样式的 `☀⚠✖` 不会仅因位于符号区而变成表情标签。

范围表使用 inline 函数中的静态只读数组，兼容 GNU++11，跨翻译单元由 ODR 共享；
数据大小及生成头 SHA 见 `manifest.json`。设备直接读取 Flash，模块无大块 RAM 资源。

## 验证

```sh
python3 packages/esp32-s3/cardputer-adv/assets/chat-symbols/generate.py --check
python3 packages/esp32-s3/cardputer-adv/tests/run_chat_symbols_tests.py
```

测试使用官方源文件作为独立 oracle，遍历所有 U+0000..10FFFF 属性并对全部合法 Unicode scalar
验证 UTF-8 解码、原位保留/删除、非增长和边界 canary；C++11 ASan/UBSan、零堆分配和两翻译单元均验。
实际问候 Emoji、肤色、ZWJ、keycap/tag 序列和代码/数学/音乐符号另有回归；
全部 7 种中文标签的每个汉字均经锁定 `efontCN_12` 覆盖表核验。
临时测试数据自动清理；不访问硬件、模型、语音服务或密钥。

## 已知边界

- **P3 / 高置信**：这是逐码点降级策略，不是完整 grapheme cluster 渲染；家庭/旗帜等复合序列可能显示多个标签。
- **P3 / 高置信**：默认文本样式符号只保留原字形；移除 VS16 后不会将它们全部视为 Emoji。
  更广泛的符号字形覆盖仍取决于设备字体。
- **P3 / 高置信**：本模块保留普通文字的 ZWJ，但不提供复杂脚本 shaping 或超出现有字体的字形。
- **P3 / 高置信**：未来 Unicode 新版本表情需更新锁定数据后重新生成；未定义码点不会按巨大区间误判成表情。
- **P3 / 高置信**：调用者须保留原始正文/协议 offset，并仅规范化显示副本；本模块的压缩后字节数不适用原始流偏移。
