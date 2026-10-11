/*
 * board_discovery.c -- finds Pico-family boards among the OS's serial
 * ports (SetupAPI on Windows, sysfs on Linux).
 */
#include "config.h"
#include "board_discovery.h"
#include "platform.h"

#ifdef _WIN32
    #include <initguid.h>
    #include <devguid.h>
    #include <setupapi.h>
#endif

#if defined(__GNUC__) && !defined(__clang__)
    /* GCC pessimistically assumes PATH_MAX-sized path components could
     * each be close to PATH_MAX bytes long when concatenated with a
     * short suffix (e.g. "/idVendor"). snprintf() below always truncates
     * safely regardless, so this warning class has no real bug behind
     * it here -- silence it rather than pad every path buffer further. */
    #pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

/* ─── Board discovery filter ─────────────────────────────────────────── */

/* Raspberry Pi Foundation's USB Vendor ID. Every RP2040/RP2350 board that
 * enumerates as a native USB CDC device (Pico SDK stdio_usb / TinyUSB
 * defaults) reports this VID, regardless of the board vendor (Pico,
 * Pico 2, WIZnet EVB-Pico boards, etc.). */
#define RPI_VID 0x2E8A

static const char *NAME_HINTS[] = {"pico", "rp2040", "rp2350", "wiznet", "board cdc"};
#define NAME_HINTS_COUNT (sizeof(NAME_HINTS) / sizeof(NAME_HINTS[0]))

/* ─── Board discovery ─────────────────────────────────────────────────── */

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (haystack[0] == '\0' || needle[0] == '\0') {
        return false;
    }
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p != '\0'; p++) {
        if (STRNCASECMP(p, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_candidate(const port_info_t *port) {
    if (port->vid == RPI_VID) {
        return true;
    }
    for (size_t i = 0; i < NAME_HINTS_COUNT; i++) {
        if (str_contains_ci(port->description, NAME_HINTS[i])
            || str_contains_ci(port->manufacturer, NAME_HINTS[i])
            || str_contains_ci(port->product, NAME_HINTS[i])) {
            return true;
        }
    }
    return false;
}

#ifdef _WIN32

static bool extract_com_port_name(const char *friendly_name, char *out, size_t out_size) {
    const char *open_paren = strrchr(friendly_name, '(');
    const char *close_paren = strrchr(friendly_name, ')');
    if (open_paren == NULL || close_paren == NULL || close_paren <= open_paren) {
        return false;
    }
    size_t name_len = (size_t)(close_paren - open_paren - 1);
    if (name_len == 0 || name_len >= out_size) {
        return false;
    }
    if (strncmp(open_paren + 1, "COM", strlen("COM")) != 0) {
        return false;
    }
    memcpy(out, open_paren + 1, name_len);
    out[name_len] = '\0';
    return true;
}

static void extract_vid_pid(const char *hardware_id, uint16_t *vid_out, uint16_t *pid_out) {
    *vid_out = 0;
    *pid_out = 0;

    const char *vid_marker = strstr(hardware_id, "VID_");
    if (vid_marker != NULL) {
        *vid_out = (uint16_t)strtol(vid_marker + strlen("VID_"), NULL, HEXADECIMAL_BASE);
    }
    const char *pid_marker = strstr(hardware_id, "PID_");
    if (pid_marker != NULL) {
        *pid_out = (uint16_t)strtol(pid_marker + strlen("PID_"), NULL, HEXADECIMAL_BASE);
    }
}

/* Windows fabricates location strings like "6&1a2b3c4d&0&0000" for
 * composite devices that don't expose a true USB serial number. Real
 * serial numbers don't follow this shape (three '&' separators). */
static bool looks_like_generated_location_id(const char *text) {
    int ampersand_count = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '&') {
            ampersand_count++;
        }
    }
    return ampersand_count == GENERATED_LOCATION_ID_AMPERSAND_COUNT;
}

static void extract_serial_number(const char *instance_id, char *out, size_t out_size) {
    out[0] = '\0';
    const char *last_backslash = strrchr(instance_id, '\\');
    if (last_backslash == NULL) {
        return;
    }
    const char *last_segment = last_backslash + 1;
    if (looks_like_generated_location_id(last_segment)) {
        return;
    }
    strncpy(out, last_segment, out_size - 1);
    out[out_size - 1] = '\0';
}

size_t enumerate_all_ports(port_info_t *out_ports, size_t max_ports) {
    size_t count = 0;
    HDEVINFO device_info_set = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (device_info_set == INVALID_HANDLE_VALUE) {
        return 0;
    }

    SP_DEVINFO_DATA device_info_data;
    device_info_data.cbSize = sizeof(device_info_data);

    for (DWORD index = 0; SetupDiEnumDeviceInfo(device_info_set, index, &device_info_data); index++) {
        if (count >= max_ports) {
            break;
        }

        char friendly_name[MAX_DESC_LEN];
        friendly_name[0] = '\0';
        SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_FRIENDLYNAME,
                                           NULL, (PBYTE)friendly_name, sizeof(friendly_name), NULL);

        char com_port_name[COM_PORT_NAME_LEN];
        if (!extract_com_port_name(friendly_name, com_port_name, sizeof(com_port_name))) {
            continue; /* not addressable as a COMx port */
        }

        port_info_t *port = &out_ports[count];
        memset(port, 0, sizeof(*port));
        strncpy(port->device, com_port_name, sizeof(port->device) - 1);
        strncpy(port->description, friendly_name, sizeof(port->description) - 1);
        strncpy(port->product, friendly_name, sizeof(port->product) - 1);

        char hardware_id[MAX_DESC_LEN];
        hardware_id[0] = '\0';
        SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_HARDWAREID,
                                           NULL, (PBYTE)hardware_id, sizeof(hardware_id), NULL);
        extract_vid_pid(hardware_id, &port->vid, &port->pid);

        char manufacturer[MAX_DESC_LEN];
        manufacturer[0] = '\0';
        SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_MFG,
                                           NULL, (PBYTE)manufacturer, sizeof(manufacturer), NULL);
        strncpy(port->manufacturer, manufacturer, sizeof(port->manufacturer) - 1);

        char instance_id[MAX_DESC_LEN];
        if (SetupDiGetDeviceInstanceIdA(device_info_set, &device_info_data,
                                         instance_id, sizeof(instance_id), NULL)) {
            extract_serial_number(instance_id, port->serial_number, sizeof(port->serial_number));
            port->has_serial_number = (port->serial_number[0] != '\0');
        }

        count++;
    }

    SetupDiDestroyDeviceInfoList(device_info_set);
    return count;
}

