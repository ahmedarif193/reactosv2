/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 HAL DMA Structures
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#ifndef _HALDMA_H_
#define _HALDMA_H_

/* INCLUDES *******************************************************************/

#include <ntdef.h>
#include <iotypes.h>

/* DEFINITIONS ****************************************************************/

/* STRUCTURES *****************************************************************/

/* DMA Mode Register Structure (for compatibility with x86 DMA) */
typedef union _DMA_MODE
{
    struct
    {
        UCHAR Channel: 2;
        UCHAR TransferType: 2;
        UCHAR AutoInitialize: 1;
        UCHAR AddressDecrement: 1;
        UCHAR RequestMode: 2;
    };
    UCHAR Byte;
} DMA_MODE, *PDMA_MODE;

/* DMA Map Register Entry */
typedef struct _ROS_MAP_REGISTER_ENTRY
{
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Counter;
} ROS_MAP_REGISTER_ENTRY, *PROS_MAP_REGISTER_ENTRY;

/* ARM64 DMA Adapter Object */
typedef struct _ADAPTER_OBJECT {
    /*
     * New style DMA object definition. The fact that it is at the beginning
     * of the ADAPTER_OBJECT structure allows us to easily implement the
     * fallback implementation of IoGetDmaAdapter.
     */
    DMA_ADAPTER DmaHeader;

    /*
     * For normal adapter objects pointer to master adapter that takes care
     * of channel allocation. For master adapter set to NULL.
     */
    struct _ADAPTER_OBJECT *MasterAdapter;

    ULONG MapRegistersPerChannel;
    PVOID AdapterBaseVa;
    PROS_MAP_REGISTER_ENTRY MapRegisterBase;

    ULONG NumberOfMapRegisters;
    ULONG CommittedMapRegisters;

    PWAIT_CONTEXT_BLOCK CurrentWcb;
    KDEVICE_QUEUE ChannelWaitQueue;
    PKDEVICE_QUEUE RegisterWaitQueue;
    LIST_ENTRY AdapterQueue;
    KSPIN_LOCK SpinLock;
    PRTL_BITMAP MapRegisters;
    PUCHAR PagePort;
    UCHAR ChannelNumber;
    UCHAR AdapterNumber;
    USHORT DmaPortAddress;
    DMA_MODE AdapterMode;
    BOOLEAN NeedsMapRegisters;
    BOOLEAN MasterDevice;
    BOOLEAN Width16Bits;
    BOOLEAN ScatterGather;
    BOOLEAN IgnoreCount;
    BOOLEAN Dma32BitAddresses;
    BOOLEAN Dma64BitAddresses;
    ULONG MaximumLength;
    LIST_ENTRY AdapterList;
} ADAPTER_OBJECT, *PADAPTER_OBJECT;

/* EXTERNAL VARIABLES *********************************************************/

/* FUNCTION PROTOTYPES ********************************************************/

#endif /* _HALDMA_H_ */