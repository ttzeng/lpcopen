/*
 * FreeRTOS+TCP Labs Build 150406 (C) 2015 Real Time Engineers ltd.
 * Authors include Hein Tibosch and Richard Barry
 *
 *******************************************************************************
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 ***                                                                         ***
 ***                                                                         ***
 ***   FREERTOS+TCP IS STILL IN THE LAB:                                     ***
 ***                                                                         ***
 ***   This product is functional and is already being used in commercial    ***
 ***   products.  Be aware however that we are still refining its design,    ***
 ***   the source code does not yet fully conform to the strict coding and   ***
 ***   style standards mandated by Real Time Engineers ltd., and the         ***
 ***   documentation and testing is not necessarily complete.                ***
 ***                                                                         ***
 ***   PLEASE REPORT EXPERIENCES USING THE SUPPORT RESOURCES FOUND ON THE    ***
 ***   URL: http://www.FreeRTOS.org/contact  Active early adopters may, at   ***
 ***   the sole discretion of Real Time Engineers Ltd., be offered versions  ***
 ***   under a license other than that described below.                      ***
 ***                                                                         ***
 ***                                                                         ***
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 *******************************************************************************
 *
 * - Open source licensing -
 * While FreeRTOS+TCP is in the lab it is provided only under version two of the
 * GNU General Public License (GPL) (which is different to the standard FreeRTOS
 * license).  FreeRTOS+TCP is free to download, use and distribute under the
 * terms of that license provided the copyright notice and this text are not
 * altered or removed from the source files.  The GPL V2 text is available on
 * the gnu.org web site, and on the following
 * URL: http://www.FreeRTOS.org/gpl-2.0.txt.  Active early adopters may, and
 * solely at the discretion of Real Time Engineers Ltd., be offered versions
 * under a license other then the GPL.
 *
 * FreeRTOS+TCP is distributed in the hope that it will be useful.  You cannot
 * use FreeRTOS+TCP unless you agree that you use the software 'as is'.
 * FreeRTOS+TCP is provided WITHOUT ANY WARRANTY; without even the implied
 * warranties of NON-INFRINGEMENT, MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. Real Time Engineers Ltd. disclaims all conditions and terms, be they
 * implied, expressed, or statutory.
 *
 * 1 tab == 4 spaces!
 *
 * http://www.FreeRTOS.org
 * http://www.FreeRTOS.org/plus
 * http://www.FreeRTOS.org/labs
 *
 */

#ifndef FREERTOS_DEFAULT_IP_CONFIG_H
#define FREERTOS_DEFAULT_IP_CONFIG_H

/* The error numbers defined in this file will be moved to the core FreeRTOS
code in future versions of FreeRTOS - at which time the following header file
will be removed. */
#include "FreeRTOS_errno_TCP.h"

/* This file provides default values for configuration options that are missing
from the FreeRTOSIPConfig.h configuration header file. */

#ifndef ipconfigUSE_TCP
	#define	ipconfigUSE_TCP						( 1 )
#endif

#if	ipconfigUSE_TCP

	/* Include support for TCP scaling windows */
	#ifndef ipconfigUSE_TCP_WIN
		#define ipconfigUSE_TCP_WIN				( 1 )
	#endif

	#ifndef ipconfigTCP_WIN_SEG_COUNT
		#define	ipconfigTCP_WIN_SEG_COUNT		( 256 )
	#endif

	#ifndef ipconfigIGNORE_UNKNOWN_PACKETS
		/* When non-zero, TCP will not send RST packets in reply to
		TCP packets which are unknown, or out-of-order. */
		#define ipconfigIGNORE_UNKNOWN_PACKETS	( 0 )
	#endif
#endif

/*
 * For debuging/logging: check if the port number is used for telnet
 * Some events will not be logged for telnet connections
 * because it would produce logging about the transmission of the logging...
 * This macro will only be used if FreeRTOS_debug_printf() is defined for logging
 */
#ifndef ipconfigTCP_MAY_LOG_PORT
	#define ipconfigTCP_MAY_LOG_PORT(xPort)			( ( xPort ) != 23 )
#endif


#ifndef	ipconfigSOCK_DEFAULT_RECEIVE_BLOCK_TIME
	#define	ipconfigSOCK_DEFAULT_RECEIVE_BLOCK_TIME	portMAX_DELAY
