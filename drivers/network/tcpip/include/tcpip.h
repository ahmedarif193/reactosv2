/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        include/tcpip.h
 * PURPOSE:     TCP/IP protocol driver definitions
 * NOTES:       Spin lock acquire order:
 *                - Net table list lock
 *                - Interface lock
 *                - Interface list lock
 *                - Prefix list lock
 *                - Neighbor cache lock
 *                - Route cache lock
 */

#pragma once

#ifdef _MSC_VER
#include <basetsd.h>
#include <ntddk.h>
#include <windef.h>
#include <ndis.h>
#include <tdikrnl.h>
#include <tdiinfo.h>
#else
#include <ntddk.h>
#include <ndis.h>
#include <tdikrnl.h>
#include <tdiinfo.h>
#endif

#include <debug.h>

#define TAG_STRING	' RTS' /* string */

/* Define _NTTEST_ to make test version. Device names are prefixed with
   'NT' to allow the driver to run side by side with MS TCP/IP driver */
//#define _NTTEST_

/* FIXME: The following should be moved to ntddk.h or tdi headers */
#ifndef _MSC_VER

#ifndef IO_NETWORK_INCREMENT
#define IO_NETWORK_INCREMENT 2
#endif

#endif

#ifdef _MSC_VER
/* EXPORTED is already defined ddk/defines.h */
#define EXPORTED __declspec(dllexport)

#endif

#include <titypes.h>
#include <ticonsts.h>

/* Macros */

#define MIN(value1, value2) \
    ((value1 < value2)? value1 : value2)

#define MAX(value1, value2) \
    ((value1 > value2)? value1 : value2)

#define NDIS_BUFFER_TAG FOURCC('n','b','u','f')
#define NDIS_PACKET_TAG FOURCC('n','p','k','t')

#if defined(i386) || defined(_AMD64_) || defined(_ARM_) || defined(_ARM64_)

/* DWORD network to host byte order conversion for i386 */
#define DN2H(dw) \
    ((((dw) & 0xFF000000L) >> 24) | \
	 (((dw) & 0x00FF0000L) >> 8) | \
	 (((dw) & 0x0000FF00L) << 8) | \
	 (((dw) & 0x000000FFL) << 24))

/* DWORD host to network byte order conversion for i386 */
#define DH2N(dw) \
	((((dw) & 0xFF000000L) >> 24) | \
	 (((dw) & 0x00FF0000L) >> 8) | \
	 (((dw) & 0x0000FF00L) << 8) | \
	 (((dw) & 0x000000FFL) << 24))

/* WORD network to host order conversion for i386 */
#define WN2H(w) \
	((((w) & 0xFF00) >> 8) | \
	 (((w) & 0x00FF) << 8))

/* WORD host to network byte order conversion for i386 */
#define WH2N(w) \
	((((w) & 0xFF00) >> 8) | \
	 (((w) & 0x00FF) << 8))

#else /* defined(i386) || defined(_AMD64_) || defined(_ARM_) || defined(_ARM64_) */

#error Unsupported architecture

/* DWORD network to host byte order conversion for other architectures */
#define DN2H(dw) \
    (dw)

/* DWORD host to network byte order conversion for other architectures */
#define DH2N(dw) \
    (dw)

/* WORD network to host order conversion for other architectures */
#define WN2H(w) \
    (w)

/* WORD host to network byte order conversion for other architectures */
#define WH2N(w) \
    (w)

#endif /* defined(i386) || defined(_AMD64_) || defined(_ARM_) || defined(_ARM64_) */

/* AF_INET and other things Arty likes to use ;) */
#define AF_INET 2
#define SOCK_STREAM 1

/* Should use TDI structure, but Arty wants to keep BSD style */
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
struct in_addr
{
    union
    {
        struct { u_char s_b1,s_b2,s_b3,s_b4; } S_un_b;
        struct { u_short s_w1,s_w2; } S_un_w;
        u_long S_addr;
    } S_un;
#define s_addr  S_un.S_addr
#define s_host  S_un.S_un_b.s_b2
#define s_net   S_un.S_un_b.s_b1
#define s_imp   S_un.S_un_w.s_w2
#define s_impno S_un.S_un_b.s_b4
#define s_lh    S_un.S_un_b.s_b3
};

