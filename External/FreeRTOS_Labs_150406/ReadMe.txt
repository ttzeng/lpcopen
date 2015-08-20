The FreeRTOS Labs download currently contains the FreeRTOS+TCP and FreeRTOS+FAT
source code, along with a set of examples that build using the FreeRTOS Windows
port.

The directory structure of the official FreeRTOS download is used, even though
currently these products are provided in a separate Labs download.  This allow
the projects to be seamlessly moved from one download to the other, but can seem
strange when the files are viewed in isolation.

Two Visual Studio project files are provided:

    1) The main FreeRTOS+TCP and FreeRTOS+FAT example

    This project demonstrates the TCP/IP stack and embedded FAT file system
    being used together in various scenarios.  The Visual Studio project used to
    build this project is located in the following directory:
    FreeRTOS-Plus\Demo\FreeRTOS_Plus_TCP_and_CLI_Windows_Simulator

    2) A simple FreeRTOS+TCP only example

    This project demonstrates the FreeRTOS+TCP stack being used to create both
    TCP and UDP clients.  The Visual Studio project used to build this demo is
    located in the following directory:
    FreeRTOS-Plus\Demo\FreeRTOS_Plus_TCP_Minimal_Windows_Simulator

Instructions on using the Visual Studio project are provided on the following URL:
http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/examples_FreeRTOS_simulator.html

A description of the FreeRTOS+TCP source code directory is provided on the
following URL:
http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/TCP_Networking_Tutorial_Source_Code_Organisation.html

A description of the FreeRTOS+FAT source code directory is provided on the
following URL:
http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_FAT/free_fat_file_system_source_code_organisation.html

A description of the way the main FreeRTOS .zip file download source code is
organised is provided on the following URL:
http://www.freertos.org/a00017.html

License information:
http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/FreeRTOS_Plus_TCP_License.html
http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_FAT/FreeRTOS_Plus_FAT_License.html

