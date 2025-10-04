#include "include/yaml_parser.h"
#include <iostream>

int main()
{
    UefinityYamlParser parser;

    // Test parsing the uefinity.yaml file
    if (parser.ParseFromFile("uefinity.yaml"))
    {
        std::cout << "YAML parsing successful!\n\n";

        // Test bootloader config
        const auto& bootloader = parser.GetBootloaderConfig();
        std::cout << "Bootloader Name: " << (bootloader.name ? bootloader.name : "N/A") << "\n";
        std::cout << "Version: " << (bootloader.version ? bootloader.version : "N/A") << "\n";
        std::cout << "Default OS: " << (bootloader.default_os ? bootloader.default_os : "N/A") << "\n";
        std::cout << "Timeout: " << bootloader.timeout << " seconds\n\n";

        // Test architecture config
        const auto& arch = parser.GetArchitectureConfig();
        std::cout << "Architecture: " << (arch.type ? arch.type : "N/A") << "\n";
        std::cout << "Serial Debug: " << (arch.serial_debug ? "true" : "false") << "\n";
        std::cout << "Serial Baud Rate: " << arch.serial_baud_rate << "\n\n";

        // Test display config
        const auto& display = parser.GetDisplayConfig();
        std::cout << "Display Title: " << (display.title ? display.title : "N/A") << "\n";
        std::cout << "Show Progress: " << (display.show_progress ? "true" : "false") << "\n\n";

        // Test operating systems
        std::cout << "Operating Systems:\n";
        size_t os_count = parser.GetOperatingSystemCount();
        const auto* os_entries = parser.GetOperatingSystemEntries();

        for (size_t i = 0; i < os_count; i++)
        {
            std::cout << "  [" << i << "] " << (os_entries[i].name ? os_entries[i].name : "N/A") << "\n";
            std::cout << "      Boot Type: " << (os_entries[i].boot_type ? os_entries[i].boot_type : "N/A") << "\n";
            std::cout << "      Kernel: " << (os_entries[i].kernel ? os_entries[i].kernel : "N/A") << "\n";
        }

        std::cout << "\nTotal OS entries: " << os_count << "\n";

        // Test finding a specific OS
        const auto* reactos = parser.FindOperatingSystem("ReactOS");
        if (reactos)
        {
            std::cout << "\nFound ReactOS entry:\n";
            std::cout << "  Options: " << (reactos->options ? reactos->options : "N/A") << "\n";
        }
    }
    else
    {
        std::cout << "Failed to parse YAML file!\n";
        return 1;
    }

    return 0;
}