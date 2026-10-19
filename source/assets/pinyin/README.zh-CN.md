# Cardputer-Adv 离线拼音词库

来源为 [Rime 官方袖珍简化字拼音](https://github.com/rime/rime-pinyin-simp)，
锁定提交 `0c6861ef7420ee780270ca6d993d18d4101049d0`，原词库派生自 AOSP PinyinIME，
采用 Apache-2.0。`upstream/` 保留未经修改的完整原始字典、LICENSE、AUTHORS、README，
各文件 SHA-256 见 `sources.json`。修改说明和第三方署名见 `NOTICE`。

当前生成资源含 **6,749 个可显示汉字、7,291 条单字读音、30,752 个原始词组**。
增加实际音节的 `nue/nve`、`lue/lve` 等价拼写后，共 38,072 条候选读音、25,897 个连写键。
所有候选汉字均经锁定的 M5GFX 0.2.33 `efontCN_12` 字体覆盖校验。
原始词库中 9,756 条罕见字读音和 4 个含缺字的词组不会进入设备候选。

三份只读数组共 **778,235 字节**，不超过 760 KiB 的数据预算。
该数字包括每数组末尾编译器 NUL，不含少量链接对齐和查询代码。
数据在 `src/PinyinDictionaryData.cpp` 中只定义一次，ESP32-S3 直接从 Flash DROM 读取；
多个 C++11 翻译单元包含字典头不会各自复制词库。查询无堆分配、无网络请求、无可变大缓存，
一次只复制当前页的 5 个候选。host `NativePinyinPage` 为 296 字节；板端大小以工具链结果为准。

## 接口

```cpp
#include "PinyinDictionary.h"

NativePinyinPage page;
bool valid = NativePinyinDictionary::lookup("nihaozzz", 0, page);
// page.candidates[0].text == "你好"
// consumed_bytes == 5：仅移除原始输入的 nihao，保留 zzz 继续编辑。
```

`lookup(const char* input, size_t page, NativePinyinPage& result)` 中页码从 0 开始。
返回值表示输入格式有效，空输入有效且候选为空。非法输入和超过 63 个原始 UTF-8 字节的
输入返回 false，并清空输出。`total` 是所有匹配候选数，`count` 最多 5，`has_next` 表示后续页。
候选包含 `text[49]`、原始输入字节数 `consumed_bytes`、`syllables` 和 `complete`。

匹配先显示整串拼音，再按最长可提交字词前缀排列；同键按上游真实词频排列，平权按文本和音节稳定排序。
例如 `nihao` 首候选“你好”、`zhongwen` 首候选“中文”。未收录长句可以逐段选词，
每次仅消费已匹配前缀，未完成或未知后缀保留给用户编辑。

支持 ASCII 大小写、`v`、`ü/Ü`、`u:`；`lve/lue`、`nve/nue` 按真实音节等价，
`jv/qv/xv/yv` 归一到 `ju/qu/xu/yu`。
空格和 ASCII 撇号是显式音节边界：`xi'an` 不会匹配单音节“先”；
`nver`、`nv'er` 都可匹配“女儿”，不会跨 `nv + er` 错改拼音。
消费字节数包含匹配词后的分隔符，避免下一段遗留空格或撇号。

## 可重复生成与验证

在仓库根目录运行：

```sh
python3 packages/esp32-s3/cardputer-adv/assets/pinyin/generate.py --check
python3 packages/esp32-s3/cardputer-adv/tests/run_pinyin_dictionary_tests.py
```

生成器只读本地锁定源文件，不下载资源。无 `--check` 时，确定性重建小声明头、唯一数据 cpp 和 manifest；
策略为保留字体可显示的全部单字读音，再按原始词频保留能放入预算的词组前缀，不添加手写测试词。

测试在临时目录运行并自动清理：

- C++11、`-Wall -Wextra -Werror`、ASan/UBSan；原始词库逐键、逐候选、逐顺序对照。
- Unicode 输入边界、音节边界、`ü/v` 等价、分页、极大页码、最长输入、前缀消费、零堆分配。
- 两个翻译单元共同使用唯一字典定义。
- 安装了锁定 PlatformIO 依赖时，从真实 SDK 提取 `U8g2font::getGlyph` 函数，
  对全部 65,536 个 BMP 码点独立核验保存的字体覆盖表。

字体覆盖事实可以独立重建/核验：

```sh
python3 packages/esp32-s3/cardputer-adv/assets/pinyin/font_coverage.py \
  --font packages/esp32-s3/cardputer-adv/.pio/libdeps/cardputer-adv/M5GFX/src/lgfx/Fonts/efont/lgfx_efont_cn.c \
  --check
```

`font_codepoints.txt` 仅包含 Unicode 码点，不复制字体像素；字体源文件、版本与 SHA 见 `font_coverage.json`。

## 已知边界

- **P3 / 高置信**：采用固定词频，没有学习词频、联网联想、全句语言模型或简拼纠错。
  词组最长为上游提供的 4 字，长句按真实词段提交。
- **P3 / 高置信**：输入使用不带声调拼音；数字声调和带声调元音会明确判为非法，`ü/Ü` 除外。
- **P3 / 高置信**：上游数据截至锁定提交，不保证新品牌或罕见专名作为整词收录；可按字或较短词段输入。
- **P3 / 高置信**：更换设备字体时必须同步重建覆盖表；当前过滤只针对已锁定的 `efontCN_12`。
  它保证候选有字形，不改变服务器聊天内容本身的字体覆盖范围。

主任务负责键盘、渲染和实机验收；此模块的测试不连接设备、不录音、不播放声音、不调用模型或语音服务。
