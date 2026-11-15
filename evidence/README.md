> 本目录原有文件为旧版历史证据，旧记录不可替代新版实机验收。

# 证据阅读顺序

当前 v10 alpha.3 看 `v10-alpha3-20261115/verification.json`、根 `PROGRESS.zh-CN.md` 和 `firmware-manifest.json`。`v10-alpha2-20261115/verification.json`保存alpha2用户确认与测量边界；`v10-rewrite-20261115/verification.json` 是 alpha.1 的过程证据。

`acceptance-summary.json`、`production-0737-text-user-confirmed.json`、原验收范围及以下脱敏说明均为 v9 历史结果，不代表 v10 的验收或新包的完整扫描结论。

其余来源／组装审查是当时的快照，可能写着本地候选、未压缩／未发布或旧扫描数量；它们不覆盖当前公开预览结果。旧来源清单保留原哈希，公开全包实际字节以根SHA256SUMS.txt为准。

PEM头复核：当前固件共有四种立即NUL终止的私钥头解析常量，旧有限模式只计到三种，遗漏ENCRYPTED PRIVATE KEY。未发现随这些标记携带的PEM正文；不将解析常量当作用户私钥。三条Users构建路径来自预编译libc，非当前本地用户／项目路径。当前精确凭据值和形状扫描另外记录，不宣称有限扫描证明所有可能秘密均不存在。
