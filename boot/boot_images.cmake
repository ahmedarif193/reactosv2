## efisys.bin

# EFI platform ID, used in environ/CMakelists.txt for bootmgfw filename naming also.
if(ARCH STREQUAL "amd64")
    set(EFI_PLATFORM_ID "x64")
elseif(ARCH STREQUAL "i386")
    if(NOT (SARCH STREQUAL "pc98" OR SARCH STREQUAL "xbox"))
        set(EFI_PLATFORM_ID "ia32")
    endif()
elseif(ARCH STREQUAL "ia64")
    set(EFI_PLATFORM_ID "ia64")
elseif(ARCH STREQUAL "arm")
    set(EFI_PLATFORM_ID "arm")
elseif(ARCH STREQUAL "arm64")
    set(EFI_PLATFORM_ID "aa64")
else()
    message(FATAL_ERROR "Unknown ARCH '" ${ARCH} "', cannot generate a valid UEFI boot filename.")
endif()

set(ISO_PRIMARY_BOOT_PARAMS)
set(ISO_EFI_BOOT_PARAMS)

if(DEFINED EFI_PLATFORM_ID)
    # Select the UEFI bootloader binary
    set(EFI_BOOT_TARGET uefildr)
    set(EFI_BOOT_SECTOR_COUNT 2880)
    if(BUILD_UEFINITY AND TARGET uefinity)
        set(EFI_BOOT_TARGET uefinity)
        # Uefinity pulls in significantly more functionality; allow a larger ESP image
        set(EFI_BOOT_SECTOR_COUNT 65536)
    elseif(TARGET bootmgfw)
        set(EFI_BOOT_TARGET bootmgfw)
    endif()

    set(_efisys_command
        native-fatten ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin -format ${EFI_BOOT_SECTOR_COUNT} EFIBOOT)

    set(_efisys_depends native-fatten ${EFI_BOOT_TARGET})
    if(BUILD_FREELDR)
        list(APPEND _efisys_command -boot ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/fat.bin)
        list(APPEND _efisys_depends fat)
    endif()

    list(APPEND _efisys_command -mkdir EFI -mkdir EFI/BOOT
        -add $<TARGET_FILE:${EFI_BOOT_TARGET}> EFI/BOOT/boot${EFI_PLATFORM_ID}.efi)

    add_custom_target(efisys
        COMMAND ${_efisys_command}
        DEPENDS ${_efisys_depends}
        VERBATIM)

    set(_efisys_file  ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin)
    set(ISO_EFI_BOOT_PARAMS -eltorito-platform efi -eltorito-boot loader/efisys.bin -no-emul-boot)
endif()

if(BUILD_FREELDR)
    set(_isombr_file  ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isombr.bin)
    set(_isoboot_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isoboot.bin)
    set(_isobtrt_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isobtrt.bin)
    set(ISO_PRIMARY_BOOT_PARAMS -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4)
endif()

# Create an 'empty' directory (guaranteed to be empty) to be able to add
# arbitrary empty directories to the ISO image using mkisofs.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/empty)

# Create a mkisofs sort file to specify an explicit ordering for the boot files
# to place them at the beginning of the image (makes ISO image analysis easier).
# See mkisofs/schilytools/mkisofs/README.sort for more details.
# As the default file sort weight is '0', give the boot files sort weights >= 1.
# Note that it is sad that '-sort' does not work using grafted points, and as a
# result we need in particular to use the boot catalog file "path" mkisofs that
# mkisofs expects, that is, the boot catalog file name is appended to the first
# host-system path listed in the file list, whatever it is, and that does not
# work well if the first item is a graft point (and especially if it's a file
# and not a directory). To fix that, the trick is to use as the first file item
# the empty directory created earlier. This ensures that:
# - the boot catalog file path is meaningful;
# - since its contents are included by mkisofs in the root of the ISO image,
#   using the empty directory ensures that no extra unwanted files are added.
#
set(ISO_SORT_FILE_DATA "${CMAKE_CURRENT_BINARY_DIR}/empty/boot.catalog 4\n")
if(BUILD_FREELDR)
    string(APPEND ISO_SORT_FILE_DATA "${_isoboot_file} 3\n")
    string(APPEND ISO_SORT_FILE_DATA "${_isobtrt_file} 2\n")
endif()
if(DEFINED EFI_PLATFORM_ID)
    string(APPEND ISO_SORT_FILE_DATA "${_efisys_file} 1\n")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort ${ISO_SORT_FILE_DATA})

# ISO image identifier names
set(ISO_MANUFACTURER "ReactOS Project") # For both the publisher and the preparer
set(ISO_VOLNAME      "ReactOS")         # For both the Volume ID and the Volume set ID


