pillager_browser BOF（对齐 Pillager 浏览器全流程的主要能力）
========================================
已实现：
- Chromium 系：与 Chrome.cs 的 browserOnChromium 列表一致（另含 Edge Beta/Dev/Canary）；Local State → DPAPI → AES-GCM(v10/v11)；
  各 profile：Login Data、Network\Cookies 或 Cookies、History/360History、Bookmarks/360Bookmarks；**Guest Profile**；**扩展清单**（Extensions/*/版本/manifest.json 的 name）→ *_extension.txt。
- Firefox：Profiles 目录下各 profile 的 cookies / 历史 / 书签 / 口令（key4+logins，无主密码场景）。
- **SYSTEM 多会话**：与 Program.cs 类似，枚举 **explorer.exe**，**DuplicateTokenEx + ImpersonateLoggedOnUser**，用 **ExpandEnvironmentStringsForUser** 取各用户 %LOCALAPPDATA%/%APPDATA%；对每个会话跑 Chromium/Firefox/IE/OldSogou。下载文件名加 **<用户标签>_** 前缀以免覆盖。
- **IE 类**：Credential Manager **Vault**（vaultcli，Win8+ 布局）、注册表 **TypedURLs**、**收藏夹** *.url（递归子目录）→ IE_dump.txt。
- **旧版搜狗**：%APPDATA%\SogouExplorer\Webkit（与 OldSogou.cs）：Local State 主密钥、Default\Cookies、HistoryUrl3.db(UserRankUrl)；另上传 FormData3.dat、favorite3.dat（若存在）。**未**递归打包 Local Storage 目录（体积极大）。

未编入或弱于原版：
- Firefox **主密码**（Pillager 的 masterPassword 非空分支）。
- Pillager 对 Chromium 各 profile 的 **Local Storage / Extension Settings** 整目录拷贝。
- **ICommandOnce** 等非浏览器模块。
- 锁定 DB 的 **Raw 读**（本 BOF 的 CreateFile 已带 FILE_SHARE_READ|WRITE|DELETE，与多数场景兼容，但不等同 Pillager 的 LockedFile 后备路径）。

编译：在 pillager_bridge 目录 make（或 make -C src_bof/pillager_browser），需 mingw-w64，生成 pillager_browser.x64.o。
参数：Beacon pack 首个 int 为掩码（无参数或全 F=全开）：
  bit0 (0x1) = Chromium 系
  bit1 (0x2) = Firefox
  bit2 (0x4) = IE / Vault / 收藏夹
  bit3 (0x8) = OldSogouExplorer

注意：SQLite 解析仅覆盖常见库；Vault 结构随系统版本可能有差异，异常版本上可能需调整。
异步 BOF 下 AxDownloadMemory 需使用已修复 filename 打包的 beacon（见主工程 beacon_functions 异步路径）。
