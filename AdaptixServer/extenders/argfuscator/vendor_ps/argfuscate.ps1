# ── Adaptix argfuscator 包装脚本 ──────────────────────────────────────────
#
# 本项目**唯一手写的** PowerShell 文件，静态随附、永不生成、永不模板化。
#
# 只从环境变量读参数。用户的命令文本经 Go 侧的 cmd.Env 进来，**从不**插值进
# 命令行或本文件正文 —— pwsh -Command "…用户输入…" 会让分号 / 管道 / $() /
# 反引号以 teamserver 权限任意执行。这里一个 param() / $args 都不读也是同一个
# 理由：位置传参时，开头的 '-' 会被绑成开关（尤其 -Interactive 会弹交互提示，
# 把请求挂到超时）。
#
# 输出编码：模块对每个变体调一次 Write-Output，而**一个变体可能是多行的**
# （多行 PowerShell 命令合法）。若按行 split，1 个多行变体就会被读成 2 个变体
# —— 一个不会报错的错误答案。所以这里用 U+001E（记录分隔符）显式拼装：
# 命令行里不可能出现的字符，Go 侧按它切，与变体内部有几个换行无关。
#
# 用 splatting（@params）而不是反引号续行：反引号续行时漏掉一个括号只会得到
# 一句 "Missing closing ')'"，而且 pwsh -File 遇到解析错误时**退出码非 0 却
# 一个字都不输出**。哈希表没有续行，这类错误根本写不出来。
#
# 只读的三个变量：
#   AXF_CMD       要混淆的命令（必填）
#   AXF_PLATFORM  windows | linux | mac
#   AXF_N         生成几个变体
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Invoke-ArgFuscator.psm1') -Force

# 不收 -Profile：在 -Command 这条路径上上游根本不读它（psm1 的 FromCommand
# 分支只用 platform 去挑 profile），传了也只是让读代码的人以为它生效。
$axfArgs = @{
    Command  = $env:AXF_CMD
    Platform = $env:AXF_PLATFORM
    n        = [int]$env:AXF_N
}

$variants = @(Invoke-ArgFuscator @axfArgs)

[Console]::Out.Write(($variants -join [string][char]0x1E))