# Create user profile directories in the LiveImage
function(add_allusers_profile_dirs _image_filelist _rootdir)
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Application Data=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Music=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Pictures=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Videos=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Favorites=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/My Documents=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Start Menu/Programs/StartUp=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Templates=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
endfunction()
function(add_user_profile_dirs _image_filelist _rootdir _username)
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Application Data=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Application Data/Microsoft/Internet Explorer/Quick Launch=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Cookies=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Desktop=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Favorites=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Local Settings/Application Data=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Local Settings/History=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Local Settings/Temporary Internet Files=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Music=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Pictures=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Videos=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/NetHood=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/PrintHood=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Recent=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/SendTo=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs/Administrative Tools=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs/StartUp=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Templates=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
endfunction()


## BootCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcd.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

set(_bootcd_boot_options)
if(BUILD_FREELDR)
    list(APPEND _bootcd_boot_options -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4)
endif()
if(DEFINED ISO_EFI_BOOT_PARAMS)
    list(APPEND _bootcd_boot_options ${ISO_EFI_BOOT_PARAMS})
endif()

set(_bootcd_commands
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/bootcd.iso -iso-level 4
        -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
        ${_bootcd_boot_options} -hide boot.catalog
        -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
        -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/bootcd.$<CONFIG>.lst)

set(_bootcd_depends native-mkisofs)
if(BUILD_FREELDR)
    list(APPEND _bootcd_commands
        COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/bootcd.iso)
    list(APPEND _bootcd_depends isombr native-isohybrid)
endif()

add_custom_target(bootcd
    ${_bootcd_commands}
    DEPENDS ${_bootcd_depends}
    VERBATIM)

## BootCDRegTest
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

if(BUILD_FREELDR)
    add_custom_target(bootcdregtest
        COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/bootcdregtest.iso -iso-level 4
            -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
            -eltorito-boot loader/isobtrt.bin -no-emul-boot -boot-load-size 4 ${ISO_EFI_BOOT_PARAMS} -hide boot.catalog
            -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
            -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.$<CONFIG>.lst
        COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/bootcdregtest.iso
        DEPENDS isombr native-isohybrid native-mkisofs
        VERBATIM)
endif()

## LiveCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create TEMP dir
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "reactos/TEMP=${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create user profile directories
add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "Profiles" "Default User")

set(_livecd_boot_options)
if(BUILD_FREELDR)
    list(APPEND _livecd_boot_options -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4)
endif()
if(DEFINED ISO_EFI_BOOT_PARAMS)
    list(APPEND _livecd_boot_options ${ISO_EFI_BOOT_PARAMS})
endif()

set(_livecd_commands
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/livecd.iso -iso-level 4
        -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
        ${_livecd_boot_options} -hide boot.catalog
        -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
        -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/livecd.$<CONFIG>.lst)

set(_livecd_depends native-mkisofs)
if(BUILD_FREELDR)
    list(APPEND _livecd_commands
        COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/livecd.iso)
    list(APPEND _livecd_depends isombr native-isohybrid)
endif()

add_custom_target(livecd
    ${_livecd_commands}
    DEPENDS ${_livecd_depends}
    VERBATIM)

## HybridCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create user profile directories
add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "livecd/Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "livecd/Profiles" "Default User")

set(_hybrid_boot_options)
if(BUILD_FREELDR)
    list(APPEND _hybrid_boot_options -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4)
endif()
if(DEFINED ISO_EFI_BOOT_PARAMS)
    list(APPEND _hybrid_boot_options ${ISO_EFI_BOOT_PARAMS})
endif()

set(_hybrid_commands
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/hybridcd.iso -iso-level 4
        -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
        ${_hybrid_boot_options} -hide boot.catalog
        -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
        -duplicates-once -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.$<CONFIG>.lst)

set(_hybrid_depends bootcd livecd)
if(BUILD_FREELDR)
    list(APPEND _hybrid_commands
        COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/hybridcd.iso)
    list(APPEND _hybrid_depends native-isohybrid)
endif()

add_custom_target(hybridcd
    ${_hybrid_commands}
    DEPENDS ${_hybrid_depends}
    VERBATIM)

if(DEFINED EFI_PLATFORM_ID)
    # For things like flashing USB drives, we also add the efi file into efi/boot.
    add_cd_file(TARGET efisys FILE ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin DESTINATION loader NO_CAB NOT_IN_HYBRIDCD FOR bootcd regtest livecd hybridcd)

    if(TARGET ${EFI_BOOT_TARGET})
        add_cd_file(
            TARGET ${EFI_BOOT_TARGET}
            DESTINATION efi/boot
            NO_CAB
            NAME_ON_CD boot${EFI_PLATFORM_ID}.efi
            FOR livecd hybridcd)
    endif()
endif()
