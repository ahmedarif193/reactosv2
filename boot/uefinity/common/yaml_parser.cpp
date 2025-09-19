#include "yaml_parser.h"
#include <cstring>
#include <cstdio>

UefinityYamlParser::UefinityYamlParser()
    : m_parsed(false), m_os_count(0)
{
    memset(&m_bootloader_config, 0, sizeof(m_bootloader_config));
    memset(&m_arch_config, 0, sizeof(m_arch_config));
    memset(&m_display_config, 0, sizeof(m_display_config));
    memset(&m_filesystem_config, 0, sizeof(m_filesystem_config));
    memset(&m_hardware_config, 0, sizeof(m_hardware_config));
    memset(&m_advanced_config, 0, sizeof(m_advanced_config));
    memset(&m_security_config, 0, sizeof(m_security_config));
    memset(&m_debugging_config, 0, sizeof(m_debugging_config));
    memset(m_os_entries, 0, sizeof(m_os_entries));
}

UefinityYamlParser::~UefinityYamlParser()
{
}

bool UefinityYamlParser::ParseFromString(const char* yaml_content)
{
    if (!yaml_content)
        return false;

    try
    {
        m_tree = ryml::parse_in_arena(ryml::to_csubstr(yaml_content));
        m_root = m_tree.rootref();

        if (!m_root.valid())
            return false;

        ParseBootloaderSection();
        ParseArchitectureSection();
        ParseDisplaySection();
        ParseOperatingSystemsSection();
        ParseFilesystemSection();
        ParseHardwareSection();
        ParseAdvancedSection();
        ParseSecuritySection();
        ParseDebuggingSection();

        m_parsed = true;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool UefinityYamlParser::ParseFromFile(const char* file_path)
{
    if (!file_path)
        return false;

    FILE* file = fopen(file_path, "r");
    if (!file)
        return false;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0)
    {
        fclose(file);
        return false;
    }

    char* buffer = new char[file_size + 1];
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != static_cast<size_t>(file_size))
    {
        delete[] buffer;
        return false;
    }

    buffer[file_size] = '\0';
    bool result = ParseFromString(buffer);
    delete[] buffer;

    return result;
}

const char* UefinityYamlParser::GetStringValue(ryml::NodeRef node, const char* key, const char* default_value)
{
    if (!node.valid() || !node.has_child(ryml::to_csubstr(key)))
        return default_value;

    ryml::NodeRef child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.is_val())
        return default_value;

    ryml::csubstr val = child.val();
    static char temp_buffer[512];
    size_t len = val.len < sizeof(temp_buffer) - 1 ? val.len : sizeof(temp_buffer) - 1;
    memcpy(temp_buffer, val.str, len);
    temp_buffer[len] = '\0';
    return temp_buffer;
}

int UefinityYamlParser::GetIntValue(ryml::NodeRef node, const char* key, int default_value)
{
    if (!node.valid() || !node.has_child(ryml::to_csubstr(key)))
        return default_value;

    ryml::NodeRef child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.is_val())
        return default_value;

    int value;
    if (ryml::from_chars(child.val(), &value))
        return value;

    return default_value;
}

bool UefinityYamlParser::GetBoolValue(ryml::NodeRef node, const char* key, bool default_value)
{
    if (!node.valid() || !node.has_child(ryml::to_csubstr(key)))
        return default_value;

    ryml::NodeRef child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.is_val())
        return default_value;

    ryml::csubstr val = child.val();
    if (val.len == 4 && strncmp(val.str, "true", 4) == 0)
        return true;
    if (val.len == 5 && strncmp(val.str, "false", 5) == 0)
        return false;

    return default_value;
}