#else /* POSIX board discovery */

static void read_sysfs_attribute(const char *dir_path, const char *attribute_name,
                                  char *out, size_t out_size) {
    char file_path[PATH_MAX];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, attribute_name);

    out[0] = '\0';
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        return;
    }
    if (fgets(out, (int)out_size, file) != NULL) {
        size_t len = strlen(out);
        while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
            len--;
            out[len] = '\0';
        }
    }
    fclose(file);
}

/* Walks up from the tty's own sysfs device directory (typically the USB
 * interface, e.g. .../1-1:1.0) looking for the parent USB device
 * directory that exposes idVendor/idProduct/serial. */
static bool find_usb_device_dir(const char *tty_sysfs_path, char *usb_dir_out, size_t usb_dir_out_size) {
    char device_link[PATH_MAX];
    snprintf(device_link, sizeof(device_link), "%s/device", tty_sysfs_path);

    char resolved_path[PATH_MAX];
    if (realpath(device_link, resolved_path) == NULL) {
        return false;
    }

    char search_path[PATH_MAX];
    strncpy(search_path, resolved_path, sizeof(search_path) - 1);
    search_path[sizeof(search_path) - 1] = '\0';

    for (int level = 0; level < USB_DIR_SEARCH_MAX_LEVELS; level++) {
        char id_vendor_path[PATH_MAX + SYSFS_HEX_FIELD_LEN];
        snprintf(id_vendor_path, sizeof(id_vendor_path), "%s/idVendor", search_path);

        struct stat stat_buf;
        if (stat(id_vendor_path, &stat_buf) == 0) {
            strncpy(usb_dir_out, search_path, usb_dir_out_size - 1);
            usb_dir_out[usb_dir_out_size - 1] = '\0';
            return true;
        }

        char *last_slash = strrchr(search_path, '/');
        if (last_slash == NULL || last_slash == search_path) {
            break;
        }
        *last_slash = '\0';
    }
    return false;
}

