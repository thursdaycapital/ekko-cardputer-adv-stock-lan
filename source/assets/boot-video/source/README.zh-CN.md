# 用户指定 Ekko Studio 开机视频 · Remotion 适配

使用用户提供的 `Ekko-Studio-Opening-16Agents-4s-2K60.mp4` 原片；副本为 `public/source-opening.mp4`，721092字节，SHA256 `7d03587837e13a17145a02b973026614545e3ab8142dc79e107369dba67f12fc`。来源许可由用户提供，本工程不追加第三方品牌授权声明。原片标签为Made with Remotion4.0.515，本次实际运行Remotion4.0.535官方组件和renderer，不是把普通FFmpeg转码称为Remotion。

## 画面与输出

`src/Composition.tsx` 仅一层官方 `@remotion/media` Video，保留全4秒、16:9、原片内容与正常速度，无裁剪、叠字、附加动画或音轨。

- `EkkoOpeningOriginal2K60`：2560×1440，60fps，240帧，4.000秒。H264/yuv420p，CRF32导出146911字节，保留原片分辨率和帧率，严格小于300000字节。
- `EkkoOpeningLCD`：240×135，15fps，60个PNG帧，4秒。此组合只导出PNG，实际LCD的135px高度不用于H264/H265编码，因此有一个精确范围的even-dimensions eslint说明。
- MCU JPEG：全部baseline/non-progressive，quality95，225401字节payload＋244字节uint32偏移＝225645字节，严格小于300000字节。

M5的LCD无法直接解码MP4。本固件嵌入同一原片经Remotion渲染的JPEG序列，MP4作为独立可播放文件交付。LCD上的16头像会随实际240×135尺寸变小；原片画面保持不变。

## 再生成

```sh
npm ci
npm run lint
npm run dev -- --no-open
npm run render:mp4
npm run render:lcd
python3 ../build_assets.py renders/lcd-frames --duration-ms 4000 --fps 15 --quality-max 95 --source-description 'User-provided 4s source via Remotion EkkoOpeningLCD composition' --output ..
```

所有Remotion包由package.json及package-lock.json锁定4.0.535。再生成工具需要Python3/Pillow。MCU数据只从 `BootVideoAssetsData.inc` 编译一次；生成器严格检查帧数/时长/fps、16:9、像素尺寸、opaque输入、baseline格式、SHA、递增偏移和包括偏移表的总预算，不会改变播放时长来过门槛。

此次主机默认下载的Chrome157在Getting composition阶段返回Target closed，无产物。将并发降到1并通过CLI `--browser-executable` 指定已有Chrome Headless Shell152后，两项真实Remotion渲染成功。命令行可按本机Chrome路径追加该flag；源码未硬编码任何机器路径。参考官方诊断：https://www.remotion.dev/docs/target-closed 。不能单独断言崩溃仅由版本或并发造成。

## 已完成的主机验证

- ESLint/TypeScript通过，Studio实际显示原片组合，根任务验证无current_error。
- MP4逐流ffprobe：仅视频，2560×1440、60/1fps、240帧、4.000秒、146911字节；FFmpeg `-v error -xerror` 全4秒解码通过。
- 原片对导出全部240帧SSIM为0.997683；已看0/40/120/180/239帧证明图。
- LCD全部60PNG真实Remotion渲染。全部60JPEG从C++数据独立解析后完整解码，SOF0、尺寸、偏移、逐帧SHA和manifest均通过。
- 不把主机渲染/固件编译当成M5物理开机验收；实际开机与返回首页由根任务单独记录。

## 已发现问题与范围

- P2，高置信：首次默认Chrome157渲染Target closed，已按上述组合成功恢复，失败结果未交付。
- P2，高置信：当前精确锁定的scaffold开发依赖有10项npm high告警，来自 `@remotion/eslint-config-flat` 引入的typescript-eslint/glob/braces/micromatch工具链；`npm audit --omit=dev` 为0。未执行audit fix --force，渲染依赖和M5二进制不含这段开发lint工具链。根记录保留工具问题，不能宣称依赖无漏洞。
- P3，高置信：原片细线和16个头像在240×135屏幕会缩小；保留原内容而不在压缩过程另画画面。