#endif

#ifndef	ipconfigSOCK_DEFAULT_SEND_BLOCK_TIME
	#define	ipconfigSOCK_DEFAULT_SEND_BLOCK_TIME	portMAX_DELAY
#endif

/*
 * FreeRTOS debug logging routine (proposal)
 * The macro will be called in the printf() style. Users can define
 * their own logging routine as:
 *
 *     #define FreeRTOS_debug_printf( MSG )			my_printf MSG
 *
 * The FreeRTOS_debug_printf() must be thread-safe but does not have to be
 * interrupt-safe.
 */
#ifdef ipconfigHAS_DEBUG_PRINTF
	#if( ipconfigHAS_DEBUG_PRINTF == 0 )
		#ifdef FreeRTOS_debug_printf
			#error Do not define FreeRTOS_debug_print if ipconfigHAS_DEBUG_PRINTF is set to 0
		#endif /* ifdef FreeRTOS_debug_printf */
	#endif /* ( ipconfigHAS_DEBUG_PRINTF == 0 ) */
#endif /* ifdef ipconfigHAS_DEBUG_PRINTF */

#ifndef FreeRTOS_debug_printf
    #define FreeRTOS_debug_printf( MSG )		do{} while(0)
	#define ipconfigHAS_DEBUG_PRINTF			0
#endif

/*
 * FreeRTOS general logging routine (proposal)
 * Used in some utility functions such as FreeRTOS_netstat() and FreeRTOS_PrintARPCache()
 *
 *     #define FreeRTOS_printf( MSG )			my_printf MSG
 *
 * The FreeRTOS_printf() must be thread-safe but does not have to be interrupt-safe
 */
#ifdef ipconfigHAS_PRINTF
	#if( ipconfigHAS_PRINTF == 0 )
		#ifdef FreeRTOS_printf
			#error Do not define FreeRTOS_print if ipconfigHAS_PRINTF is set to 0
		#endif /* ifdef FreeRTOS_debug_printf */
	#endif /* ( ipconfigHAS_PRINTF == 0 ) */
#endif /* ifdef ipconfigHAS_PRINTF */

#ifndef FreeRTOS_printf
    #define FreeRTOS_printf( MSG )				do{} while(0)
	#define ipconfigHAS_PRINTF					0
#endif

/*
 * In cases where a lot of logging is produced, FreeRTOS_flush_logging( )
 * will be called to give the logging module a chance to flush the data
 * An example of this is the netstat command, which produces many lines of logging
 */
#ifndef FreeRTOS_flush_logging
    #define FreeRTOS_flush_logging( )			do{} while(0)
#endif

/* Malloc functions. Within most applications of FreeRTOS, the couple
 * pvPortMalloc()/vPortFree() will be used.
 * If there is also SDRAM, the user may decide to use a different memory
 * allocator:
 * MallocLarge is used to allocate large TCP buffers (for Rx/Tx)
 * MallocSocket is used to allocate the space for the sockets
 */
#ifndef pvPortMallocLarge
	#define pvPortMallocLarge( x )				pvPortMalloc( x )
#endif

#ifndef vPortFreeLarge
	#define vPortFreeLarge(ptr)					vPortFree(ptr)
#endif

#ifndef pvPortMallocSocket
	#define pvPortMallocSocket( x )				pvPortMalloc( x )
#endif

#ifndef vPortFreeSocket
	#define vPortFreeSocket(ptr)				vPortFree(ptr)
#endif

/*
 * At several places within the library, random numbers are needed:
 * - DHCP:    For creating a DHCP transaction number
 * - TCP:     Set the Initial Sequence Number: this is the value of the first outgoing
 *            sequence number being used when connecting to a peer.
 *            Having a well randomised ISN is important to avoid spoofing
 * - UDP/TCP: for setting the first port number to be used, in case a socket
 *            uses a 'random' or anonymous port number
 */
#ifndef ipconfigRAND32
	#define ipconfigRAND32() rand()
#endif
/* --------------------------------------------------------
 * End of: HT Added some macro defaults for the PLUS-UDP project
 * -------------------------------------------------------- */

#ifndef ipconfigUSE_NETWORK_EVENT_HOOK
	#define ipconfigUSE_NETWORK_EVENT_HOOK 0
