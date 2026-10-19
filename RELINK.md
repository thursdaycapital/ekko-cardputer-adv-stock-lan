# 构建、修改和重新链接

本发布只分发 Ekko `cardputer-adv-v9.0.0-dev.10-ram-lan` 应用；公开预览标签为 `v9.0.0-dev.10-lan`，二者对应同一个已实测应用，不是两份不同固件。原厂应用本次没有重新分发。

## 原配置构建

在仓库根目录执行：

```sh
uvx --from platformio pio run -d source -e cardputer-adv
```

固定平台为 espressif32@7.0.1；Arduino 框架 `3.20017.241212+sha.dcc1105b`（Arduino2.0.17），GCC8.4.0+2021r2-patch5、SDK4.4.7。项目库精确版本见 `source/platformio.ini`。首次构建会下载固定依赖及工具链，不是完全离线的工具链包。源码、生成头、字体／头像／视频数据和构建脚本均保留。

## Arduino LGPL 对应源码与替换

`source/arduino-esp32-2.0.17/` 保存实际固定框架的 cores、libraries、variants、PlatformIO脚本及package元数据，来自此前公开交付的同版本树。1,022文件中1,020与本次安装的固定框架逐字节相同；两项WiFiClient示例文档／示例代码中的演示Key已在此前公开树中改为EXAMPLE_*占位符，未参与本应用编译或链接。本次2026-10-19核对并保留该去演示Key处理，未从本机示例回拷。实际核心、库运行实现、板型定义和构建脚本对应，未用这些示例差异修改本次固件。

可在**独立副本**中取得完整固定框架包，保留其SDK及工具，用本目录对应源码修改所需Arduino核心／库，再在自己的项目副本中通过PlatformIO `platform_packages`指定本地框架路径。不要覆盖其他项目共享的框架安装。官方本地包说明：https://docs.platformio.org/en/latest/projectconf/sections/env/options/platform/platform_packages.html

完整应用源码与构建配置均提供；用修改后的库进行干净重建，即可生成可替换执行文件。构建产物／对象／ELF／map位于 `source/.pio/build/cardputer-adv/`，公共源包不包含开发者原有`.pio`、私人配置或完整Flash读出。本项目没有增加禁止修改LGPL部分、替换／重新链接它们或为调试这些修改而逆向工程的限制。

```sh
uvx --from platformio pio run -d source -e cardputer-adv -t clean
uvx --from platformio pio run -d source -e cardputer-adv
```

SDK和工具链原声明位于 `source/licenses/`，各库及素材的原文保留，不用根BSL许可覆盖LGPL、MIT、Apache、字体或其他第三方条款。准确材料与比对范围见 `evidence/arduino-source-correspondence.json`。不同路径、日期及工具环境可能改变重建字节，不承诺字节级重现。

## 验证与安装修改版

按 `source/README.zh-CN.md` 运行主机检查，并在真实设备验证。仅为既有本项目8MiB双固件布局安装修改应用，目标为 `0x10000`、容量 `0x330000`。修改后必须先核尺寸；不要写到 `0x0`、擦整片或覆盖原厂分区。详见根安装教程。此源码不构成陌生设备首刷镜像。