size_t enumerate_all_ports(port_info_t *out_ports, size_t max_ports) {
    size_t count = 0;
    DIR *tty_class_dir = opendir("/sys/class/tty");
    if (tty_class_dir == NULL) {
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(tty_class_dir)) != NULL && count < max_ports) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char tty_sysfs_path[PATH_MAX];
        snprintf(tty_sysfs_path, sizeof(tty_sysfs_path), "/sys/class/tty/%s", entry->d_name);

        /* Bare virtual-console ttys (tty0, tty1, ...) have no "device"
         * symlink at all -- skip them, they're never real serial ports. */
        char device_link_path[PATH_MAX];
        snprintf(device_link_path, sizeof(device_link_path), "%s/device", tty_sysfs_path);
        struct stat device_link_stat;
        if (lstat(device_link_path, &device_link_stat) != 0) {
            continue;
        }

        char usb_dir[PATH_MAX];
        bool found_usb_dir = find_usb_device_dir(tty_sysfs_path, usb_dir, sizeof(usb_dir));

        port_info_t *port = &out_ports[count];
        memset(port, 0, sizeof(*port));
        if (strlen(entry->d_name) + strlen("/dev/") >= sizeof(port->device)) {
            continue; /* device name implausibly long -- not a real tty node */
        }
        snprintf(port->device, sizeof(port->device), "/dev/%s", entry->d_name);

        if (found_usb_dir) {
            char vid_text[SYSFS_HEX_FIELD_LEN];
            char pid_text[SYSFS_HEX_FIELD_LEN];
            read_sysfs_attribute(usb_dir, "idVendor", vid_text, sizeof(vid_text));
            read_sysfs_attribute(usb_dir, "idProduct", pid_text, sizeof(pid_text));
            if (vid_text[0] != '\0') {
                port->vid = (uint16_t)strtol(vid_text, NULL, HEXADECIMAL_BASE);
            }
            if (pid_text[0] != '\0') {
                port->pid = (uint16_t)strtol(pid_text, NULL, HEXADECIMAL_BASE);
            }
            read_sysfs_attribute(usb_dir, "manufacturer", port->manufacturer, sizeof(port->manufacturer));
            read_sysfs_attribute(usb_dir, "product", port->product, sizeof(port->product));
            strncpy(port->description, port->product, sizeof(port->description) - 1);
            read_sysfs_attribute(usb_dir, "serial", port->serial_number, sizeof(port->serial_number));
            port->has_serial_number = (port->serial_number[0] != '\0');
        }

        count++;
    }

    closedir(tty_class_dir);
    return count;
}

#endif /* POSIX board discovery */

size_t find_boards(port_info_t *out_boards, size_t max_boards) {
    port_info_t all_ports[MAX_BOARDS];
    size_t all_count = enumerate_all_ports(all_ports, MAX_BOARDS);

    size_t candidate_count = 0;
    for (size_t i = 0; i < all_count && candidate_count < max_boards; i++) {
        if (is_candidate(&all_ports[i])) {
            out_boards[candidate_count] = all_ports[i];
            candidate_count++;
        }
    }
    return candidate_count;
}

void describe(const port_info_t *port, char *out, size_t out_size) {
    const char *serial_text = "n/a";
    if (port->has_serial_number) {
        serial_text = port->serial_number;
    }
    snprintf(out, out_size, "%-15s VID:PID=%04X:%04X  SN=%s  %s",
             port->device, port->vid, port->pid, serial_text, port->description);
}

const port_info_t *choose(const port_info_t *boards, size_t board_count) {
    char description_buf[DESCRIBE_BUF_LEN];

    if (board_count == 1) {
        describe(&boards[0], description_buf, sizeof(description_buf));
        printf("One board found -- connecting automatically:\n  %s\n", description_buf);
        return &boards[0];
    }

    printf("Found %zu candidate boards:\n\n", board_count);
    for (size_t i = 0; i < board_count; i++) {
        describe(&boards[i], description_buf, sizeof(description_buf));
        printf("  [%zu] %s\n", i + 1, description_buf);
    }
    printf("\n");

    while (true) {
        printf("Pick a board [1-%zu]: ", board_count);
        fflush(stdout);

        char input_line[INPUT_LINE_LEN];
        if (fgets(input_line, sizeof(input_line), stdin) == NULL) {
            return NULL; /* input closed: nobody left to answer the prompt */
        }

        char *end_ptr = NULL;
        long choice = strtol(input_line, &end_ptr, DECIMAL_BASE);
        if (end_ptr != input_line && choice >= 1 && (size_t)choice <= board_count) {
            return &boards[choice - 1];
        }
        printf("Invalid choice, try again.\n");
    }
}

/* Re-locates the board after it drops off the bus. Prefers matching by
 * USB serial number (survives a device-path change); if the board
 * doesn't report one, falls back to the same device path it was on
 * before. */
bool find_port_by_serial(const char *serial_number, const char *fallback_device,
                                 port_info_t *out_port) {
    port_info_t candidates[MAX_BOARDS];
    size_t candidate_count = find_boards(candidates, MAX_BOARDS);

    if (serial_number != NULL && serial_number[0] != '\0') {
        for (size_t i = 0; i < candidate_count; i++) {
            if (candidates[i].has_serial_number
                && strcmp(candidates[i].serial_number, serial_number) == 0) {
                *out_port = candidates[i];
                return true;
            }
        }
    }

    for (size_t i = 0; i < candidate_count; i++) {
        if (strcmp(candidates[i].device, fallback_device) == 0) {
            *out_port = candidates[i];
            return true;
        }
    }
    return false;
}
