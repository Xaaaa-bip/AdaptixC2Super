# SAR-BOF

This extension enhances situational awareness by providing a set of remote Beacon Object File (BOF) commands. These commands allow the operator to gather detailed information about the target.

![](_img/01.png)



## nbtscan

NetBIOS name scanner that queries NetBIOS name service (port 137) to discover NetBIOS names, MAC addresses, and service information from Windows hosts on the network. Automatically registers discovered targets in AdaptixC2.

```
nbtscan <target> [-v] [-q] [-e] [-l] [-s <separator>] [-t <timeout>] [-no-targets]
```

- `target` (required): Destination IP address, range or CIDR format
    - Single IP: `192.168.1.1`
    - IP range: `192.168.1.1-192.168.1.20` or `192.168.1.1-20`
    - CIDR: `192.168.1.0/24`
    - Comma-separated: `192.168.1.1,192.168.1.5,192.168.1.10`
- `-v` (optional): Verbose output - shows detailed NetBIOS information including service types
- `-q` (optional): Quiet mode - suppresses error messages
- `-e` (optional): Output in `/etc/hosts` format
- `-l` (optional): Output in `lmhosts` format
- `-s <separator>` (optional): Script-friendly output with custom separator (enables script mode)
- `-t <timeout>` (optional): Response timeout in milliseconds (default: 1000, max: 600000)
- `-no-targets` (optional): Disable automatic target registration in Adaptix

**Output Formats:**

1. **Normal mode** (default): Shows IP address, NetBIOS name, and MAC address
2. **Verbose mode** (`-v`): Shows detailed information including:
    - NetBIOS names with service types (00, 03, 20, etc.)
    - MAC address
    - Domain/workgroup information
3. **Script mode** (`-s <separator>`): Machine-readable output with custom separator
4. **Hosts format** (`-e` or `-l`): Output suitable for `/etc/hosts` or `lmhosts` files

**NetBIOS Service Types:**

| Code | Service Type |
|------|--------------|
| `00` | Workstation Service |
| `03` | Messenger Service |
| `20` | File Server Service |
| `1B` | Domain Master Browser |
| `1C` | Domain Controller |
| `1D` | Master Browser |
| `1E` | Browser Service Elections |

```Shell
# Basic scan of a single host
nbtscan 192.168.1.1

# Scan a subnet
nbtscan 192.168.1.0/24

# Scan an IP range
nbtscan 192.168.1.1-192.168.1.20

# Verbose output with detailed information
nbtscan 192.168.1.0/24 -v

# Quiet mode (suppress errors)
nbtscan 192.168.1.0/24 -q

# Output in /etc/hosts format
nbtscan 192.168.1.0/24 -e

# Output in lmhosts format
nbtscan 192.168.1.0/24 -l

# Script-friendly output with custom separator
nbtscan 192.168.1.0/24 -s "|"

# Custom timeout (2 seconds)
nbtscan 192.168.1.0/24 -t 2000

# Scan without auto-registering targets
nbtscan 192.168.1.0/24 -no-targets

# Combined: verbose scan with custom timeout
nbtscan 192.168.1.0/24 -v -t 3000
```

**Target Registration:**

By default, nbtscan automatically registers discovered hosts in Adaptix with the following information:
- Computer name (from NetBIOS name)
- Domain/workgroup (if available)
- IP address
- OS information (if detected)
- MAC address

Use `-no-targets` flag to disable automatic registration if you only want to view the scan results without adding them to Adaptix targets.



## smartscan

Single-threaded silent port scanner

```
smartscan <targets> [-p mode/port_list]
```

**Modes**
1. fast
* Web services: 80, 443, 8080, 8443
* Databases: 1433, 1521, 3306, 5432, 6379, 27017
2. standart
* Web Services: 80, 443, 8080, 8443
* Databases: 1433, 1521, 3306, 5432, 6379, 27017
* Windows-specific: 135, 139, 445, 3389, 5985, 5986
* Linux-specific: 22
* Infrastructure: 21, 25, 53, 110, 143, 993, 995
3. full
* Web Services: 80, 443, 8080, 8443, 8000, 8888
* Databases: 1433, 1521, 3306, 5432, 6379, 27017, 9200, 9300
* Windows/Domain Controllers: 135, 139, 445, 3389, 5985, 5986, 88, 389, 636, 3268, 3269
* Linux/Unix: 22, 23
* Infrastructure: 21, 25, 53, 69, 110, 111, 143, 993, 995
* Other Services: 7, 9, 13, 19, 37, 79, 113, 119, 1025, 1434, 1604, 1723, 2000, 2001, 2048, 2049, 2100, 3128, 5000, 5060, 5061, 5900, 6000, 6667, 8081, 9000, 10000, 11211

```
smartscan 192.168.1.1 -p full

smartscan 192.168.1.1-192.168.1.10 -p standart

smartscan 192.168.1.1/24 -p 20-25,80-90,443
```



## taskhound

Collect scheduled tasks from remote systems, with options to filter for domain accounts with stored credentials and save XML task definitions for offline analysis. Can also collect DPAPI credential blobs and masterkeys for offline decryption.

```
taskhound <target> [username] [password] [-save <directory>] [-unsaved-creds] [-grab-blobs]
```

- `target` (required): Remote system to collect from (IP or hostname)
- `username` (optional): Username for authentication
- `password` (optional): Password for authentication
- `-save <directory>` (optional): Directory to save XML task files locally
- `-unsaved-creds` (optional): Show tasks without stored credentials (interactive token only)
- `-grab-blobs` (optional): Also collect credential blobs and masterkeys (requires `-save` flag)

```Shell
# Basic usage with current user context
taskhound 192.168.1.100

# Using explicit credentials
taskhound DC01 -u domain\admin -p P@ssw0rd

# Save XML files locally for offline analysis
taskhound 192.168.1.100 -save C:\TaskOutput

# Show all domain tasks including those without stored credentials
taskhound 192.168.1.100 -unsaved-creds

# Combined: save files and show all tasks
taskhound DC01 -u domain\admin -p P@ssw0rd -save C:\Output -unsaved-creds

# Collect tasks + credential blobs and masterkeys for offline decryption
taskhound 192.168.1.100 -save C:\TaskOutput -grab-blobs

# Full collection with credentials
taskhound DC01 -u domain\admin -p P@ssw0rd -save C:\Output -grab-blobs
```

**DPAPI Collection:**

When the `-grab-blobs` flag is used along with `-save`, TaskHound will additionally collect:
- **Credential Blobs**: `C:\Windows\System32\config\systemprofile\AppData\Local\Microsoft\Credentials\*`
- **SYSTEM Masterkeys**: `C:\Windows\System32\Microsoft\Protect\S-1-5-18\User\*`

These files are saved to:
- `<save_dir>\<hostname>\credentials\*` (credential blobs)
- `<save_dir>\<hostname>\masterkeys\*` (masterkeys)

The collected DPAPI files can be used with offline decryption tools to extract stored credentials from scheduled tasks.



## quser

Query user sessions on a remote machine, providing session information.

```
quser [host]
```



## Credits
* TaskHound - https://github.com/1r0BIT/TaskHound
* Quser-BOF - https://github.com/netero1010/Quser-BOF
* NBTscan - https://github.com/shashinma/NBTscan-BOF