#endif

#ifndef ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS
	#define ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS ( 20 / portTICK_PERIOD_MS )
#endif

#ifndef ipconfigARP_CACHE_ENTRIES
	#define ipconfigARP_CACHE_ENTRIES		10
#endif

#ifndef ipconfigMAX_ARP_RETRANSMISSIONS
	#define ipconfigMAX_ARP_RETRANSMISSIONS ( 5 )
#endif

#ifndef ipconfigMAX_ARP_AGE
	#define ipconfigMAX_ARP_AGE			150
#endif

#ifndef ipconfigUSE_ARP_REVERSED_LOOKUP
	#define ipconfigUSE_ARP_REVERSED_LOOKUP		0
#endif

#ifndef ipconfigUSE_ARP_REMOVE_ENTRY
	#define	ipconfigUSE_ARP_REMOVE_ENTRY		0
#endif

#ifndef ipconfigINCLUDE_FULL_INET_ADDR
	#define ipconfigINCLUDE_FULL_INET_ADDR	1
#endif

#ifndef ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS
	#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS		45
#endif

#ifndef ipconfigEVENT_QUEUE_LENGTH
	#define ipconfigEVENT_QUEUE_LENGTH		( ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS + 5 )
#endif

#ifndef ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND
	#define ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND 1
#endif

#ifndef ipconfigUDP_TIME_TO_LIVE
	#define ipconfigUDP_TIME_TO_LIVE		128
#endif

#ifndef ipconfigTCP_TIME_TO_LIVE
	#define ipconfigTCP_TIME_TO_LIVE		128
#endif

#ifndef ipconfigUDP_MAX_RX_PACKETS
	/* Make postive to define the maximum number of packets which will be buffered
	 * for each UDP socket.
	 * Can be overridden with the socket option FREERTOS_SO_UDP_MAX_RX_PACKETS
	 */
	#define ipconfigUDP_MAX_RX_PACKETS		0
#endif

#ifndef ipconfigUSE_DHCP
	#define ipconfigUSE_DHCP	1
#endif

#ifndef ipconfigNETWORK_MTU
	#define ipconfigNETWORK_MTU		1500
#endif

#ifndef ipconfigTCP_MSS
	#define ipconfigTCP_MSS		( ipconfigNETWORK_MTU - ipSIZE_OF_IP_HEADER - ipSIZE_OF_TCP_HEADER )
#endif

/* Each TCP socket has circular stream buffers for Rx and Tx, which
 * have a fixed maximum size.
 * The defaults for these size are defined here, although
 * they can be overridden at runtime by using the setsockopt() call */
#ifndef ipconfigTCP_RX_BUF_LEN
	#define ipconfigTCP_RX_BUF_LEN			( 4 * ipconfigTCP_MSS )	/* defaults to 5840 bytes */
#endif

/* Define the size of Tx stream buffer for TCP sockets */
#ifndef ipconfigTCP_TX_BUF_LEN
#	define ipconfigTCP_TX_BUF_LEN			( 4 * ipconfigTCP_MSS )	/* defaults to 5840 bytes */
#endif

#ifndef ipconfigMAXIMUM_DISCOVER_TX_PERIOD
	#ifdef _WINDOWS_
		#define ipconfigMAXIMUM_DISCOVER_TX_PERIOD		( 999 / portTICK_PERIOD_MS )
	#else
		#define ipconfigMAXIMUM_DISCOVER_TX_PERIOD		( 30000 / portTICK_PERIOD_MS )
	#endif /* _WINDOWS_ */
#endif /* ipconfigMAXIMUM_DISCOVER_TX_PERIOD */

#ifndef ipconfigUSE_DNS
	#define ipconfigUSE_DNS						1
#endif

#ifndef ipconfigDNS_REQUEST_ATTEMPTS
	#define ipconfigDNS_REQUEST_ATTEMPTS		5
#endif

#ifndef ipconfigUSE_DNS_CACHE
	#define ipconfigUSE_DNS_CACHE				0
#endif

#if( ipconfigUSE_DNS_CACHE != 0 )
	#ifndef ipconfigDNS_CACHE_NAME_LENGTH
		#define ipconfigDNS_CACHE_NAME_LENGTH		( 16 )
	#endif

	#ifndef ipconfigDNS_CACHE_ENTRIES
		#define ipconfigDNS_CACHE_ENTRIES			0
	#endif
