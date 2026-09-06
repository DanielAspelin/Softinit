# softinit

`softinit` is a compact Linux system-initialisation utility written as a single C source file. It collects a generic machine specification and writes every variable into a separate hidden file.

Each output file contains exactly one value followed by a newline. The files are suitable for direct consumption by C, C++, Bash, JavaScript, R, build systems, launchers, and monitoring components.

## Build

```bash
gcc -std=c17 -O2 -Wall -Wextra -Wpedantic softinit.c -o softinit
```

No third-party libraries are required.

## Run

Write the dot files into the current working directory:

```bash
./softinit
```

Write them into an existing directory:

```bash
./softinit /intended/directory
```

The output directory must already exist and must be writable. Supplying more than one argument produces a usage error.

## Output contract

- One variable is stored in one dot file.
- One dot file contains one terminal value.
- Existing managed dot files are overwritten on every run.
- Each new value is written to a temporary file and atomically renamed into place.
- Unrelated files in the output directory are not modified.
- An unavailable textual value is normally written as `unknown`.
- A successfully determined numeric zero is written as `0`.
- Values representing storage or memory sizes use bytes unless the filename states another unit.

Example:

```text
.kernelname
```

contains:

```text
Linux
```

## Generated variables

### System

| File | Value |
| --- | --- |
| `.systemname` | Operating-system display name |
| `.systemid` | Operating-system identifier |
| `.systemversion` | Operating-system version description |
| `.systemversionid` | Operating-system version identifier |
| `.systemcodename` | Distribution version codename |
| `.systemhostname` | Current host name |
| `.systemuptime` | System uptime in seconds |
| `.systemmachineid` | Persistent Linux machine identifier |
| `.systembootid` | Identifier of the current boot session |
| `.systemtimezone` | Configured system timezone |
| `.systempagesizebytes` | Memory page size in bytes |
| `.systemprocessid` | Process ID of the running `softinit` instance |
| `.systemparentprocessid` | Parent process ID of `softinit` |

### Machine

| File | Value |
| --- | --- |
| `.machinehostname` | Machine host name |
| `.machinename` | Hardware product or machine name |
| `.machinevendor` | Hardware manufacturer |
| `.machineversion` | Hardware product version |
| `.machineserial` | Hardware serial number, when exposed |
| `.machinearchitecture` | Machine architecture, such as `x86_64` or `aarch64` |
| `.machinebyteorder` | Native byte order: `little` or `big` |
| `.machinechassistype` | Firmware/DMI chassis type code |

### Kernel

| File | Value |
| --- | --- |
| `.kernelname` | Kernel or operating-system family name |
| `.kernelrelease` | Kernel release |
| `.kernelversion` | Kernel build version |
| `.kernelarchitecture` | Architecture reported by the kernel |
| `.kerneldomainname` | NIS domain name, when configured |

### Filesystem

Filesystem values describe the filesystem containing the selected output directory.

| File | Value |
| --- | --- |
| `.filesystemtype` | Numeric filesystem magic identifier |
| `.filesystemname` | Recognised filesystem name |
| `.filesystemmountpoint` | Output path supplied to `softinit` |
| `.filesystemblocksize` | Filesystem block size in bytes |
| `.filesystemtotalbytes` | Total filesystem capacity |
| `.filesystemavailablebytes` | Bytes available to an unprivileged process |
| `.filesystemfreebytes` | Total free bytes, including reserved capacity |
| `.filesystemusedbytes` | Allocated filesystem capacity |
| `.filesystemtotalinodes` | Total inode count |
| `.filesystemfreeinodes` | Free inode count |
| `.filesystemnamemax` | Maximum filename length |

### Network

Network addressing is taken from the first active, non-loopback interface found by the operating system. A restricted container may legitimately report `unknown`.

| File | Value |
| --- | --- |
| `.networkhostname` | Current host name |
| `.networkinterface` | Selected active interface name |
| `.networkaddress` | First selected IP address |
| `.networkipv4` | IPv4 address of the selected interface |
| `.networkipv6` | IPv6 address of the selected interface |
| `.networkmacaddress` | Hardware/MAC address of the selected interface |
| `.networkdns` | First configured DNS resolver address |

### Memory

| File | Value |
| --- | --- |
| `.memorytotalbytes` | Total physical memory |
| `.memoryavailablebytes` | Linux `MemAvailable` estimate |
| `.memoryfreebytes` | Completely unused physical memory |
| `.memorybuffersbytes` | Kernel buffer memory |
| `.memorycachedbytes` | Cached memory |
| `.memoryswaptotalbytes` | Total swap capacity |
| `.memoryswapfreebytes` | Available swap capacity |

### Processor

| File | Value |
| --- | --- |
| `.processorarchitecture` | Processor architecture |
| `.processormodel` | Processor model description |
| `.processorvendor` | Processor vendor or implementation identifier |
| `.processorfrequencymegahertz` | Reported processor frequency in MHz |
| `.processorbyteorder` | Native processor byte order |
| `.processorcount` | Online logical processor count |
| `.processoronlinecount` | Online logical processor count |
| `.processorconfiguredcount` | Configured logical processor capacity |

## Reading values

Bash:

```bash
kernel_name="$(<.kernelname)"
memory_total="$(<.memorytotalbytes)"
```

C:

```c
FILE *file = fopen(".kernelname", "r");
```

The trailing newline should be removed when an exact scalar string is required.

## Exit status

| Status | Meaning |
| --- | --- |
| `0` | Every managed value was written successfully |
| Non-zero | Initial system inspection or at least one file write failed |

Individual write errors are reported through standard error. Successfully written files remain available even if another value fails.

## Security considerations

The following outputs can identify a host and should not be published without an explicit access policy:

```text
.systemmachineid
.systembootid
.machineserial
.networkmacaddress
.networkipv4
.networkipv6
```

Run `softinit` only in a controlled, writable output directory—particularly when it operates with elevated privileges. The generated files should inherit access controls appropriate to the consuming service.

## Platform scope

`softinit` targets Linux. It reads standard Linux interfaces including `uname`, `sysinfo`, `statfs`, `sysconf`, `/etc/os-release`, `/proc`, `/sys`, and the active network-interface list. Hardware firmware values may be unavailable on virtual machines, containers, ARM systems without DMI, or systems with restricted `/sys` access.