#define __LWIP_INET_H__
#include "lwip/sockets.h"

/* Sufficient information to manage the entity list */
typedef struct {
    UINT tei_entity;
    UINT tei_instance;
    PVOID context;
    UINT flags;
} TDIEntityInfo;

#ifndef htons
#define htons(x) WH2N(x)
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#endif

/* Global variable */
extern PDEVICE_OBJECT TCPDeviceObject;
extern PDEVICE_OBJECT UDPDeviceObject;
extern PDEVICE_OBJECT IPDeviceObject;
extern PDEVICE_OBJECT RawIPDeviceObject;
extern LIST_ENTRY InterfaceListHead;
extern KSPIN_LOCK InterfaceListLock;
extern LIST_ENTRY AddressFileListHead;
extern KSPIN_LOCK AddressFileListLock;
extern NDIS_HANDLE GlobalPacketPool;
extern NDIS_HANDLE GlobalBufferPool;
extern KSPIN_LOCK EntityListLock;
extern TDIEntityInfo *EntityList;
extern ULONG EntityCount;
extern ULONG EntityMax;

extern NTSTATUS TiGetProtocolNumber( PUNICODE_STRING FileName,
				     PULONG Protocol );

/* EOF */




//FIXME: use this instead
// /* byteorder.h — include this once and only define if not provided by lwIP/winsock */
// #ifndef TCPIP_BYTEORDER_H
// #define TCPIP_BYTEORDER_H

// #include <wdm.h> /* or ntddk.h for ReactOS; brings in ULONG/USHORT */

// /* Decide endianness at compile-time. ReactOS only builds LE,
//    but keep this future-proof for BE ports. */
// #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
//   #define HOST_BIG_ENDIAN 1
// #else
//   #define HOST_LITTLE_ENDIAN 1
// #endif

// /* Intrinsics for fast swaps */
// #if defined(_MSC_VER)
//   #include <intrin.h>
//   #define BSWAP16(x) _byteswap_ushort((USHORT)(x))
//   #define BSWAP32(x) _byteswap_ulong((ULONG)(x))
//   #define BSWAP64(x) _byteswap_uint64((unsigned __int64)(x))
// #else
//   #define BSWAP16(x) __builtin_bswap16((unsigned short)(x))
//   #define BSWAP32(x) __builtin_bswap32((unsigned int)(x))
//   #define BSWAP64(x) __builtin_bswap64((unsigned long long)(x))
// #endif

// /* Standard names: these are what you should use in code */
// #if defined(HOST_LITTLE_ENDIAN)
//   #ifndef htons
//   #define htons(x) ((UINT16)BSWAP16(x))
//   #endif
//   #ifndef htonl
//   #define htonl(x) ((UINT32)BSWAP32(x))
//   #endif
//   #ifndef ntohs
//   #define ntohs(x) ((UINT16)BSWAP16(x))
//   #endif
//   #ifndef ntohl
//   #define ntohl(x) ((UINT32)BSWAP32(x))
//   #endif

//   /* If needed */
//   #ifndef htonll
//   #define htonll(x) ((UINT64)BSWAP64(x))
//   #define ntohll(x) ((UINT64)BSWAP64(x))
//   #endif

// #else /* HOST_BIG_ENDIAN */
//   #ifndef htons
//   #define htons(x) (x)
//   #endif
//   #ifndef htonl
//   #define htonl(x) (x)
//   #endif
//   #ifndef ntohs
//   #define ntohs(x) (x)
//   #endif
//   #ifndef ntohl
//   #define ntohl(x) (x)
//   #endif
//   #ifndef htonll
//   #define htonll(x) (x)
//   #define ntohll(x) (x)
//   #endif
// #endif /* endianness */

// #endif /* TCPIP_BYTEORDER_H */