#endif /* ipconfigUSE_DNS_CACHE != 0 */

#ifndef ipconfigUSE_LLMNR
	/* Include support for LLMNR: Link-local Multicast Name Resolution (non-Microsoft) */
	#define ipconfigUSE_LLMNR					( 0 )
#endif

#ifndef ipconfigREPLY_TO_INCOMING_PINGS
	#define ipconfigREPLY_TO_INCOMING_PINGS		1
#endif

#ifndef ipconfigSUPPORT_OUTGOING_PINGS
	#define ipconfigSUPPORT_OUTGOING_PINGS		0
#endif

#ifndef ipconfigUDP_LOOPBACK_ETHERNET_PACKETS
	#define ipconfigUDP_LOOPBACK_ETHERNET_PACKETS	0
#endif

#ifndef ipconfigFILTER_OUT_NON_ETHERNET_II_FRAMES
	#define ipconfigFILTER_OUT_NON_ETHERNET_II_FRAMES 1
#endif

#ifndef ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES
	#define ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES	1
#endif

#ifndef configINCLUDE_TRACE_RELATED_CLI_COMMANDS
	#define ipconfigINCLUDE_EXAMPLE_FREERTOS_PLUS_TRACE_CALLS 0
#else
	#define ipconfigINCLUDE_EXAMPLE_FREERTOS_PLUS_TRACE_CALLS configINCLUDE_TRACE_RELATED_CLI_COMMANDS
#endif

#ifndef ipconfigFREERTOS_PLUS_NABTO
	#define ipconfigFREERTOS_PLUS_NABTO 0
#endif

#ifndef ipconfigNABTO_TASK_STACK_SIZE
	#define ipconfigNABTO_TASK_STACK_SIZE ( configMINIMAL_STACK_SIZE * 2 )
#endif

#ifndef ipconfigNABTO_TASK_PRIORITY
	#define ipconfigNABTO_TASK_PRIORITY	 ( ipconfigIP_TASK_PRIORITY + 1 )
#endif

#ifndef ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM
	#define	ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM	( 0 )
#endif

#ifndef ipconfigETHERNET_DRIVER_FILTERS_PACKETS
	#define	ipconfigETHERNET_DRIVER_FILTERS_PACKETS	( 0 )
#endif

#ifndef ipconfigWATCHDOG_TIMER
	/* This macro will be called in every loop the IP-task makes.  It may be
	replaced by user-code that triggers a watchdog */
	#define ipconfigWATCHDOG_TIMER()
#endif

#ifndef ipconfigUSE_CALLBACKS
	#define ipconfigUSE_CALLBACKS			( 0 )
#endif

#if( ipconfigUSE_CALLBACKS != 0 )
	#ifndef ipconfigIS_VALID_PROG_ADDRESS
		/* Replace this macro with a test returning non-zero if the memory pointer to by x
		 * is valid memory which can contain executable code
		 * In fact this is an extra safety measure: if a handler points to invalid memory,
		 * it will not be called
		 */
		#define ipconfigIS_VALID_PROG_ADDRESS(x)		( ( x ) != NULL )
	#endif
#endif

#ifndef ipconfigHAS_INLINE_FUNCTIONS
	#define	ipconfigHAS_INLINE_FUNCTIONS	( 1 )
#endif

#ifndef portINLINE
	#define portINLINE inline
#endif

#ifndef ipconfigZERO_COPY_TX_DRIVER
	/* When non-zero, the buffers passed to the SEND routine may be passed
	to DMA. As soon as sending is ready, the buffers must be released by
	calling vReleaseNetworkBufferAndDescriptor(), */
	#define ipconfigZERO_COPY_TX_DRIVER		( 0 )
#endif

#ifndef ipconfigZERO_COPY_RX_DRIVER
	/* This define doesn't mean much to the driver, except that it makes
	sure that pxPacketBuffer_to_NetworkBuffer() will be included. */
	#define ipconfigZERO_COPY_RX_DRIVER		( 0 )
#endif

#endif /* FREERTOS_DEFAULT_IP_CONFIG_H */
