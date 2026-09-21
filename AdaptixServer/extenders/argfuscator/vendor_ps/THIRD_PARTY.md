# 第三方代码来源

本目录下的 PowerShell 代码**不是**本项目写的，原样取自上游，仅供 `argfuscator`
服务插件在服务端调用。两个上游都是 **GPL-3.0**（各自附有 `LICENSE`）。

| 上游 | 用途 | 钉住的 commit | 取用方式 |
|---|---|---|---|
| [wietze/Invoke-ArgFuscator](https://github.com/wietze/Invoke-ArgFuscator) | 混淆引擎本体 | `dfafed8163c55bbd601ee3e60e6d5d7f25dfa990`（2026-02-13） | `Invoke-ArgFuscator.psm1` `.psd1`、`Modifiers/`(10)、`Types/`(4)、`LICENSE` |
| [wietze/ArgFuscator.net](https://github.com/wietze/ArgFuscator.net) | 156 个命令的参数模型 | `aeca415b04f40246232bb3c37798f54664c5c940`（2026-02-19） | `models/`(156 个 `.json`)、`LICENSE` |

## 为什么 models/ 来自第二个仓库

上游 README 把这句写在脚注里，很容易漏：`-Command` 这条路径要先 tokenise，
再拿第一个词去拼 `models/<cmd>.json`，**文件不存在就直接报 command not found**。

> *Note that this requires the [models/](https://github.com/wietze/ArgFuscator.net/tree/main/models)
> folder to be present in the same folder as `Invoke-ArgFuscator.ps1`.*

而 `wietze/Invoke-ArgFuscator` 这个仓库里**没有** `models/` 目录（`git ls-files`
可证）。少了它，插件能加载、`/status` 也能报出 pwsh 版本，但每一次生成都会失败。
两个仓库都是 GPL-3.0，份量一致。

## 本项目自己写的部分

只有 `argfuscate.ps1` 是手写的（包装层，只从环境变量读参数并显式拼装变体分隔符）。
`models/`、`Modifiers/`、`Types/`、`*.psm1`、`*.psd1` 全部原样搬运，未作修改。

## 许可

GPL-3.0。**是否构成派生作品属于法务判断，这里不代为结论** —— 本文件的作用只是
把「用了谁的代码、用的哪一版」写清楚，而不是静默 vendor。

重新获取（在有网机器上）：

```sh
git clone --depth 1 https://github.com/wietze/Invoke-ArgFuscator /tmp/argf
git -C /tmp/argf rev-parse HEAD   # 应等于上表第一个 commit

git clone --depth 1 https://github.com/wietze/ArgFuscator.net /tmp/argnet
git -C /tmp/argnet rev-parse HEAD # 应等于上表第二个 commit

cp /tmp/argf/Invoke-ArgFuscator.psm1 /tmp/argf/Invoke-ArgFuscator.psd1 /tmp/argf/LICENSE .
cp -r /tmp/argf/Modifiers /tmp/argf/Types .
cp -r /tmp/argnet/models .
cp /tmp/argnet/LICENSE LICENSE.ArgFuscator.net
```
