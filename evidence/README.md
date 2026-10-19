# 证据阅读顺序

当前状态看 `acceptance-summary.json`、`production-0737-text-user-confirmed.json`、根验收范围及firmware-manifest。

其余来源／组装审查是当时的快照，可能写着本地候选、未压缩／未发布或旧扫描数量；它们不覆盖当前公开预览结果。旧来源清单保留原哈希，公开全包实际字节以根SHA256SUMS.txt为准。

PEM头复核：当前固件共有四种立即NUL终止的私钥头解析常量，旧有限模式只计到三种，遗漏ENCRYPTED PRIVATE KEY。未发现随这些标记携带的PEM正文；不将解析常量当作用户私钥。三条Users构建路径来自预编译libc，非当前本地用户／项目路径。当前精确凭据值和形状扫描另外记录，不宣称有限扫描证明所有可能秘密均不存在。
