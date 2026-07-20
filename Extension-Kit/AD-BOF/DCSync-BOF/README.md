# DCSync BOF

A Beacon Object File (BOF) implementation of the DCSync attack for extracting credential material from Active Directory domain controllers.

### Features

- Single User Targeting - Extract credentials for a specific domain user
- Bulk Extraction - Dump all users from a domain or organizational unit
- Flexible LDAP Options - Support for custom DCs, OUs, and LDAPS connections

## Usage

### Command Reference

| Command   | Description                               | Usage                                                        |
|-----------|-------------------------------------------|--------------------------------------------------------------|
| `single`  | Extract credentials for a specific user   | `dcsync single <target> [-ou <path>] [-dc <fqdn>] [--ldaps]` |
| `all`     | Extract credentials for all domain users  | `dcsync all [-ou <path>] [-dc <fqdn>] [--ldaps]`             |

### Examples

Extract credentials for a single user:
```sh
dcsync single jane.doe
```

Search for target user in a specific OU using LDAPS:
```sh
dcsync single john.smith -ou "OU=Admins,DC=corp,DC=local" --ldaps
```

Dump all users from the domain:
```sh
dcsync all
```

Target a specific domain controller:
```sh
dcsync all -dc dc01.corp.local
```

## Credits

This project builds upon research and code from:
- [DCSyncer](https://github.com/notsoshant/DCSyncer)
- [DCsyncer Write-up](https://www.notsoshant.io/tools/dcsyncer/)
- [Mimikatz](https://github.com/gentilkiwi/mimikatz)

## Authors Note

Mimikatz and similar programs parse supplemental credentials for offsets, then map to a struct to extract AES keys. This project identifies the salt then uses heuristic detection to locate and extract the AES keys. There will always be edge cases where this approach won't work, however I have yet to encounter a case like that in testing. I have also run into situations where accounts didn't actually have any supplemental credentials, so if you do encounter such an edge case I ask that you test with a different tool, such as impacket's secretsdump, before creating an issue. At the very least it should always return the nthash of the target user.

## Disclaimer

For Authorized Use Only - Unauthorized access to computer systems is illegal. This tool should only be used in environments where you have explicit permission to conduct security testing.