void UefinityYamlParser::ParseBootloaderSection()
{
    if (!m_root.has_child("bootloader"))
        return;

    ryml::NodeRef bootloader = m_root["bootloader"];

    m_bootloader_config.name = GetStringValue(bootloader, "name");
    m_bootloader_config.version = GetStringValue(bootloader, "version");
    m_bootloader_config.description = GetStringValue(bootloader, "description");
    m_bootloader_config.message_box = GetStringValue(bootloader, "message_box");

    if (bootloader.has_child("boot"))
    {
        ryml::NodeRef boot = bootloader["boot"];
        m_bootloader_config.default_os = GetStringValue(boot, "default_os");
        m_bootloader_config.timeout = GetIntValue(boot, "timeout", 15);
        m_bootloader_config.boot_logo = GetBoolValue(boot, "boot_logo", true);
        m_bootloader_config.global_debug_options = GetStringValue(boot, "global_debug_options");
    }
}

void UefinityYamlParser::ParseArchitectureSection()
{
    if (!m_root.has_child("architecture"))
        return;

    ryml::NodeRef arch = m_root["architecture"];

    m_arch_config.type = GetStringValue(arch, "type");

    if (arch.has_child("platform"))
    {
        ryml::NodeRef platform = arch["platform"];
        m_arch_config.serial_debug = GetBoolValue(platform, "serial_debug", true);
        m_arch_config.serial_baud_rate = GetIntValue(platform, "serial_baud_rate", 115200);
        m_arch_config.efi_chainload = GetBoolValue(platform, "efi_chainload", true);
        m_arch_config.uefi_gop_mode = GetIntValue(platform, "uefi_gop_mode", 0);
    }
}

void UefinityYamlParser::ParseDisplaySection()
{
    if (!m_root.has_child("display"))
        return;

    ryml::NodeRef display = m_root["display"];

    m_display_config.title = GetStringValue(display, "title");
    m_display_config.minimal_ui = GetBoolValue(display, "minimal_ui", false);
    m_display_config.show_progress = GetBoolValue(display, "show_progress", true);
    m_display_config.show_memory_info = GetBoolValue(display, "show_memory_info", true);
    m_display_config.show_system_info = GetBoolValue(display, "show_system_info", true);

    if (display.has_child("colors"))
    {
        ryml::NodeRef colors = display["colors"];
        m_display_config.menu_text = GetStringValue(colors, "menu_text");
        m_display_config.menu_background = GetStringValue(colors, "menu_background");
        m_display_config.text = GetStringValue(colors, "text");
        m_display_config.background = GetStringValue(colors, "background");
        m_display_config.title_color = GetStringValue(colors, "title");
        m_display_config.status = GetStringValue(colors, "status");
    }
}

void UefinityYamlParser::ParseOperatingSystemsSection()
{
    if (!m_root.has_child("operating_systems"))
        return;

    ryml::NodeRef os_section = m_root["operating_systems"];
    m_os_count = 0;

    for (ryml::NodeRef os_entry : os_section.children())
    {
        if (m_os_count >= MAX_OS_ENTRIES)
            break;

        OperatingSystemEntry& entry = m_os_entries[m_os_count];

        entry.name = GetStringValue(os_entry, "name");
        entry.boot_type = GetStringValue(os_entry, "boot_type");
        entry.system_path = GetStringValue(os_entry, "system_path");
        entry.kernel = GetStringValue(os_entry, "kernel");
        entry.hal = GetStringValue(os_entry, "hal");
        entry.options = GetStringValue(os_entry, "options");
        entry.boot_sector = GetBoolValue(os_entry, "boot_sector", false);
        entry.setup_path = GetStringValue(os_entry, "setup_path");
        entry.efi_app_path = GetStringValue(os_entry, "efi_app_path");
        entry.description = GetStringValue(os_entry, "description");

        m_os_count++;
    }
}

void UefinityYamlParser::ParseFilesystemSection()
{
    if (!m_root.has_child("filesystems"))
        return;

    ryml::NodeRef fs = m_root["filesystems"];

    m_filesystem_config.primary_fs = GetStringValue(fs, "primary_fs");
    m_filesystem_config.secondary_fs = GetStringValue(fs, "secondary_fs");
    m_filesystem_config.write_support = GetBoolValue(fs, "write_support", true);
    m_filesystem_config.cache_size = GetStringValue(fs, "cache_size");
}

