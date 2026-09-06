
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static const char *output_directory = ".";

static void copy_text(char *destination, size_t size, const char *source)
{
    size_t length;
    if (size == 0)
        return;
    length = strlen(source);
    if (length >= size)
        length = size - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static int write_value(const char *name, const char *value)
{
    char path[PATH_MAX];
    char temporary[PATH_MAX];
    FILE *file;

    if (snprintf(path, sizeof path, "%s/%s", output_directory, name) >= (int)sizeof path ||
        snprintf(temporary, sizeof temporary, "%s.tmp", path) >= (int)sizeof temporary) {
        errno = ENAMETOOLONG;
        return -1;
    }

    file = fopen(temporary, "w");
    if (!file)
        return -1;

    if (fprintf(file, "%s\n", value ? value : "unknown") < 0 ||
        fflush(file) != 0 || fclose(file) != 0) {
        remove(temporary);
        return -1;
    }

    if (rename(temporary, path) != 0) {
        remove(temporary);
        return -1;
    }

    printf("%-24s %s\n", name, value ? value : "unknown");
    return 0;
}

static int write_number(const char *name, unsigned long long value)
{
    char text[64];
    snprintf(text, sizeof text, "%llu", value);
    return write_value(name, text);
}

static void trim(char *text)
{
    size_t length = strlen(text);
    while (length && (text[length - 1] == '\n' || text[length - 1] == '\r' ||
                      text[length - 1] == ' '))
        text[--length] = '\0';
}

static int read_first_line(const char *path, char *result, size_t size)
{
    FILE *file = fopen(path, "r");
    char line[512];
    if (!file)
        return -1;
    if (!fgets(line, sizeof line, file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    trim(line);
    copy_text(result, size, line);
    return 0;
}

static int colon_value(const char *path, const char *wanted,
                       char *result, size_t size)
{
    FILE *file = fopen(path, "r");
    char line[512];
    size_t wanted_length = strlen(wanted);
    if (!file)
        return -1;
    while (fgets(line, sizeof line, file)) {
        char *value;
        if (strncmp(line, wanted, wanted_length) != 0)
            continue;
        value = strchr(line, ':');
        if (!value)
            continue;
        while (*++value == ' ' || *value == '\t') { }
        trim(value);
        copy_text(result, size, value);
        fclose(file);
        return 0;
    }
    fclose(file);
    return -1;
}

static int directive_value(const char *path, const char *wanted,
                           char *result, size_t size)
{
    FILE *file = fopen(path, "r");
    char line[512];
    size_t wanted_length = strlen(wanted);
    if (!file)
        return -1;
    while (fgets(line, sizeof line, file)) {
        char *value;
        if (strncmp(line, wanted, wanted_length) != 0 ||
            (line[wanted_length] != ' ' && line[wanted_length] != '\t'))
            continue;
        value = line + wanted_length;
        while (*value == ' ' || *value == '\t')
            ++value;
        trim(value);
        copy_text(result, size, value);
        fclose(file);
        return 0;
    }
    fclose(file);
    return -1;
}

static int memory_bytes(const char *wanted, char *result, size_t size)
{
    char value[128];
    char *end;
    unsigned long long kibibytes;
    if (colon_value("/proc/meminfo", wanted, value, sizeof value) != 0)
        return -1;
    errno = 0;
    kibibytes = strtoull(value, &end, 10);
    if (errno != 0 || end == value)
        return -1;
    snprintf(result, size, "%llu", kibibytes * 1024ULL);
    return 0;
}

static const char *filesystem_name(long type)
{
    switch ((unsigned long)type) {
        case 0xEF53UL: return "ext2/ext3/ext4";
        case 0x9123683EUL: return "btrfs";
        case 0x58465342UL: return "xfs";
        case 0x01021994UL: return "tmpfs";
        case 0x794C7630UL: return "overlay";
        case 0x6969UL: return "nfs";
        case 0xFF534D42UL: return "cifs";
        case 0x4D44UL: return "fat";
        case 0x5346544EUL: return "ntfs";
        case 0x2FC12FC1UL: return "zfs";
        default: return "unknown";
    }
}

static void network_addresses(const char *wanted_interface,
                              char *ipv4, size_t ipv4_size,
                              char *ipv6, size_t ipv6_size)
{
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *item;
    if (getifaddrs(&interfaces) != 0)
        return;
    for (item = interfaces; item; item = item->ifa_next) {
        void *source = NULL;
        if (!item->ifa_addr || strcmp(item->ifa_name, wanted_interface) != 0)
            continue;
        if (item->ifa_addr->sa_family == AF_INET) {
            source = &((struct sockaddr_in *)item->ifa_addr)->sin_addr;
            (void)inet_ntop(AF_INET, source, ipv4, (socklen_t)ipv4_size);
        } else if (item->ifa_addr->sa_family == AF_INET6 &&
                   strcmp(ipv6, "unknown") == 0) {
            source = &((struct sockaddr_in6 *)item->ifa_addr)->sin6_addr;
            (void)inet_ntop(AF_INET6, source, ipv6, (socklen_t)ipv6_size);
        }
    }
    freeifaddrs(interfaces);
}

static int os_release_value(const char *wanted, char *result, size_t size)
{
    FILE *file = fopen("/etc/os-release", "r");
    char line[512];
    size_t wanted_length = strlen(wanted);

    if (!file)
        return -1;

    while (fgets(line, sizeof line, file)) {
        char *value;
        size_t length;

        if (strncmp(line, wanted, wanted_length) != 0 || line[wanted_length] != '=')
            continue;

        value = line + wanted_length + 1;
        trim(value);
        length = strlen(value);
        if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
            value[length - 1] = '\0';
            ++value;
        }

        copy_text(result, size, value);
        fclose(file);
        return 0;
    }

    fclose(file);
    return -1;
}

static int processor_model(char *result, size_t size)
{
    FILE *file = fopen("/proc/cpuinfo", "r");
    char line[512];

    if (!file)
        return -1;

    while (fgets(line, sizeof line, file)) {
        char *separator;
        if (strncmp(line, "model name", 10) != 0 &&
            strncmp(line, "Hardware", 8) != 0 &&
            strncmp(line, "Processor", 9) != 0)
            continue;

        separator = strchr(line, ':');
        if (!separator)
            continue;
        while (*++separator == ' ' || *separator == '\t') { }
        trim(separator);
        copy_text(result, size, separator);
        fclose(file);
        return 0;
    }

    fclose(file);
    return -1;
}

static int network_values(char *interface, size_t interface_size,
                          char *address, size_t address_size)
{
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *item;

    if (getifaddrs(&interfaces) != 0)
        return -1;

    for (item = interfaces; item; item = item->ifa_next) {
        void *source;

        if (!item->ifa_addr || !(item->ifa_flags & IFF_UP) ||
            (item->ifa_flags & IFF_LOOPBACK))
            continue;

        if (item->ifa_addr->sa_family == AF_INET)
            source = &((struct sockaddr_in *)item->ifa_addr)->sin_addr;
        else if (item->ifa_addr->sa_family == AF_INET6)
            source = &((struct sockaddr_in6 *)item->ifa_addr)->sin6_addr;
        else
            continue;

        copy_text(interface, interface_size, item->ifa_name);
        if (!inet_ntop(item->ifa_addr->sa_family, source, address,
                       (socklen_t)address_size))
            continue;

        freeifaddrs(interfaces);
        return 0;
    }

    freeifaddrs(interfaces);
    return -1;
}

int main(int argc, char **argv)
{
    struct utsname kernel;
    struct sysinfo system_information;
    struct statfs filesystem;
    char system_name[256] = "unknown";
    char system_id[128] = "unknown";
    char system_version[256] = "unknown";
    char system_version_id[128] = "unknown";
    char system_codename[128] = "unknown";
    char processor[256] = "unknown";
    char processor_vendor[256] = "unknown";
    char processor_frequency[128] = "unknown";
    char hostname[256] = "unknown";
    char domainname[256] = "unknown";
    char interface[IF_NAMESIZE] = "unknown";
    char address[INET6_ADDRSTRLEN] = "unknown";
    char ipv4[INET_ADDRSTRLEN] = "unknown";
    char ipv6[INET6_ADDRSTRLEN] = "unknown";
    char mac[128] = "unknown";
    char dns[128] = "unknown";
    char machine_id[256] = "unknown";
    char boot_id[256] = "unknown";
    char machine_vendor[256] = "unknown";
    char machine_name[256] = "unknown";
    char machine_version[256] = "unknown";
    char machine_serial[256] = "unknown";
    char chassis_type[128] = "unknown";
    char timezone[128] = "unknown";
    char mem_total[128] = "unknown";
    char mem_available[128] = "unknown";
    char mem_free[128] = "unknown";
    char mem_buffers[128] = "unknown";
    char mem_cached[128] = "unknown";
    char swap_total[128] = "unknown";
    char swap_free[128] = "unknown";
    char network_path[PATH_MAX];
    char filesystem_type[32];
    char byte_order[16];
    long processor_count;
    long configured_processor_count;
    long page_size;
    int failures = 0;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [output-directory]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2)
        output_directory = argv[1];

    if (uname(&kernel) != 0 || sysinfo(&system_information) != 0 ||
        statfs(output_directory, &filesystem) != 0) {
        perror("softinit");
        return EXIT_FAILURE;
    }

    (void)os_release_value("PRETTY_NAME", system_name, sizeof system_name);
    (void)os_release_value("ID", system_id, sizeof system_id);
    (void)os_release_value("VERSION", system_version, sizeof system_version);
    (void)os_release_value("VERSION_ID", system_version_id, sizeof system_version_id);
    (void)os_release_value("VERSION_CODENAME", system_codename, sizeof system_codename);
    (void)processor_model(processor, sizeof processor);
    (void)colon_value("/proc/cpuinfo", "vendor_id", processor_vendor,
                      sizeof processor_vendor);
    if (strcmp(processor_vendor, "unknown") == 0)
        (void)colon_value("/proc/cpuinfo", "CPU implementer", processor_vendor,
                          sizeof processor_vendor);
    (void)colon_value("/proc/cpuinfo", "cpu MHz", processor_frequency,
                      sizeof processor_frequency);
    (void)gethostname(hostname, sizeof hostname);
    hostname[sizeof hostname - 1] = '\0';
    if (getdomainname(domainname, sizeof domainname) != 0)
        copy_text(domainname, sizeof domainname, "unknown");
    domainname[sizeof domainname - 1] = '\0';
    (void)network_values(interface, sizeof interface, address, sizeof address);
    network_addresses(interface, ipv4, sizeof ipv4, ipv6, sizeof ipv6);
    if (snprintf(network_path, sizeof network_path,
                 "/sys/class/net/%s/address", interface) < (int)sizeof network_path)
        (void)read_first_line(network_path, mac, sizeof mac);
    (void)directive_value("/etc/resolv.conf", "nameserver", dns, sizeof dns);

    (void)read_first_line("/etc/machine-id", machine_id, sizeof machine_id);
    (void)read_first_line("/proc/sys/kernel/random/boot_id", boot_id, sizeof boot_id);
    (void)read_first_line("/sys/class/dmi/id/sys_vendor", machine_vendor,
                          sizeof machine_vendor);
    (void)read_first_line("/sys/class/dmi/id/product_name", machine_name,
                          sizeof machine_name);
    (void)read_first_line("/sys/class/dmi/id/product_version", machine_version,
                          sizeof machine_version);
    (void)read_first_line("/sys/class/dmi/id/product_serial", machine_serial,
                          sizeof machine_serial);
    (void)read_first_line("/sys/class/dmi/id/chassis_type", chassis_type,
                          sizeof chassis_type);
    (void)read_first_line("/etc/timezone", timezone, sizeof timezone);

    (void)memory_bytes("MemTotal", mem_total, sizeof mem_total);
    (void)memory_bytes("MemAvailable", mem_available, sizeof mem_available);
    (void)memory_bytes("MemFree", mem_free, sizeof mem_free);
    (void)memory_bytes("Buffers", mem_buffers, sizeof mem_buffers);
    (void)memory_bytes("Cached", mem_cached, sizeof mem_cached);
    (void)memory_bytes("SwapTotal", swap_total, sizeof swap_total);
    (void)memory_bytes("SwapFree", swap_free, sizeof swap_free);

    processor_count = sysconf(_SC_NPROCESSORS_ONLN);
    configured_processor_count = sysconf(_SC_NPROCESSORS_CONF);
    page_size = sysconf(_SC_PAGESIZE);
    snprintf(filesystem_type, sizeof filesystem_type, "0x%lx",
             (unsigned long)filesystem.f_type);
    {
        const unsigned int marker = 1;
        copy_text(byte_order, sizeof byte_order,
                  *(const unsigned char *)&marker ? "little" : "big");
    }

#define WRITE(call) do { if ((call) != 0) { perror("softinit"); ++failures; } } while (0)

    WRITE(write_value(".systemname", system_name));
    WRITE(write_value(".systemid", system_id));
    WRITE(write_value(".systemversion", system_version));
    WRITE(write_value(".systemversionid", system_version_id));
    WRITE(write_value(".systemcodename", system_codename));
    WRITE(write_value(".systemhostname", hostname));
    WRITE(write_number(".systemuptime", (unsigned long long)system_information.uptime));
    WRITE(write_value(".systemmachineid", machine_id));
    WRITE(write_value(".systembootid", boot_id));
    WRITE(write_value(".systemtimezone", timezone));
    WRITE(write_number(".systempagesizebytes",
                       page_size > 0 ? (unsigned long long)page_size : 0));
    WRITE(write_number(".systemprocessid", (unsigned long long)getpid()));
    WRITE(write_number(".systemparentprocessid", (unsigned long long)getppid()));

    WRITE(write_value(".machinehostname", hostname));
    WRITE(write_value(".machinename", machine_name));
    WRITE(write_value(".machinevendor", machine_vendor));
    WRITE(write_value(".machineversion", machine_version));
    WRITE(write_value(".machineserial", machine_serial));
    WRITE(write_value(".machinearchitecture", kernel.machine));
    WRITE(write_value(".machinebyteorder", byte_order));
    WRITE(write_value(".machinechassistype", chassis_type));

    WRITE(write_value(".kernelname", kernel.sysname));
    WRITE(write_value(".kernelrelease", kernel.release));
    WRITE(write_value(".kernelversion", kernel.version));
    WRITE(write_value(".kernelarchitecture", kernel.machine));
    WRITE(write_value(".kerneldomainname", domainname));

    WRITE(write_value(".filesystemtype", filesystem_type));
    WRITE(write_value(".filesystemname", filesystem_name(filesystem.f_type)));
    WRITE(write_value(".filesystemmountpoint", output_directory));
    WRITE(write_number(".filesystemblocksize", (unsigned long long)filesystem.f_bsize));
    WRITE(write_number(".filesystemtotalbytes",
                       (unsigned long long)filesystem.f_blocks * filesystem.f_bsize));
    WRITE(write_number(".filesystemavailablebytes",
                       (unsigned long long)filesystem.f_bavail * filesystem.f_bsize));
    WRITE(write_number(".filesystemfreebytes",
                       (unsigned long long)filesystem.f_bfree * filesystem.f_bsize));
    WRITE(write_number(".filesystemusedbytes",
                       (unsigned long long)(filesystem.f_blocks - filesystem.f_bfree) *
                       filesystem.f_bsize));
    WRITE(write_number(".filesystemtotalinodes", (unsigned long long)filesystem.f_files));
    WRITE(write_number(".filesystemfreeinodes", (unsigned long long)filesystem.f_ffree));
    WRITE(write_number(".filesystemnamemax", (unsigned long long)filesystem.f_namelen));

    WRITE(write_value(".networkhostname", hostname));
    WRITE(write_value(".networkinterface", interface));
    WRITE(write_value(".networkaddress", address));
    WRITE(write_value(".networkipv4", ipv4));
    WRITE(write_value(".networkipv6", ipv6));
    WRITE(write_value(".networkmacaddress", mac));
    WRITE(write_value(".networkdns", dns));

    WRITE(write_value(".memorytotalbytes", mem_total));
    WRITE(write_value(".memoryavailablebytes", mem_available));
    WRITE(write_value(".memoryfreebytes", mem_free));
    WRITE(write_value(".memorybuffersbytes", mem_buffers));
    WRITE(write_value(".memorycachedbytes", mem_cached));
    WRITE(write_value(".memoryswaptotalbytes", swap_total));
    WRITE(write_value(".memoryswapfreebytes", swap_free));

    WRITE(write_value(".processorarchitecture", kernel.machine));
    WRITE(write_value(".processormodel", processor));
    WRITE(write_value(".processorvendor", processor_vendor));
    WRITE(write_value(".processorfrequencymegahertz", processor_frequency));
    WRITE(write_value(".processorbyteorder", byte_order));
    WRITE(write_number(".processorcount",
                       processor_count > 0 ? (unsigned long long)processor_count : 0));
    WRITE(write_number(".processoronlinecount",
                       processor_count > 0 ? (unsigned long long)processor_count : 0));
    WRITE(write_number(".processorconfiguredcount",
                       configured_processor_count > 0 ?
                       (unsigned long long)configured_processor_count : 0));

#undef WRITE

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}