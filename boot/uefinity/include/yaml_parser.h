#pragma once
#ifndef _YAML_PARSER_H_
#define _YAML_PARSER_H_

#include <ryml.hpp>

class UefinityYamlParser
{
public:
    struct BootloaderConfig
    {
        const char* name;
        const char* version;
        const char* description;
        const char* message_box;
        const char* default_os;
        int timeout;
        bool boot_logo;
        const char* global_debug_options;
    };

    struct ArchitectureConfig
    {
        const char* type;
        bool serial_debug;
        int serial_baud_rate;
        bool efi_chainload;
        int uefi_gop_mode;
    };

    struct DisplayConfig
    {
        const char* title;
        bool minimal_ui;
        bool show_progress;
        bool show_memory_info;
        bool show_system_info;
        const char* menu_text;
        const char* menu_background;
        const char* text;
        const char* background;
        const char* title_color;
        const char* status;
    };

    struct OperatingSystemEntry
    {
        const char* name;
        const char* boot_type;
        const char* system_path;
        const char* kernel;
        const char* hal;
        const char* options;
        bool boot_sector;
        const char* setup_path;
        const char* efi_app_path;
        const char* description;
    };

    struct FilesystemConfig
    {
        const char* primary_fs;
        const char* secondary_fs;
        bool write_support;
        const char* cache_size;
    };

    struct HardwareConfig
    {
        const char* max_memory;
        const char* video_mode;
        const char* serial_port;
        const char* keyboard_layout;
        bool mouse_support;
        bool usb_support;
        bool network_support;
    };

    struct AdvancedConfig
    {
        bool memory_test;
        bool disk_check;
        bool verbose_logging;
        bool show_boot_time;
        bool profiling;
        bool safe_mode;
    };

    struct SecurityConfig
    {
        bool secure_boot;
        bool verify_signatures;
        const char* trusted_boot_path;
        bool allow_unsigned_drivers;
    };

    struct DebuggingConfig
    {
        bool kernel_debugger;
        const char* debugger_type;
        bool break_on_start;
        const char* log_level;
        const char* debug_buffer_size;
    };

private:
    ryml::Tree m_tree;
    ryml::NodeRef m_root;
    bool m_parsed;

    BootloaderConfig m_bootloader_config;
    ArchitectureConfig m_arch_config;
    DisplayConfig m_display_config;
    FilesystemConfig m_filesystem_config;
    HardwareConfig m_hardware_config;
    AdvancedConfig m_advanced_config;
    SecurityConfig m_security_config;
    DebuggingConfig m_debugging_config;

    static constexpr size_t MAX_OS_ENTRIES = 16;
    OperatingSystemEntry m_os_entries[MAX_OS_ENTRIES];
    size_t m_os_count;

    void ParseBootloaderSection();
    void ParseArchitectureSection();
    void ParseDisplaySection();
    void ParseOperatingSystemsSection();
    void ParseFilesystemSection();
    void ParseHardwareSection();
    void ParseAdvancedSection();
    void ParseSecuritySection();
    void ParseDebuggingSection();

    const char* GetStringValue(ryml::NodeRef node, const char* key, const char* default_value = "");
    int GetIntValue(ryml::NodeRef node, const char* key, int default_value = 0);
    bool GetBoolValue(ryml::NodeRef node, const char* key, bool default_value = false);

public:
    UefinityYamlParser();
    ~UefinityYamlParser();

    bool ParseFromString(const char* yaml_content);
    bool ParseFromFile(const char* file_path);

    bool IsValid() const { return m_parsed; }

    const BootloaderConfig& GetBootloaderConfig() const { return m_bootloader_config; }
    const ArchitectureConfig& GetArchitectureConfig() const { return m_arch_config; }
    const DisplayConfig& GetDisplayConfig() const { return m_display_config; }
    const FilesystemConfig& GetFilesystemConfig() const { return m_filesystem_config; }
    const HardwareConfig& GetHardwareConfig() const { return m_hardware_config; }
    const AdvancedConfig& GetAdvancedConfig() const { return m_advanced_config; }
    const SecurityConfig& GetSecurityConfig() const { return m_security_config; }
    const DebuggingConfig& GetDebuggingConfig() const { return m_debugging_config; }

    const OperatingSystemEntry* GetOperatingSystemEntries() const { return m_os_entries; }
    size_t GetOperatingSystemCount() const { return m_os_count; }

    const OperatingSystemEntry* FindOperatingSystem(const char* name) const;
};

#endif // _YAML_PARSER_H_