void UefinityYamlParser::ParseHardwareSection()
{
    if (!m_root.has_child("hardware"))
        return;

    ryml::NodeRef hw = m_root["hardware"];

    if (hw.has_child("memory"))
    {
        ryml::NodeRef memory = hw["memory"];
        m_hardware_config.max_memory = GetStringValue(memory, "max_memory");
    }

    if (hw.has_child("display"))
    {
        ryml::NodeRef display = hw["display"];
        m_hardware_config.video_mode = GetStringValue(display, "video_mode");
    }

    if (hw.has_child("peripherals"))
    {
        ryml::NodeRef peripherals = hw["peripherals"];
        m_hardware_config.serial_port = GetStringValue(peripherals, "serial_port");
        m_hardware_config.keyboard_layout = GetStringValue(peripherals, "keyboard_layout");
        m_hardware_config.mouse_support = GetBoolValue(peripherals, "mouse_support", true);
        m_hardware_config.usb_support = GetBoolValue(peripherals, "usb_support", true);
        m_hardware_config.network_support = GetBoolValue(peripherals, "network_support", true);
    }
}

void UefinityYamlParser::ParseAdvancedSection()
{
    if (!m_root.has_child("advanced"))
        return;

    ryml::NodeRef adv = m_root["advanced"];

    if (adv.has_child("testing"))
    {
        ryml::NodeRef testing = adv["testing"];
        m_advanced_config.memory_test = GetBoolValue(testing, "memory_test", false);
        m_advanced_config.disk_check = GetBoolValue(testing, "disk_check", false);
    }

    if (adv.has_child("diagnostics"))
    {
        ryml::NodeRef diagnostics = adv["diagnostics"];
        m_advanced_config.verbose_logging = GetBoolValue(diagnostics, "verbose_logging", true);
        m_advanced_config.show_boot_time = GetBoolValue(diagnostics, "show_boot_time", true);
        m_advanced_config.profiling = GetBoolValue(diagnostics, "profiling", false);
    }

    if (adv.has_child("modes"))
    {
        ryml::NodeRef modes = adv["modes"];
        m_advanced_config.safe_mode = GetBoolValue(modes, "safe_mode", false);
    }
}

void UefinityYamlParser::ParseSecuritySection()
{
    if (!m_root.has_child("security"))
        return;

    ryml::NodeRef security = m_root["security"];

    m_security_config.secure_boot = GetBoolValue(security, "secure_boot", false);
    m_security_config.verify_signatures = GetBoolValue(security, "verify_signatures", false);
    m_security_config.trusted_boot_path = GetStringValue(security, "trusted_boot_path");
    m_security_config.allow_unsigned_drivers = GetBoolValue(security, "allow_unsigned_drivers", true);
}

void UefinityYamlParser::ParseDebuggingSection()
{
    if (!m_root.has_child("debugging"))
        return;

    ryml::NodeRef debug = m_root["debugging"];

    m_debugging_config.kernel_debugger = GetBoolValue(debug, "kernel_debugger", true);
    m_debugging_config.debugger_type = GetStringValue(debug, "debugger_type");
    m_debugging_config.break_on_start = GetBoolValue(debug, "break_on_start", false);
    m_debugging_config.log_level = GetStringValue(debug, "log_level");
    m_debugging_config.debug_buffer_size = GetStringValue(debug, "debug_buffer_size");
}

const UefinityYamlParser::OperatingSystemEntry* UefinityYamlParser::FindOperatingSystem(const char* name) const
{
    if (!name)
        return nullptr;

    for (size_t i = 0; i < m_os_count; i++)
    {
        if (m_os_entries[i].name && strcmp(m_os_entries[i].name, name) == 0)
            return &m_os_entries[i];
    }

    return nullptr;
}