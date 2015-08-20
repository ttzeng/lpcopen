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

/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_ARP.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_TCP_IP.h"
#include "FreeRTOS_DHCP.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"
#include "FreeRTOS_DNS.h"

/* Ensure defined configuration constants are using the most up to date naming. */
#ifdef tcpconfigIP_TIME_TO_LIVE
	#error now called: ipconfigTCP_TIME_TO_LIVE
#endif

#ifdef updconfigIP_TIME_TO_LIVE
	#error now called: ipconfigUDP_TIME_TO_LIVE
#endif

#ifdef updconfigLOOPBACK_ETHERNET_PACKETS
	#error now called: ipconfigUDP_LOOPBACK_ETHERNET_PACKETS
#endif

#ifdef ipFILLER_SIZE
	#error now called: ipconfigPACKET_FILLER_SIZE
#endif

#ifdef dnsMAX_REQUEST_ATTEMPTS
	#error now called: ipconfigDNS_REQUEST_ATTEMPTS
#endif

#ifdef ipconfigUDP_TASK_PRIORITY
	#error now called: ipconfigIP_TASK_PRIORITY
#endif

#ifdef ipconfigUDP_TASK_STACK_SIZE_WORDS
	#error now called: ipconfigIP_TASK_STACK_SIZE_WORDS
#endif

#ifdef ipconfigDRIVER_INCLUDED_RX_IP_FILTERING
	#error now called: ipconfigETHERNET_DRIVER_FILTERS_PACKETS
#endif

#ifdef ipconfigMAX_SEND_BLOCK_TIME_TICKS
	#error now called: ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS
#endif

#ifdef ipconfigUSE_RECEIVE_CONNECT_CALLBACKS
	#error now called: ipconfigUSE_CALLBACKS
#endif

#ifdef ipconfigNUM_NETWORK_BUFFERS
	#error now called: ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS
#endif

#ifdef ipconfigTCP_HANG_PROT
	#error now called: ipconfigTCP_HANG_PROTECTION
#endif

#ifdef ipconfigTCP_HANG_PROT_TIME
	#error now called: ipconfigTCP_HANG_PROTECTION_TIME
#endif

#ifdef FreeRTOS_lprintf
	#error now called: FreeRTOS_debug_printf
#endif

#if ( ipconfigEVENT_QUEUE_LENGTH < ( ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS + 5 ) )
	#error The ipconfigEVENT_QUEUE_LENGTH parameter must be at least ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS + 5
#endif

#if ( ipconfigNETWORK_MTU < 46 )
	#error ipconfigNETWORK_MTU must be at least 46.
#endif

#if ( (ipconfigTCP_MSS + ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER ) > ipconfigNETWORK_MTU)
	#error The ipconfigTCP_MSS setting in FreeRTOSIPConfig.h is too large.
#endif

#ifdef	ipconfigBUFFER_ALLOC_FIXED_SIZE
	#error ipconfigBUFFER_ALLOC_FIXED_SIZE was dropped and replaced by a const value, declared in BufferAllocation[12].c
#endif

#ifdef	ipconfigNIC_SEND_PASSES_DMA
	#error now called: ipconfigZERO_COPY_TX_DRIVER
#endif

/*-----------------------------------------------------------*/

/* ICMP protocol definitions. */
#define ipICMP_ECHO_REQUEST		( ( uint16_t ) 8 )
#define ipICMP_ECHO_REPLY		( ( uint16_t ) 0 )

/* Time delay between repeated attempts to initialise the network hardware. */
#define ipINITIALISATION_RETRY_DELAY	( ( ( TickType_t ) 3000 ) / portTICK_PERIOD_MS )

/* Defines how often the ARP timer callback function is executed.  The time is
shorted in the Windows simulator as simulated time is not real time. */
#ifdef _WINDOWS_
	#define ipARP_TIMER_PERIOD_MS	( 500 ) /* For windows simulator builds. */
#else
	#define ipARP_TIMER_PERIOD_MS	( 10000 )
#endif

#if( ( ipconfigUSE_TCP == 1 ) && !defined( ipTCP_TIMER_PERIOD_MS ) )
	#define ipTCP_TIMER_PERIOD_MS	( 1000 )
#endif

/* If ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES is set to 1, then the Ethernet
driver will filter incoming packets and only pass the stack those packets it
considers need processing.  In this case ipCONSIDER_FRAME_FOR_PROCESSING() can
be #defined away.  If ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES is set to 0
then the Ethernet driver will pass all received packets to the stack, and the
stack must do the filtering itself.  In this case ipCONSIDER_FRAME_FOR_PROCESSING
needs to call eConsiderFrameForProcessing. */
#if ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0
	#define ipCONSIDER_FRAME_FOR_PROCESSING( pucEthernetBuffer ) eConsiderFrameForProcessing( ( pucEthernetBuffer ) )
#else
	#define ipCONSIDER_FRAME_FOR_PROCESSING( pucEthernetBuffer ) eProcessBuffer
#endif

/* The character used to fill ICMP echo requests, and therefore also the
character expected to fill ICMP echo replies. */
#define ipECHO_DATA_FILL_BYTE						'x'

#if( ipconfigBYTE_ORDER == pdFREERTOS_LITTLE_ENDIAN )
	/* The bits in the two byte IP header field that make up the fragment offset value. */
	#define ipFRAGMENT_OFFSET_BIT_MASK				( ( uint16_t ) 0xff0f )
#else
	/* The bits in the two byte IP header field that make up the fragment offset value. */
	#define ipFRAGMENT_OFFSET_BIT_MASK				( ( uint16_t ) 0x0fff )
#endif /* ipconfigBYTE_ORDER */

/* The maximum time the IP task is allowed to remain in the Blocked state if no
events are posted to the network event queue. */
#ifndef	ipconfigMAX_IP_TASK_SLEEP_TIME
	#define ipconfigMAX_IP_TASK_SLEEP_TIME ( 10000UL / portTICK_PERIOD_MS )
#endif

/* When a new TCP connection is established, the value of
'ulNextInitialSequenceNumber' will be used as the initial sequence number.  It
is very important that at start-up, 'ulNextInitialSequenceNumber' contains a
random value.  Also its value must be increased continuously in time, to prevent
a third party guessing the next sequence number and take-over a TCP connection.
It is advised to increment it by 1 ever 4us, which makes about 256 times
per ms: */
#define ipINITIAL_SEQUENCE_NUMBER_FACTOR	256UL

/* Returned as the (invalid) checksum when the protocol being checked is not
handled.  The value is chosen simply to be easy to spot when debugging. */
#define ipUNHANDLED_PROTOCOL 0x4321

/* Returned to indicate a valid checksum when the checksum does not need to be
calculated. */
#define ipCORRECT_CRC 0xffff

/* Returned as the (invalid) checksum when the length of the data being checked
had an invalid length. */
#define ipINVALID_LENGTH	0x1234

/*-----------------------------------------------------------*/

typedef struct xIP_TIMER
{
	uint32_t
		bActive : 1,	/* This timer is running and must be processed. */
		bExpired : 1;	/* Timer has expired and a task must be processed. */
	TimeOut_t xTimeOut;
	TickType_t ulRemainingTime;
	TickType_t ulReloadTime;
} IPTimer_t;

/* Used in checksum calculation. */
typedef union _xUnion32
{
	uint32_t u32;
	uint16_t u16[ 2 ];
	uint8_t u8[ 4 ];
} xUnion32;

/* Used in checksum calculation. */
typedef union _xUnionPtr
{
	uint32_t *u32ptr;
	uint16_t *u16ptr;
	uint8_t *u8ptr;
} xUnionPtr;

/*-----------------------------------------------------------*/

/*
 * The main TCP/IP stack processing task.  This task receives commands/events
 * from the network hardware drivers and tasks that are using sockets.  It also
 * maintains a set of protocol timers.
 */
static void prvIPTask( void *pvParameters );

/*
 * Called when new data is available from the network interface.
 */
static void prvProcessEthernetPacket( xNetworkBufferDescriptor_t * const pxNetworkBuffer );

/*
 * Process incoming IP packets.
 */
static eFrameProcessingResult_t prvProcessIPPacket( const xIPPacket_t * const pxIPPacket, xNetworkBufferDescriptor_t * const pxNetworkBuffer );

#if ( ipconfigREPLY_TO_INCOMING_PINGS == 1 ) || ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )
	/*
	 * Process incoming ICMP packets.
	 */
	static eFrameProcessingResult_t prvProcessICMPPacket( xICMPPacket_t * const pxICMPPacket );
#endif /* ( ipconfigREPLY_TO_INCOMING_PINGS == 1 ) || ( ipconfigSUPPORT_OUTGOING_PINGS == 1 ) */

/*
 * Turns around an incoming ping request to convert it into a ping reply.
 */
#if ( ipconfigREPLY_TO_INCOMING_PINGS == 1 )
	static eFrameProcessingResult_t prvProcessICMPEchoRequest( xICMPPacket_t * const pxICMPPacket );
#endif /* ipconfigREPLY_TO_INCOMING_PINGS */

/*
 * Processes incoming ping replies.  The application callback function
 * vApplicationPingReplyHook() is called with the results.
 */
#if ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )
	static void prvProcessICMPEchoReply( xICMPPacket_t * const pxICMPPacket );
#endif /* ipconfigSUPPORT_OUTGOING_PINGS */

/*
 * Called to create a network connection when the stack is first started, or
 * when the network connection is lost.
 */
static void prvProcessNetworkDownEvent( void );

/*
 * Checks the ARP, DHCP and TCP timers to see if any periodic or timeout
 * processing is required.
 */
static void prvCheckNetworkTimers( void );

/*
 * Determine how long the IP task can sleep for, which depends on when the next
 * periodic or timeout processing must be performed.
 */
static TickType_t prvCalculateSleepTime( void );

/*
 * The network card driver has received a packet.  In the case that it is part
 * of a linked packet chain, walk through it to handle every message.
 */
static void prvHandleEthernetPacket(struct xNETWORK_BUFFER *pxBuffer);

/*
 * Utility functions for the light weight IP timers.
 */
static void prvIPTimerStart( IPTimer_t *pxTimer, TickType_t xTime );
static BaseType_t prvIPTimerCheck( IPTimer_t *pxTimer );
static void prvIPTimerReload( IPTimer_t *pxTimer, TickType_t xTime );

/*-----------------------------------------------------------*/

/* The queue used to pass events into the IP-task for processing. */
QueueHandle_t xNetworkEventQueue = NULL;

/* uxRxEventCount is an estimate of the number of RX-events that have been
queued.  It is checked in FreeRTOS_TCP_IP, where, if it is non-zero, the driver
will stop sending multiple messages in order to give priority to incoming
data. */
/* _HT_ made it 'hidden public by supplying a read-only access function */
/*_RB_ Not sure I understand this comment as the variable cannot be made static. */
volatile UBaseType_t uxRxEventCount = 0;

/*_RB_ Requires comment. */
uint16_t usPacketIdentifier = 0U;

/* For convenience, a MAC address of all 0xffs is defined const for quick
reference. */
const xMACAddress_t xBroadcastMACAddress = { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };

/* Structure that stores the netmask, gateway address and DNS server addresses. */
xNetworkAddressingParameters_t xNetworkAddressing = { 0, 0, 0, 0, 0 };

/* Used to ensure network down events cannot be missed when they cannot be
posted to the network event queue because the network event queue is already
full. */
static BaseType_t xNetworkDownEventPending = pdFALSE;

/* Stores the handle of the task that handles the stack.  The handle is used
(indirectly) by some utility function to determine if the utility function is
being called by a task (in which case it is ok to block) or by the IP task
itself (in which case it is not ok to block). */
static TaskHandle_t xIPTaskHandle = NULL;

#if( ipconfigUSE_TCP != 0 )
	/* Set to a non-zero value if one or more TCP message have been processed
	within the last round. */
	static BaseType_t xProcessedTCPMessage;
#endif

/* Simple set to pdTRUE or pdFALSE depending on whether the network is up or
down (connected, not connected) respectively. */
static BaseType_t xNetworkUp = pdFALSE;

/*
A timer for each of the following processes, all of which need attention on a
regular basis:
	1. ARP, to check its table entries
	2. DPHC, to send requests and to renew a reservation
	3. TCP, to check for timeouts, resends
	4. DNS, to check for timeouts when looking-up a domain.
 */
static IPTimer_t xARPTimer;
#if( ipconfigUSE_DHCP != 0 )
	static IPTimer_t xDHCPTimer;
#endif
#if( ipconfigUSE_TCP != 0 )
	static IPTimer_t xTCPTimer;
#endif
#if( ipconfigDNS_USE_CALLBACKS != 0 )
	static IPTimer_t xDNSTimer;
#endif

/* Set to pdTRUE when the IP task is ready to start processing packets. */
static BaseType_t xIPTaskInitialised = pdFALSE;

/*-----------------------------------------------------------*/

static void prvIPTask( void *pvParameters )
{
xIPStackEvent_t xReceivedEvent;
TickType_t xNextIPSleep;
xFreeRTOS_Socket_t *pxSocket;
struct freertos_sockaddr xAddress;

	/* Just to prevent compiler warnings about unused parameters. */
	( void ) pvParameters;

	/* Generate a dummy message to say that the network connection has gone
	down.  This will cause this task to initialise the network interface.  After
	this it is the responsibility of the network interface hardware driver to
	send this message if a previously connected network is disconnected. */
	FreeRTOS_NetworkDown();

	#if( ipconfigUSE_TCP == 1 )
	{
		/* Initialise the TCP timer. */
		prvIPTimerReload( &xTCPTimer, ipTCP_TIMER_PERIOD_MS / portTICK_PERIOD_MS );
	}
	#endif

	/* Initialisation is complete and events can now be processed. */
	xIPTaskInitialised = pdTRUE;

	FreeRTOS_debug_printf( ( "prvIPTask started\n" ) );

	/* Loop, processing IP events. */
	for( ;; )
	{
		ipconfigWATCHDOG_TIMER();

		/* Check the ARP, DHCP and TCP timers to see if there is any periodic
		or timeout processing to perform. */
		prvCheckNetworkTimers();

		/* Calculate the acceptable maximum sleep time. */
		xNextIPSleep = prvCalculateSleepTime();

		/* Wait until there is something to do.  The event is initialised to "no
		event" in case the following call exits due to a time out rather than a
		message being received. */
		xReceivedEvent.eEventType = eNoEvent;
		xQueueReceive( xNetworkEventQueue, ( void * ) &xReceivedEvent, xNextIPSleep );

		iptraceNETWORK_EVENT_RECEIVED( xReceivedEvent.eEventType );

		switch( xReceivedEvent.eEventType )
		{
			case eNetworkDownEvent :
				/* Attempt to establish a connection. */
				xNetworkUp = pdFALSE;
				prvProcessNetworkDownEvent();
				break;

			case eNetworkRxEvent:
				/* The network hardware driver has received a new packet.  A
				pointer to the received buffer is located in the pvData member
				of the received event structure. */
				prvHandleEthernetPacket( ( xNetworkBufferDescriptor_t * ) ( xReceivedEvent.pvData ) );
				break;

			case eARPTimerEvent :
				/* The ARP timer has expired, process the ARP cache. */
				vARPAgeCache();
				break;

			case eSocketBindEvent:
				/* FreeRTOS_bind (a user API) wants the IP-task to bind a socket
				to a port. The port number is communicated in the socket field
				usLocPort. vSocketBind() will actually bind the socket and the
				API will unblock as soon as the eSOCKET_BOUND event is triggered*/
				pxSocket = ( xFreeRTOS_Socket_t * ) ( xReceivedEvent.pvData );
				xAddress.sin_addr = 0;	/* For the moment */
				xAddress.sin_port = FreeRTOS_ntohs( pxSocket->usLocPort );
				pxSocket->usLocPort = 0;
				vSocketBind( pxSocket, &xAddress, sizeof( xAddress ), pdFALSE );

				/* Before 'eSocketBindEvent' was sent it was tested that
				( xEventGroup != NULL ) so it can be used now to wake up the
				user. */
				pxSocket->xEventBits |= eSOCKET_BOUND;
				vWakeUpSocketUser( pxSocket );
				break;

			case eSocketCloseEvent :
				/* The user API FreeRTOS_closesocket() has sent a message to the
				IP-task to actually close a socket. This is handled in vSocketClose().
				As the socket gets closed, there is no way to report back to the API,
				so the API won't wait for the result */
				vSocketClose( ( xFreeRTOS_Socket_t * ) ( xReceivedEvent.pvData ) );
				break;

			case eStackTxEvent :
				/* The network stack has generated a packet to send.  A
				pointer to the generated buffer is located in the pvData
				member of the received event structure. */
				vProcessGeneratedUDPPacket( ( xNetworkBufferDescriptor_t * ) ( xReceivedEvent.pvData ) );
				break;

			case eDHCPEvent:
				/* The DHCP state machine needs processing. */
				#if( ipconfigUSE_DHCP == 1 )
				{
					vDHCPProcess( pdFALSE );
				}
				#endif /* ipconfigUSE_DHCP */
				break;

			case eSocketSelectEvent :
				/* FreeRTOS_select() has got unblocked by a socket event, vSocketSelect()
				will check which sockets actually have an event and update the socket field
				xSocketBits */
				#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
				{
					vSocketSelect( ( xSocketSelect_t * ) ( xReceivedEvent.pvData ) );
				}
				#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
				break;

			case eTCPTimerEvent :
				#if( ipconfigUSE_TCP == 1 )
				{
					/* Simply mark the TCP timer as expired so it gets processed
					the next time prvCheckNetworkTimers() is called. */
					xTCPTimer.bExpired = pdTRUE;
				}
				#endif /* ipconfigUSE_TCP */
				break;

			case eTCPAcceptEvent:
				/* The API FreeRTOS_accept() was called, the IP-task will now check
				if the listening socket (communicated in pvData) actually received a
				new connection */
				#if( ipconfigUSE_TCP == 1 )
				{
					pxSocket = ( xFreeRTOS_Socket_t * ) ( xReceivedEvent.pvData );

					if( xTCPCheckNewClient( pxSocket ) != pdFALSE )
					{
						pxSocket->xEventBits |= eSOCKET_ACCEPT;
						vWakeUpSocketUser( pxSocket );
					}
				}
				#endif /* ipconfigUSE_TCP */
				break;

			case eTCPNetStat:
				/* FreeRTOS_netstat() was called to have the IP-task print an
				overview of all sockets and their connections */
				#if( ( ipconfigUSE_TCP == 1 ) && ( ipconfigHAS_PRINTF == 1 ) )
				{
					vTCPNetStat();
				}
				#endif /* ipconfigUSE_TCP */
				break;

			default :
				/* Should not get here. */
				break;
		}

		if( xNetworkDownEventPending != pdFALSE )
		{
			/* A network down event could not be posted to the network event
			queue because the queue was full.  Try posting again. */
			FreeRTOS_NetworkDown();
		}
	}
}
/*-----------------------------------------------------------*/

BaseType_t xIsCallingFromIPTask( void )
{
BaseType_t xReturn;

	if( xTaskGetCurrentTaskHandle() == xIPTaskHandle )
	{
		xReturn = pdTRUE;
	}
	else
	{
		xReturn = pdFALSE;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static void prvHandleEthernetPacket(struct xNETWORK_BUFFER *pxBuffer)
{
	#if( ipconfigUSE_LINKED_RX_MESSAGES == 0 )
	{
		/* When ipconfigUSE_LINKED_RX_MESSAGES is not set to 0 then only one
		buffer will be sent at a time.  This is the default way for +TCP to pass
		messages from the MAC to the TCP/IP stack. */
		if( uxQueueMessagesWaiting( xNetworkEventQueue ) == 0 )
		{
			/* Ensure the estimate of Rx messages waiting is
			reset as there are no further messages waiting to be
			processed after this one */
			uxRxEventCount = 0;
		}
		else if( uxRxEventCount != 0 )
		{
			uxRxEventCount--;
		}

		prvProcessEthernetPacket( pxBuffer );
	}
	#else /* ipconfigUSE_LINKED_RX_MESSAGES */
	{
	struct xNETWORK_BUFFER *pxNextBuffer;

		/* An optimisation that is useful when there is high network traffic.
		Instead of passing received packets into the IP task one at a time the
		network interface can chain received packets together and pass them into
		the IP task in one go.  The packets are chained using the pxNextBuffer
		member.  The loop below walks through the chain processing each packet
		in the chain in turn. */
		do
		{
			/* Store a pointer to the buffer after pxBuffer for use later on. */
			pxNextBuffer = pxBuffer->pxNextBuffer;

			/* Make it NULL to avoid using it later on. */
			pxBuffer->pxNextBuffer = NULL;

			/* Set uxRxEventCount to indicate if more received data is waiting
			processing.  This is done to give priority of received data over the
			priority of data being generated to send. */
			if( pxNextBuffer == NULL )
			{
				/* Now that the last packet in the chain is being processed,
				decrease the RX event counter by 1, but just to be sure, check
				the number of queued message first. */
				if( uxQueueMessagesWaiting( xNetworkEventQueue ) == 0 )
				{
					/* Ensure the estimate of Rx messages waiting is reset as
					there are no further messages waiting to be	processed after
					this one. */
					uxRxEventCount = 0;
				}
				else if( uxRxEventCount != 0 )
				{
					uxRxEventCount--;
				}
			}

			prvProcessEthernetPacket( pxBuffer );
			pxBuffer = pxNextBuffer;

		/* While there is another packet in the chain. */
		} while( pxBuffer != NULL );
	}
	#endif /* ipconfigUSE_LINKED_RX_MESSAGES */
}
/*-----------------------------------------------------------*/

static TickType_t prvCalculateSleepTime( void )
{
TickType_t xMaximumSleepTime;

	/* Start with the maximum sleep time, then check this against the remaining
	time in any other timers that are active. */
	xMaximumSleepTime = ipconfigMAX_IP_TASK_SLEEP_TIME;

	if( xARPTimer.bActive != pdFALSE )
	{
		if( xARPTimer.ulRemainingTime < xMaximumSleepTime )
		{
			xMaximumSleepTime = xARPTimer.ulReloadTime;
		}
	}

	#if( ipconfigUSE_DHCP == 1 )
	{
		if( xDHCPTimer.bActive != pdFALSE )
		{
			if( xDHCPTimer.ulRemainingTime < xMaximumSleepTime )
			{
				xMaximumSleepTime = xDHCPTimer.ulRemainingTime;
			}
		}
	}
	#endif /* ipconfigUSE_DHCP */

	#if( ipconfigUSE_TCP == 1 )
	{
		if( xTCPTimer.ulRemainingTime < xMaximumSleepTime )
		{
			xMaximumSleepTime = xTCPTimer.ulRemainingTime;
		}
	}
	#endif

	#if( ipconfigDNS_USE_CALLBACKS != 0 )
	{
		if( xDNSTimer.bActive != pdFALSE )
		{
			if( xDNSTimer.ulRemainingTime < xMaximumSleepTime )
			{
				xMaximumSleepTime = xDNSTimer.ulRemainingTime;
			}
		}
	}
	#endif

	return xMaximumSleepTime;
}
/*-----------------------------------------------------------*/

static void prvCheckNetworkTimers( void )
{
	/* Is it time for ARP processing? */
	if( prvIPTimerCheck( &xARPTimer ) != pdFALSE )
	{
		xSendEventToIPTask( eARPTimerEvent );
	}

	#if( ipconfigUSE_DHCP == 1 )
	{
		/* Is it time for DHCP processing? */
		if( prvIPTimerCheck( &xDHCPTimer ) != pdFALSE )
		{
			xSendEventToIPTask( eDHCPEvent );
		}
	}
	#endif /* ipconfigUSE_DHCP */

	#if( ipconfigDNS_USE_CALLBACKS != 0 )
	{
	extern void vDNSCheckCallBack( void *pvSearchID );

		/* Is it time for DHCP processing? */
		if( prvIPTimerCheck( &xDNSTimer ) != pdFALSE )
		{
			vDNSCheckCallBack( NULL );
		}
	}
	#endif /* ipconfigDNS_USE_CALLBACKS */

	#if( ipconfigUSE_TCP == 1 )
	{
	BaseType_t xWillSleep;
	/* xStart keeps a copy of the last time this function was active,
	and during every call it will be updated with xTaskGetTickCount()
	'0' means: not yet initialised (although later '0' might be returned
	by xTaskGetTickCount(), which is no problem). */
	static TickType_t xStart = 0;
	TickType_t xTimeNow, xNextTime;
	BaseType_t xCheckTCPSockets;
	extern uint32_t ulNextInitialSequenceNumber;

		if( uxQueueMessagesWaiting( xNetworkEventQueue ) == 0 )
		{
			xWillSleep = pdTRUE;
		}
		else
		{
			xWillSleep = pdFALSE;
		}

		xTimeNow = xTaskGetTickCount();

		if( xStart != 0 )
		{
			/* It is advised to increment the Initial Sequence Number every 4
			microseconds which makes 250 times per ms.  This will make it harder
			for a third party to 'guess' our sequence number and 'take over'
			a TCP connection */
			ulNextInitialSequenceNumber += ipINITIAL_SEQUENCE_NUMBER_FACTOR * ( ( xTimeNow - xStart ) * portTICK_PERIOD_MS );
		}

		xStart = xTimeNow;

		/* Sockets need to be checked if the TCP timer has expired. */
		xCheckTCPSockets = prvIPTimerCheck( &xTCPTimer );

		/* Sockets will also be checked if there are TCP messages but the
		message queue is empty (indicated by xWillSleep being true). */
		if( ( xProcessedTCPMessage != pdFALSE ) && ( xWillSleep != pdFALSE ) )
		{
			xCheckTCPSockets = pdTRUE;
		}

		if( xCheckTCPSockets != pdFALSE )
		{
			/* Attend to the sockets, returning the period after which the
			check must be repeated. */
			xNextTime = xTCPTimerCheck( xWillSleep );
			prvIPTimerStart( &xTCPTimer, xNextTime );
			xProcessedTCPMessage = 0;
		}
	}
	#endif /* ipconfigUSE_TCP == 1 */
}
/*-----------------------------------------------------------*/

static void prvIPTimerStart( IPTimer_t *pxTimer, TickType_t xTime )
{
	vTaskSetTimeOutState( &pxTimer->xTimeOut );
	pxTimer->ulRemainingTime = xTime;
	pxTimer->bExpired = ( xTime == 0 );
	pxTimer->bActive = pdTRUE;
}
/*-----------------------------------------------------------*/

static void prvIPTimerReload( IPTimer_t *pxTimer, TickType_t xTime )
{
	pxTimer->ulReloadTime = xTime;
	prvIPTimerStart( pxTimer, xTime );
}
/*-----------------------------------------------------------*/

static BaseType_t prvIPTimerCheck( IPTimer_t *pxTimer )
{
BaseType_t xReturn;

	if( pxTimer->bActive == pdFALSE )
	{
		/* The timer is not enabled. */
		xReturn = pdFALSE;
	}
	else
	{
		/* The timer might have set the bExpired flag already, if not, check the
		value of xTimeOut against ulRemainingTime. */
		if( ( pxTimer->bExpired != pdFALSE ) ||
			( xTaskCheckForTimeOut( &( pxTimer->xTimeOut ), &( pxTimer->ulRemainingTime ) ) != pdFALSE ) )
		{
			prvIPTimerStart( pxTimer, pxTimer->ulReloadTime );
			xReturn = pdTRUE;
		}
		else
		{
			xReturn = pdFALSE;
		}
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

void FreeRTOS_NetworkDown( void )
{
static const xIPStackEvent_t xNetworkDownEvent = { eNetworkDownEvent, NULL };
const TickType_t xDontBlock = 0;

	/* Simply send the network task the appropriate event. */
	if( xSendEventStructToIPTask( &xNetworkDownEvent, xDontBlock ) != pdPASS )
	{
		/* Could not send the message, so it is still pending. */
		xNetworkDownEventPending = pdTRUE;
	}
	else
	{
		/* Message was sent so it is not pending. */
		xNetworkDownEventPending = pdFALSE;
	}

	iptraceNETWORK_DOWN();
}
/*-----------------------------------------------------------*/

BaseType_t FreeRTOS_NetworkDownFromISR( void )
{
static const xIPStackEvent_t xNetworkDownEvent = { eNetworkDownEvent, NULL };
BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	/* Simply send the network task the appropriate event. */
	if( xQueueSendToBackFromISR( xNetworkEventQueue, &xNetworkDownEvent, &xHigherPriorityTaskWoken ) != pdPASS )
	{
		xNetworkDownEventPending = pdTRUE;
	}
	else
	{
		xNetworkDownEventPending = pdFALSE;
	}

	iptraceNETWORK_DOWN();

	return xHigherPriorityTaskWoken;
}
/*-----------------------------------------------------------*/

void *FreeRTOS_GetUDPPayloadBuffer( size_t xRequestedSizeBytes, TickType_t xBlockTimeTicks )
{
xNetworkBufferDescriptor_t *pxNetworkBuffer;
void *pvReturn;

	/* Cap the block time.  The reason for this is explained where
	ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS is defined (assuming an official
	FreeRTOSIPConfig.h header file is being used). */
	if( xBlockTimeTicks > ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS )
	{
		xBlockTimeTicks = ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS;
	}

	/* Obtain a network buffer with the required amount of storage. */
	pxNetworkBuffer = pxGetNetworkBufferWithDescriptor( sizeof( xUDPPacket_t ) + xRequestedSizeBytes, xBlockTimeTicks );

	if( pxNetworkBuffer != NULL )
	{
		/* Leave space for the UPD header. */
		pvReturn = ( void * ) &( pxNetworkBuffer->pucEthernetBuffer[ ipUDP_PAYLOAD_OFFSET ] );
	}
	else
	{
		pvReturn = NULL;
	}

	return ( void * ) pvReturn;
}
/*-----------------------------------------------------------*/

xNetworkBufferDescriptor_t *pxDuplicateNetworkBufferWithDescriptor( xNetworkBufferDescriptor_t * const pxNetworkBuffer,
	BaseType_t xNewLength )
{
xNetworkBufferDescriptor_t * pxNewBuffer;

	/* This function is only used when 'ipconfigZERO_COPY_TX_DRIVER'.
	The transmit routine wants to have ownership of the network buffer
	descriptor,	because it will pass the buffer straight to DMA. */
	pxNewBuffer = pxGetNetworkBufferWithDescriptor( ( size_t ) xNewLength, 0 );

	if( pxNewBuffer != NULL )
	{
		pxNewBuffer->ulIPAddress = pxNetworkBuffer->ulIPAddress;
		pxNewBuffer->usPort = pxNetworkBuffer->usPort;
		pxNewBuffer->usBoundPort = pxNetworkBuffer->usBoundPort;
		memcpy( pxNewBuffer->pucEthernetBuffer, pxNetworkBuffer->pucEthernetBuffer, pxNetworkBuffer->xDataLength );
	}

	return pxNewBuffer;
}
/*-----------------------------------------------------------*/

#if( ipconfigZERO_COPY_TX_DRIVER != 0 ) || ( ipconfigZERO_COPY_RX_DRIVER != 0 )

	xNetworkBufferDescriptor_t *pxPacketBuffer_to_NetworkBuffer( void *pvBuffer )
	{
	uint8_t *pucBuffer;
	xNetworkBufferDescriptor_t *pxResult;

		if( pvBuffer == NULL )
		{
			pxResult = NULL;
		}
		else
		{
			/* Obtain the network buffer from the zero copy pointer. */
			pucBuffer = ( uint8_t * ) pvBuffer;

			/* The input here is a pointer to a payload buffer.  Subtract the
			size of the header in the network buffer, usually 8 + 2 bytes. */
			pucBuffer -= ipBUFFER_PADDING;

			/* Here a pointer was placed to the network descriptor.  As a
			pointer is dereferenced, make sure it is well aligned. */
			if( ( ( ( uint32_t ) pucBuffer ) & ( sizeof( pucBuffer ) - 1 ) ) == 0 )
			{
				pxResult = * ( ( xNetworkBufferDescriptor_t ** ) pucBuffer );
			}
			else
			{
				pxResult = NULL;
			}
		}

		return pxResult;
	}

#endif /* ipconfigZERO_COPY_TX_DRIVER != 0 */
/*-----------------------------------------------------------*/

xNetworkBufferDescriptor_t *pxUDPPayloadBuffer_to_NetworkBuffer( void *pvBuffer )
{
uint8_t *pucBuffer;
xNetworkBufferDescriptor_t *pxResult;

	if( pvBuffer == NULL )
	{
		pxResult = NULL;
	}
	else
	{
		/* Obtain the network buffer from the zero copy pointer. */
		pucBuffer = ( uint8_t * ) pvBuffer;

		/* The input here is a pointer to a payload buffer.  Subtract
		the total size of a UDP/IP header plus the the size of the header in
		the network buffer, usually 8 + 2 bytes. */
		pucBuffer -= ( sizeof( xUDPPacket_t ) + ipBUFFER_PADDING );

		/* Here a pointer was placed to the network descriptor,
		As a pointer is dereferenced, make sure it is well aligned */
		if( ( ( ( uint32_t ) pucBuffer ) & ( sizeof( pucBuffer ) - 1 ) ) == 0 )
		{
			pxResult = * ( ( xNetworkBufferDescriptor_t ** ) pucBuffer );
		}
		else
		{
			pxResult = NULL;
		}
	}

	return pxResult;
}
/*-----------------------------------------------------------*/

void FreeRTOS_ReleaseUDPPayloadBuffer( void *pvBuffer )
{
	vReleaseNetworkBufferAndDescriptor( pxUDPPayloadBuffer_to_NetworkBuffer( pvBuffer ) );
}
/*-----------------------------------------------------------*/

BaseType_t FreeRTOS_IPInit( const uint8_t ucIPAddress[ ipIP_ADDRESS_LENGTH_BYTES ], const uint8_t ucNetMask[ ipIP_ADDRESS_LENGTH_BYTES ], const uint8_t ucGatewayAddress[ ipIP_ADDRESS_LENGTH_BYTES ], const uint8_t ucDNSServerAddress[ ipIP_ADDRESS_LENGTH_BYTES ], const uint8_t ucMACAddress[ ipMAC_ADDRESS_LENGTH_BYTES ] )
{
BaseType_t xReturn = pdFALSE;

	/* This function should only be called once. */
	configASSERT( xIPIsNetworkTaskReady() == pdFALSE );
	configASSERT( xNetworkEventQueue == NULL );
	configASSERT( xIPTaskHandle == NULL );

	/* Attempt to create the queue used to communicate with the IP task. */
	xNetworkEventQueue = xQueueCreate( ( UBaseType_t ) ipconfigEVENT_QUEUE_LENGTH, ( UBaseType_t )sizeof( xIPStackEvent_t ) );
	configASSERT( xNetworkEventQueue );

	if( xNetworkEventQueue != NULL )
	{
		#if ( configQUEUE_REGISTRY_SIZE > 0 )
		{
			vQueueAddToRegistry( xNetworkEventQueue, "NetEvnt" );
		}
		#endif /* configQUEUE_REGISTRY_SIZE */

		if( xNetworkBuffersInitialise() == pdPASS )
		{
			/* Store the local IP and MAC address. */
			xNetworkAddressing.ulDefaultIPAddress = FreeRTOS_inet_addr_quick( ucIPAddress[ 0 ], ucIPAddress[ 1 ], ucIPAddress[ 2 ], ucIPAddress[ 3 ] );
			xNetworkAddressing.ulNetMask = FreeRTOS_inet_addr_quick( ucNetMask[ 0 ], ucNetMask[ 1 ], ucNetMask[ 2 ], ucNetMask[ 3 ] );
			xNetworkAddressing.ulGatewayAddress = FreeRTOS_inet_addr_quick( ucGatewayAddress[ 0 ], ucGatewayAddress[ 1 ], ucGatewayAddress[ 2 ], ucGatewayAddress[ 3 ] );
			xNetworkAddressing.ulDNSServerAddress = FreeRTOS_inet_addr_quick( ucDNSServerAddress[ 0 ], ucDNSServerAddress[ 1 ], ucDNSServerAddress[ 2 ], ucDNSServerAddress[ 3 ] );
			xNetworkAddressing.ulBroadcastAddress = ( xNetworkAddressing.ulDefaultIPAddress & xNetworkAddressing.ulNetMask ) |  ~xNetworkAddressing.ulNetMask;

			#if ipconfigUSE_DHCP == 1
			{
				/* The IP address is not set until DHCP completes. */
				*ipLOCAL_IP_ADDRESS_POINTER = 0x00UL;
			}
			#else
			{
				/* The IP address is set from the value passed in. */
				*ipLOCAL_IP_ADDRESS_POINTER = xNetworkAddressing.ulDefaultIPAddress;

				/* Added to prevent ARP flood to gateway.  Ensure the
				gateway is on the same subnet as the IP	address. */
				configASSERT( ( ( *ipLOCAL_IP_ADDRESS_POINTER ) & xNetworkAddressing.ulNetMask ) == ( xNetworkAddressing.ulGatewayAddress & xNetworkAddressing.ulNetMask ) );
			}
			#endif /* ipconfigUSE_DHCP == 1 */

			/* The MAC address is stored in the start of the default packet
			header fragment, which is used when sending UDP packets. */
			memcpy( ( void * ) ipLOCAL_MAC_ADDRESS, ( void * ) ucMACAddress, ( size_t ) ipMAC_ADDRESS_LENGTH_BYTES );

			/* Prepare the sockets interface. */
			vNetworkSocketsInit();

			/* Create the task that processes Ethernet and stack events. */
			xReturn = xTaskCreate( prvIPTask, "IP-task", ( uint16_t ) ipconfigIP_TASK_STACK_SIZE_WORDS, NULL, ( UBaseType_t ) ipconfigIP_TASK_PRIORITY, &xIPTaskHandle );
		}
		else
		{
			FreeRTOS_debug_printf( ( "FreeRTOS_IPInit: xNetworkBuffersInitialise() failed\n") );

			/* Clean up. */
			vQueueDelete( xNetworkEventQueue );
			xNetworkEventQueue = NULL;
		}
	}
	else
	{
		FreeRTOS_debug_printf( ( "FreeRTOS_IPInit: Network event queue could not be created\n") );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

void FreeRTOS_GetAddressConfiguration( uint32_t *pulIPAddress, uint32_t *pulNetMask, uint32_t *pulGatewayAddress, uint32_t *pulDNSServerAddress )
{
	/* Return the address configuration to the caller. */

	if( pulIPAddress != NULL )
	{
		*pulIPAddress = *ipLOCAL_IP_ADDRESS_POINTER;
	}

	if( pulNetMask != NULL )
	{
		*pulNetMask = xNetworkAddressing.ulNetMask;
	}

	if( pulGatewayAddress != NULL )
	{
		*pulGatewayAddress = xNetworkAddressing.ulGatewayAddress;
	}

	if( pulDNSServerAddress != NULL )
	{
		*pulDNSServerAddress = xNetworkAddressing.ulDNSServerAddress;
	}
}
/*-----------------------------------------------------------*/

void FreeRTOS_SetAddressConfiguration( const uint32_t *pulIPAddress, const uint32_t *pulNetMask, const uint32_t *pulGatewayAddress, const uint32_t *pulDNSServerAddress )
{
	/* Update the address configuration. */

	if( pulIPAddress != NULL )
	{
		*ipLOCAL_IP_ADDRESS_POINTER = *pulIPAddress;
	}

	if( pulNetMask != NULL )
	{
		xNetworkAddressing.ulNetMask = *pulNetMask;
	}

	if( pulGatewayAddress != NULL )
	{
		xNetworkAddressing.ulGatewayAddress = *pulGatewayAddress;
	}

	if( pulDNSServerAddress != NULL )
	{
		xNetworkAddressing.ulDNSServerAddress = *pulDNSServerAddress;
	}
}
/*-----------------------------------------------------------*/

#if ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )

	BaseType_t FreeRTOS_SendPingRequest( uint32_t ulIPAddress, size_t xNumberOfBytesToSend, TickType_t xBlockTimeTicks )
	{
	xNetworkBufferDescriptor_t *pxNetworkBuffer;
	xICMPHeader_t *pxICMPHeader;
	BaseType_t xReturn = pdFAIL;
	static uint16_t usSequenceNumber = 0;
	uint8_t *pucChar;
	xIPStackEvent_t xStackTxEvent = { eStackTxEvent, NULL };

		if( xNumberOfBytesToSend < ( ( ipconfigNETWORK_MTU - sizeof( xIPHeader_t ) ) - sizeof( xICMPHeader_t ) ) )
		{
			pxNetworkBuffer = pxGetNetworkBufferWithDescriptor( xNumberOfBytesToSend + sizeof( xICMPPacket_t ), xBlockTimeTicks );

			if( pxNetworkBuffer != NULL )
			{
				pxICMPHeader = ( xICMPHeader_t * ) &( pxNetworkBuffer->pucEthernetBuffer[ ipIP_PAYLOAD_OFFSET ] );
				usSequenceNumber++;

				/* Fill in the basic header information. */
				pxICMPHeader->ucTypeOfMessage = ipICMP_ECHO_REQUEST;
				pxICMPHeader->ucTypeOfService = 0;
				pxICMPHeader->usIdentifier = usSequenceNumber;
				pxICMPHeader->usSequenceNumber = usSequenceNumber;

				/* Find the start of the data. */
				pucChar = ( uint8_t * ) pxICMPHeader;
				pucChar += sizeof( xICMPHeader_t );

				/* Just memset the data to a fixed value. */
				memset( ( void * ) pucChar, ( int ) ipECHO_DATA_FILL_BYTE, xNumberOfBytesToSend );

				/* The message is complete, IP and checksum's are handled by
				vProcessGeneratedUDPPacket */
				pxNetworkBuffer->pucEthernetBuffer[ ipSOCKET_OPTIONS_OFFSET ] = FREERTOS_SO_UDPCKSUM_OUT;
				pxNetworkBuffer->ulIPAddress = ulIPAddress;
				pxNetworkBuffer->usPort = ipPACKET_CONTAINS_ICMP_DATA;
				pxNetworkBuffer->xDataLength = xNumberOfBytesToSend + sizeof( xICMPHeader_t );

				/* Send to the stack. */
				xStackTxEvent.pvData = pxNetworkBuffer;

				if( xSendEventStructToIPTask( &xStackTxEvent, xBlockTimeTicks) != pdPASS )
				{
					vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
					iptraceSTACK_TX_EVENT_LOST( ipSTACK_TX_EVENT );
				}
				else
				{
					xReturn = usSequenceNumber;
				}
			}
		}
		else
		{
			/* The requested number of bytes will not fit in the available space
			in the network buffer. */
		}

		return xReturn;
	}

#endif /* ipconfigSUPPORT_OUTGOING_PINGS == 1 */
/*-----------------------------------------------------------*/

BaseType_t xSendEventToIPTask( eIPEvent_t eEvent )
{
xIPStackEvent_t xEventMessage;
const TickType_t xDontBlock = 0;

	xEventMessage.eEventType = eEvent;
	xEventMessage.pvData = ( void* )NULL;

	return xSendEventStructToIPTask( &xEventMessage, xDontBlock );
}
/*-----------------------------------------------------------*/

BaseType_t xSendEventStructToIPTask( const xIPStackEvent_t *pxEvent, TickType_t xTimeout )
{
BaseType_t xReturn, xSendMessage;

	if( ( xIPIsNetworkTaskReady() == pdFALSE ) && ( pxEvent->eEventType != eNetworkDownEvent ) )
	{
		/* Only allow eNetworkDownEvent events if the IP task is not ready
		yet.  Not going to attempt to send the message so the send failed. */
		xReturn = pdFAIL;
	}
	else
	{
		xSendMessage = pdTRUE;

		#if( ipconfigUSE_TCP == 1 )
		{
			if( pxEvent->eEventType == eTCPTimerEvent )
			{
				/* TCP timer events are sent to wake the timer task when
				xTCPTimer has expired, but there is no point sending them if the
				IP task is already awake processing other message. */
				xTCPTimer.bExpired = pdTRUE;

				if( uxQueueMessagesWaiting( xNetworkEventQueue ) != 0 )
				{
					/* Not actually going to send the message but this is not a
					failure as the message didn't need to be sent. */
					xSendMessage = pdFALSE;
				}
			}
		}
		#endif /* ipconfigUSE_TCP */

		if( pxEvent->eEventType == eNetworkRxEvent )
		{
			/* Latch the number of received frames. */
			uxRxEventCount++;
		}

		if( xSendMessage != pdFALSE )
		{
			/* The IP task cannot block itself while waiting for itself to
			respond. */
			configASSERT( ( ( xIsCallingFromIPTask() == pdTRUE ) && ( xTimeout > 0 ) ) == pdFALSE );
			if( ( xIsCallingFromIPTask() == pdTRUE ) && ( xTimeout > 0 ) )
			{
				xTimeout = 0;
			}

			xReturn = xQueueSendToBack( xNetworkEventQueue, pxEvent, xTimeout );

			if( xReturn == pdFAIL )
			{
				/* A message should have been sent to the IP task, but wasn't. */
				if( pxEvent->eEventType == eNetworkRxEvent )
				{
					uxRxEventCount--;
				}

				FreeRTOS_debug_printf( ( "xSendEventStructToIPTask: CAN NOT ADD %d\n", pxEvent->eEventType ) );
				iptraceSTACK_TX_EVENT_LOST( pxEvent->eEventType );
			}
		}
		else
		{
			/* It was not necessary to send the message to process the event so
			even though the message was not sent the call was successful. */
			xReturn = pdPASS;
		}
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

/* HT questionable */
/*#if ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0 */

eFrameProcessingResult_t eConsiderFrameForProcessing( const uint8_t * const pucEthernetBuffer )
{
eFrameProcessingResult_t eReturn;
const xEthernetHeader_t *pxEthernetHeader;

	pxEthernetHeader = ( const xEthernetHeader_t * ) pucEthernetBuffer;

	if( memcmp( ( void * ) xBroadcastMACAddress.ucBytes, ( void * ) &( pxEthernetHeader->xDestinationAddress ), sizeof( xMACAddress_t ) ) == 0 )
	{
		/* The packet was a broadcast - process it. */
		eReturn = eProcessBuffer;
	}
	else if( memcmp( ( void * ) ipLOCAL_MAC_ADDRESS, ( void * ) &( pxEthernetHeader->xDestinationAddress ), sizeof( xMACAddress_t ) ) == 0 )
	{
		/* The packet was to this node directly - process it. */
		eReturn = eProcessBuffer;
	}
	else
#if( ipconfigUSE_LLMNR == 1 )
	if( memcmp( ( void * ) xLLMNR_MacAdress.ucBytes, ( void * ) &( pxEthernetHeader->xDestinationAddress ), sizeof( xMACAddress_t ) ) == 0 )
	{
		/* The packet is a request for LLMNR - process it. */
		eReturn = eProcessBuffer;
	}
	else
#endif /* ipconfigUSE_LLMNR */
	{
		/* The packet was not a broadcast, or for this node, just release
		the buffer without taking any other action. */
		eReturn = eReleaseBuffer;
	}

	#if( ipconfigFILTER_OUT_NON_ETHERNET_II_FRAMES == 1 )
	{
	uint16_t usFrameType;

		if( eReturn == eProcessBuffer )
		{
			usFrameType = pxEthernetHeader->usFrameType;
			usFrameType = FreeRTOS_ntohs( usFrameType );

			if( usFrameType <= 0x600U )
			{
				/* Not an Ethernet II frame. */
				eReturn = eReleaseBuffer;
			}
		}
	}
	#endif /* ipconfigFILTER_OUT_NON_ETHERNET_II_FRAMES == 1  */

	return eReturn;
}
/* HT questionable */
/*#endif // ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES */
/*-----------------------------------------------------------*/

static void prvProcessNetworkDownEvent( void )
{
	/* Stop the ARP timer while there is no network. */
	xARPTimer.bActive = pdFALSE;

	#if ipconfigUSE_NETWORK_EVENT_HOOK == 1
	{
		static BaseType_t xCallEventHook = pdFALSE;

		/* The first network down event is generated by the IP stack itself to
		initialise the network hardware, so do not call the network down event
		the first time through. */
		if( xCallEventHook == pdTRUE )
		{
			vApplicationIPNetworkEventHook( eNetworkDown );
		}
		xCallEventHook = pdTRUE;
	}
	#endif

	/* The network has been disconnected (or is being initialised for the first
	time).  Perform whatever hardware processing is necessary to bring it up
	again, or wait for it to be available again.  This is hardware dependent. */
	if( xNetworkInterfaceInitialise() != pdPASS )
	{
		/* Ideally the network interface initialisation function will only
		return when the network is available.  In case this is not the case,
		wait a while before retrying the initialisation. */
		vTaskDelay( ipINITIALISATION_RETRY_DELAY );
		FreeRTOS_NetworkDown();
	}
	else
	{
		/* Set remaining time to 0 so it will become active immediately. */
		#if ipconfigUSE_DHCP == 1
		{
			/* The network is not up until DHCP has completed. */
			vDHCPProcess( pdTRUE );
			xSendEventToIPTask( eDHCPEvent );
		}
		#else
		{
			/* Perform any necessary 'network up' processing. */
			vIPNetworkUpCalls();
		}
		#endif
	}
}
/*-----------------------------------------------------------*/

void vIPNetworkUpCalls( void )
{
	xNetworkUp = pdTRUE;

	#if ipconfigUSE_NETWORK_EVENT_HOOK == 1
	{
		vApplicationIPNetworkEventHook( eNetworkUp );
	}
	#endif /* ipconfigUSE_NETWORK_EVENT_HOOK */

	/* Static configuration is being used, so the network is now up. */
	#if ipconfigFREERTOS_PLUS_NABTO == 1
	{
		vStartNabtoTask();
	}
	#endif /* ipconfigFREERTOS_PLUS_NABTO */

	#if( ipconfigDNS_USE_CALLBACKS != 0 )
	{
		/* The following function is declared in FreeRTOS_DNS.c	and 'private' to
		this library */
		extern void vDNSInitialise( void );
		vDNSInitialise();
	}
	#endif /* ipconfigDNS_USE_CALLBACKS != 0 */

	/* Set remaining time to 0 so it will become active immediately. */
	prvIPTimerReload( &xARPTimer, ipARP_TIMER_PERIOD_MS / portTICK_PERIOD_MS );
}
/*-----------------------------------------------------------*/

static void prvProcessEthernetPacket( xNetworkBufferDescriptor_t * const pxNetworkBuffer )
{
xEthernetHeader_t *pxEthernetHeader;
volatile eFrameProcessingResult_t eReturned; /* Volatile to prevent complier warnings when ipCONSIDER_FRAME_FOR_PROCESSING just sets it to eProcessBuffer. */

	configASSERT( pxNetworkBuffer );

	/* Interpret the Ethernet frame. */
	eReturned = ipCONSIDER_FRAME_FOR_PROCESSING( pxNetworkBuffer->pucEthernetBuffer );
	pxEthernetHeader = ( xEthernetHeader_t * ) ( pxNetworkBuffer->pucEthernetBuffer );

	if( eReturned == eProcessBuffer )
	{
		/* Interpret the received Ethernet packet. */
		switch( pxEthernetHeader->usFrameType )
		{
			case ipARP_TYPE	:
				/* The Ethernet frame contains an ARP packet. */
				eReturned = eARPProcessPacket( ( xARPPacket_t * ) pxNetworkBuffer->pucEthernetBuffer );
				break;

			case ipIP_TYPE	:
				/* The Ethernet frame contains an IP packet. */
				eReturned = prvProcessIPPacket( ( xIPPacket_t * ) pxNetworkBuffer->pucEthernetBuffer, pxNetworkBuffer );
				break;

			default :
				/* No other packet types are handled.  Nothing to do. */
				eReturned = eReleaseBuffer;
				break;
		}
	}

	/* Perform any actions that resulted from processing the Ethernet frame. */
	switch( eReturned )
	{
		case eReturnEthernetFrame :
			/* The Ethernet frame will have been updated (maybe it was
			an ARP request or a PING request?) and should be sent back to
			its source. */
			vReturnEthernetFrame( pxNetworkBuffer, pdTRUE );
			/* parameter pdTRUE: the buffer must be released once
			the frame has been transmitted */
			break;

		case eFrameConsumed :
			/* The frame is in use somewhere, don't release the buffer
			yet. */
			break;

		default :
			/* The frame is not being used anywhere, and the
			xNetworkBufferDescriptor_t structure containing the frame should
			just be	released back to the list of free buffers. */
			vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
			break;
	}
}
/*-----------------------------------------------------------*/

static eFrameProcessingResult_t prvAllowIPPacket( const xIPPacket_t * const pxIPPacket,
	xNetworkBufferDescriptor_t * const pxNetworkBuffer, BaseType_t lHeaderLength )
{
eFrameProcessingResult_t eReturn = eProcessBuffer;

#if( ( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 0 ) || ( ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM == 0 ) )
	const xIPHeader_t * pxIPHeader = &( pxIPPacket->xIPHeader );
#else
	/* or else, the parameter won't be used and the function will be optimised
	away */
	( void ) pxIPPacket;
#endif

	#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 0 )
	{
		/* In systems with a very small amount of RAM, it might be advantageous
		to have incoming messages checked earlier, by the network card driver.
		This method may decrease the usage of sparse network buffers. */
		uint32_t ulDestinationIPAddress = pxIPHeader->ulDestinationIPAddress;

			/* Ensure that the incoming packet is not fragmented (only outgoing
			packets can be fragmented) as these are the only handled IP frames
			currently. */
			if( ( pxIPHeader->usFragmentOffset & ipFRAGMENT_OFFSET_BIT_MASK ) != 0U )
			{
				/* Can not handle, fragmented packet. */
				eReturn = eReleaseBuffer;
			}
			/* 0x45 means: IPv4 with an IP header of 5 x 4 = 20 bytes
			 * 0x47 means: IPv4 with an IP header of 7 x 4 = 28 bytes */
			else if( ( pxIPHeader->ucVersionHeaderLength < 0x45 ) || ( pxIPHeader->ucVersionHeaderLength > 0x4F ) )
			{
				/* Can not handle, unknown or invalid header version. */
				eReturn = eReleaseBuffer;
			}
				/* Is the packet for this IP address? */
			else if( ( ulDestinationIPAddress != *ipLOCAL_IP_ADDRESS_POINTER ) &&
				/* Is it the global broadcast address 255.255.255.255 ? */
				( ulDestinationIPAddress != ipBROADCAST_IP_ADDRESS ) &&
				/* Is it a specific broadcast address 192.168.1.255 ? */
				( ulDestinationIPAddress != xNetworkAddressing.ulBroadcastAddress ) &&
			#if( ipconfigUSE_LLMNR == 1 )
				/* Is it the LLMNR multicast address? */
				( ulDestinationIPAddress != ipLLMNR_IP_ADDR ) &&
			#endif
				/* Or (during DHCP negotiation) we have no IP-address yet? */
				( *ipLOCAL_IP_ADDRESS_POINTER != 0 ) )
			{
				/* Packet is not for this node, release it */
				eReturn = eReleaseBuffer;
			}
	}
	#endif /* ipconfigETHERNET_DRIVER_FILTERS_PACKETS */

	#if( ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM == 0 )
	{
		/* Some drivers of NIC's with checksum-offloading will enable the above
		define, so that the checksum won't be checked again here */
		if (eReturn == eProcessBuffer )
		{
			/* Is the IP header checksum correct? */
			if( pxIPHeader->ucProtocol != ipPROTOCOL_ICMP &&
				usGenerateChecksum( 0UL, ( uint8_t * ) &( pxIPHeader->ucVersionHeaderLength ), lHeaderLength )  != 0xffffU )
			{
				/* Check sum in IP-header not correct. */
				eReturn = eReleaseBuffer;
			}
			/* Is the upper-layer checksum (TCP/UDP/ICMP) correct? */
			else if( usGenerateProtocolChecksum( ( uint8_t * )( pxNetworkBuffer->pucEthernetBuffer ), pdFALSE ) != 0xffff )
			{
				/* Protocol checksum not accepted. */
				eReturn = eReleaseBuffer;
			}
		}
	}
	#else
	{
		/* to avoid warning unused parameters */
		( void ) pxNetworkBuffer;
		( void ) lHeaderLength;
	}
	#endif /* ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM == 0 */

	return eReturn;
}
/*-----------------------------------------------------------*/

static eFrameProcessingResult_t prvProcessIPPacket( const xIPPacket_t * const pxIPPacket, xNetworkBufferDescriptor_t * const pxNetworkBuffer )
{
eFrameProcessingResult_t eReturn;
const xIPHeader_t * pxIPHeader = &( pxIPPacket->xIPHeader );
BaseType_t lHeaderLength = ( pxIPHeader->ucVersionHeaderLength & 0x0F) << 2;

	/* Check if the IP headers are acceptable and if it has our destination. */
	eReturn = prvAllowIPPacket( pxIPPacket, pxNetworkBuffer, lHeaderLength );

	if( eReturn == eProcessBuffer )
	{
		if( lHeaderLength > ipSIZE_OF_IP_HEADER )
		{
			/* All structs of headers expect a IP header size of 20 bytes
			 * IP header options were included, we'll ignore them and cut them out
			 * Note: IP options are mostly use in Multi-cast protocols */
			const size_t optlen = ( ( size_t ) lHeaderLength ) - ipSIZE_OF_IP_HEADER;
			/* From: the previous start of UDP/ICMP/TCP data */
			char *pucSource = ((char*)pxIPHeader) + lHeaderLength;
			/* To: the usual start of UDP/ICMP/TCP data at offset 20 from IP header */
			char *pucTarget = ((char*)pxIPHeader) + ipSIZE_OF_IP_HEADER;
			/* How many: total length minus the options and the lower headers */
			const size_t  xMoveLen = pxNetworkBuffer->xDataLength - optlen - ipSIZE_OF_IP_HEADER - ipSIZE_OF_ETH_HEADER;

			memmove( pucTarget, pucSource, xMoveLen );
			pxNetworkBuffer->xDataLength -= optlen;
		}
		/* Add the IP and MAC addresses to the ARP table if they are not
		already there - otherwise refresh the age of the existing
		entry. */
		if( pxIPHeader->ucProtocol != ipPROTOCOL_UDP )
		{
			/* Refresh the ARP cache with the IP/MAC-address of the received packet
			 * For UDP packets, this will be done later in xProcessReceivedUDPPacket()
			 * as soon as know that the message will be handled by someone
			 * This will prevent that the ARP cache will get overwritten
			 * with the IP-address of useless broadcast packets
			 */
			vARPRefreshCacheEntry( &( pxIPPacket->xEthernetHeader.xSourceAddress ), pxIPHeader->ulSourceIPAddress );
		}
		switch( pxIPHeader->ucProtocol )
		{
			case ipPROTOCOL_ICMP :
				/* The IP packet contained an ICMP frame.  Don't bother
				checking the ICMP checksum, as if it is wrong then the
				wrong data will also be returned, and the source of the
				ping will know something went wrong because it will not
				be able to validate what it receives. */
				#if ( ipconfigREPLY_TO_INCOMING_PINGS == 1 ) || ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )
				{
					xICMPPacket_t *pxICMPPacket = ( xICMPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
					/* HT:endian: *ipLOCAL_IP_ADDRESS_POINTER had network order */
					if( pxIPHeader->ulDestinationIPAddress == *ipLOCAL_IP_ADDRESS_POINTER )
					{
						eReturn = prvProcessICMPPacket( pxICMPPacket );
					}
				}
				#endif /* ( ipconfigREPLY_TO_INCOMING_PINGS == 1 ) || ( ipconfigSUPPORT_OUTGOING_PINGS == 1 ) */
				break;

			case ipPROTOCOL_UDP :
				{
					/* The IP packet contained a UDP frame. */
					xUDPPacket_t *pxUDPPacket = ( xUDPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );

					/* Note the header values required prior to the
					checksum generation as the checksum pseudo header
					may clobber some of these values. */
					pxNetworkBuffer->xDataLength = FreeRTOS_ntohs( pxUDPPacket->xUDPHeader.usLength ) - sizeof( xUDPHeader_t );
					/* HT:endian: fields in pxNetworkBuffer (usPort, ulIPAddress) were network order */
					pxNetworkBuffer->usPort = pxUDPPacket->xUDPHeader.usSourcePort;
					pxNetworkBuffer->ulIPAddress = pxUDPPacket->xIPHeader.ulSourceIPAddress;

					/* ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM:
					 * In some cases, the upper-layer checksum has been calculated
					 * by the NIC driver */
					/* Pass the packet payload to the UDP sockets implementation. */
					/* HT:endian: xProcessReceivedUDPPacket wanted network order */
					if( xProcessReceivedUDPPacket( pxNetworkBuffer, pxUDPPacket->xUDPHeader.usDestinationPort ) == pdPASS )
					{
						eReturn = eFrameConsumed;
					}
				}
				break;

#if ipconfigUSE_TCP == 1
			case ipPROTOCOL_TCP :
				{

					if( xProcessReceivedTCPPacket( pxNetworkBuffer ) == pdPASS )
					{
						eReturn = eFrameConsumed;
					}

					/* Setting this variable will cause xTCPTimerCheck()
					to be called just before the IP-task blocks. */
					xProcessedTCPMessage++;
				}
				break;
#endif
			default	:
				/* Not a supported frame type. */
				break;
		}
	}

	return eReturn;
}
/*-----------------------------------------------------------*/

#if ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )

	static void prvProcessICMPEchoReply( xICMPPacket_t * const pxICMPPacket )
	{
	ePingReplyStatus_t eStatus = eSuccess;
	uint16_t usDataLength, usCount;
	uint8_t *pucByte;

		/* Find the total length of the IP packet. */
		usDataLength = pxICMPPacket->xIPHeader.usLength;
		usDataLength = FreeRTOS_ntohs( usDataLength );

		/* Remove the length of the IP headers to obtain the length of the ICMP
		message itself. */
		usDataLength = ( uint16_t ) ( ( ( uint32_t ) usDataLength ) - ipSIZE_OF_IP_HEADER );

		/* Remove the length of the ICMP header, to obtain the length of
		data contained in the ping. */
		usDataLength = ( uint16_t ) ( ( ( uint32_t ) usDataLength ) - ipSIZE_OF_ICMP_HEADER );

		/* Checksum has already been checked before in prvProcessIPPacket */

		/* Find the first byte of the data within the ICMP packet. */
		pucByte = ( uint8_t * ) pxICMPPacket;
		pucByte += sizeof( xICMPPacket_t );

		/* Check each byte. */
		for( usCount = 0; usCount < usDataLength; usCount++ )
		{
			if( *pucByte != ipECHO_DATA_FILL_BYTE )
			{
				eStatus = eInvalidData;
				break;
			}

			pucByte++;
		}

		/* Call back into the application to pass it the result. */
		vApplicationPingReplyHook( eStatus, pxICMPPacket->xICMPHeader.usIdentifier );
	}

#endif
/*-----------------------------------------------------------*/

#if ( ipconfigREPLY_TO_INCOMING_PINGS == 1 )

	static eFrameProcessingResult_t prvProcessICMPEchoRequest( xICMPPacket_t * const pxICMPPacket )
	{
	xICMPHeader_t *pxICMPHeader;
	xIPHeader_t *pxIPHeader;

		pxICMPHeader = &( pxICMPPacket->xICMPHeader );
		pxIPHeader = &( pxICMPPacket->xIPHeader );

		/* HT:endian: changed back */
		iptraceSENDING_PING_REPLY( pxIPHeader->ulSourceIPAddress );

		/* The checksum can be checked here - but a ping reply should be
		returned even if the checksum is incorrect so the other end can
		tell that the ping was received - even if the ping reply contains
		invalid data. */
		pxICMPHeader->ucTypeOfMessage = ipICMP_ECHO_REPLY;
		pxIPHeader->ulDestinationIPAddress = pxIPHeader->ulSourceIPAddress;
		pxIPHeader->ulSourceIPAddress = *ipLOCAL_IP_ADDRESS_POINTER;

		/* Update the checksum because the ucTypeOfMessage member in the header
		has been changed to ipICMP_ECHO_REPLY.  This is faster than calling
		usGenerateChecksum(). */
		if( pxICMPHeader->usChecksum >= FreeRTOS_htons( ( ( uint16_t ) 0xffffU ) - ( ipICMP_ECHO_REQUEST << ( ( uint16_t ) 8U ) ) ) )
		{
			pxICMPHeader->usChecksum = ( uint16_t )
				( ( ( uint32_t ) pxICMPHeader->usChecksum ) +
					FreeRTOS_htons( ipICMP_ECHO_REQUEST << ( ( uint16_t ) 8U ) ) + 1U );
		}
		else
		{
			pxICMPHeader->usChecksum = ( uint16_t )
				( ( ( uint32_t ) pxICMPHeader->usChecksum ) +
					FreeRTOS_htons( ipICMP_ECHO_REQUEST << ( ( uint16_t ) 8U ) ) );
		}

		return eReturnEthernetFrame;
	}

#endif /* ipconfigREPLY_TO_INCOMING_PINGS == 1 */
/*-----------------------------------------------------------*/

#if ( ipconfigREPLY_TO_INCOMING_PINGS == 1 ) || ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )

	static eFrameProcessingResult_t prvProcessICMPPacket( xICMPPacket_t * const pxICMPPacket )
	{
	eFrameProcessingResult_t eReturn = eReleaseBuffer;

		iptraceICMP_PACKET_RECEIVED();
		switch( pxICMPPacket->xICMPHeader.ucTypeOfMessage )
		{
			case ipICMP_ECHO_REQUEST	:
				#if ( ipconfigREPLY_TO_INCOMING_PINGS == 1 )
				{
					eReturn = prvProcessICMPEchoRequest( pxICMPPacket );
				}
				#endif /* ( ipconfigREPLY_TO_INCOMING_PINGS == 1 ) */
				break;

			case ipICMP_ECHO_REPLY		:
				#if ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )
				{
					prvProcessICMPEchoReply( pxICMPPacket );
				}
				#endif /* ipconfigSUPPORT_OUTGOING_PINGS */
				break;

			default	:
				break;
		}

		return eReturn;
	}

#endif /* ( ipconfigREPLY_TO_INCOMING_PINGS == 1 ) || ( ipconfigSUPPORT_OUTGOING_PINGS == 1 ) */
/*-----------------------------------------------------------*/

uint16_t usGenerateProtocolChecksum( const uint8_t * const pucEthernetBuffer, BaseType_t xOutgoingPacket )
{
uint32_t ulLength;
uint16_t usChecksum, *pusChecksum;
const xIPPacket_t * pxIPPacket;
BaseType_t lIpHeaderLength;
xProtPacket_t *pxProtPack;
uint8_t ucProtocol;
#if( ipconfigHAS_DEBUG_PRINTF != 0 )
	const char *pcType;
#endif

	pxIPPacket = ( const xIPPacket_t * ) pucEthernetBuffer;
	lIpHeaderLength = 4 * ( pxIPPacket->xIPHeader.ucVersionHeaderLength & 0x0F ); /*_RB_ Why 4? */
	pxProtPack = ( xProtPacket_t * ) ( pucEthernetBuffer + ( lIpHeaderLength - ipSIZE_OF_IP_HEADER ) );
	ucProtocol = pxIPPacket->xIPHeader.ucProtocol;

	if( ucProtocol == ipPROTOCOL_UDP )
	{
		pusChecksum = ( uint16_t * ) ( &( pxProtPack->xUDPPacket.xUDPHeader.usChecksum ) );
		#if( ipconfigHAS_DEBUG_PRINTF != 0 )
		{
			pcType = "UDP";
		}
		#endif	/* ipconfigHAS_DEBUG_PRINTF != 0 */
	}
	else if( ucProtocol == ipPROTOCOL_TCP	)
	{
		pusChecksum = ( uint16_t * ) ( &( pxProtPack->xTCPPacket.xTCPHeader.usChecksum ) );
		#if( ipconfigHAS_DEBUG_PRINTF != 0 )
		{
			pcType = "TCP";
		}
		#endif	/* ipconfigHAS_DEBUG_PRINTF != 0 */
	}
	else if( ucProtocol == ipPROTOCOL_ICMP || ucProtocol == ipPROTOCOL_IGMP )
	{
		pusChecksum = ( uint16_t * ) ( &( pxProtPack->xICMPPacket.xICMPHeader.usChecksum ) );

		#if( ipconfigHAS_DEBUG_PRINTF != 0 )
		{
			if( ucProtocol == ipPROTOCOL_ICMP )
			{
				pcType = "ICMP";
			}
			else
			{
				pcType = "IGMP";
			}
		}
		#endif	/* ipconfigHAS_DEBUG_PRINTF != 0 */
	}
	else
	{
		/* Unhandled protocol, other than ICMP, IGMP, UDP, or TCP. */
		return ipUNHANDLED_PROTOCOL;
	}

	if( xOutgoingPacket != pdFALSE )
	{
		/* This is an outgoing packet. Before calculating the checksum, set it
		to zero. */
		*( pusChecksum ) = 0;
	}
	else if ( *pusChecksum == 0 )
	{
		/* Sender hasn't set the checksum, no use to calculate it. */
		return ipCORRECT_CRC;
	}

	ulLength = ( uint32_t )
		( FreeRTOS_ntohs( pxIPPacket->xIPHeader.usLength ) - ( ( uint16_t ) lIpHeaderLength ) ); /* normally minus 20 */

	if( ( ulLength < sizeof( pxProtPack->xUDPPacket.xUDPHeader ) ) ||
		( ulLength > ( uint32_t )( ipconfigNETWORK_MTU - lIpHeaderLength ) ) )
	{
		#if( ipconfigHAS_DEBUG_PRINTF != 0 )
		{
			FreeRTOS_debug_printf( ( "usGenerateProtocolChecksum[%s]: len invalid: %lu\n", pcType, ulLength ) );
		}
		#endif	/* ipconfigHAS_DEBUG_PRINTF != 0 */

		/* Again, in a 16-bit return value there is no space to indicate an
		error.  For incoming packets, 0x1234 will cause dropping of the packet.
		For outgoing packets, there is a serious problem with the
		format/length */
		return ipINVALID_LENGTH;
	}
	if( ucProtocol <= ipPROTOCOL_IGMP )
	{
		/* ICMP/IGMP do not have a pseudo header for CRC-calculation. */
		usChecksum = ( uint16_t )
			( ~usGenerateChecksum( 0UL,
				( uint8_t * ) &( pxProtPack->xTCPPacket.xTCPHeader ), ( BaseType_t ) ulLength ) );
	}
	else
	{
		/* For UDP and TCP, sum the pseudo header, i.e. IP protocol + length
		fields */
		usChecksum = ( uint16_t ) ( ulLength + ( ( uint16_t ) ucProtocol ) );

		/* And then continue at the IPv4 source and destination addresses. */
		usChecksum = ( uint16_t )
			( ~usGenerateChecksum( ( uint32_t ) usChecksum, ( uint8_t * )&( pxIPPacket->xIPHeader.ulSourceIPAddress ),
				( BaseType_t )( 2 * sizeof( pxIPPacket->xIPHeader.ulSourceIPAddress ) + ulLength ) ) );

		/* Sum TCP header and data. */
	}

	if( usChecksum == 0 )
	{
		#if( ipconfigHAS_DEBUG_PRINTF != 0 )
		{
			if( xOutgoingPacket != pdFALSE )
			{
				FreeRTOS_debug_printf( ( "usGenerateProtocolChecksum[%s]: crc swap: %04X\n", pcType, usChecksum ) );
			}
		}
		#endif	/* ipconfigHAS_DEBUG_PRINTF != 0 */

		usChecksum = 0xffffu;
	}
	else
	{
		usChecksum = FreeRTOS_htons( usChecksum );
	}

	if( xOutgoingPacket != pdFALSE )
	{
		*( pusChecksum ) = usChecksum;
	}
	#if( ipconfigHAS_DEBUG_PRINTF != 0 )
	else if( usChecksum != 0xffffu )
	{
		FreeRTOS_debug_printf( ( "usGenerateProtocolChecksum[%s]: ID %04X: from %lxip to %lxip bad crc: %04X\n",
			pcType,
			FreeRTOS_ntohs( pxIPPacket->xIPHeader.usIdentification ),
			FreeRTOS_ntohl( pxIPPacket->xIPHeader.ulSourceIPAddress ),
			FreeRTOS_ntohl( pxIPPacket->xIPHeader.ulDestinationIPAddress ),
			FreeRTOS_ntohs( *pusChecksum ) ) );
	}
	#endif	/* ipconfigHAS_DEBUG_PRINTF != 0 */

	return usChecksum;
}
/*-----------------------------------------------------------*/

uint16_t usGenerateChecksum( uint32_t ulSum, const uint8_t * pucNextData, BaseType_t xDataLengthBytes )
{
xUnion32 xSum2, xSum, xTerm;
xUnionPtr xSource;		/* Points to first byte */
xUnionPtr xLastSource;	/* Points to last byte plus one */
uint32_t ulAlignBits, ulCarry = 0;

	/* Small MCUs often spend up to 30% of the time doing checksum calculations
	This function is optimised for 32-bit CPUs; Each time it will try to fetch
	32-bits, sums it with an accumulator and counts the number of carries. */

	/* Swap the input (little endian platform only). */
	xSum.u32 = FreeRTOS_ntohs( ulSum );
	xTerm.u32 = 0;

	xSource.u8ptr = ( uint8_t * ) pucNextData;
	ulAlignBits = ( ( ( uint32_t ) pucNextData ) & 0x03 ); /* gives 0, 1, 2, or 3 */

	/* If byte (8-bit) aligned... */
	if( ( ulAlignBits & 1 ) && ( xDataLengthBytes >= 1 ) )
	{
		xTerm.u8[ 1 ] = *( xSource.u8ptr++ );
		xDataLengthBytes--;
		/* Now xSource is word (16-bit) aligned. */
	}

	/* If half-word (16-bit) aligned... */
	if( ( ( ulAlignBits == 1 ) || ( ulAlignBits == 2 ) ) && ( xDataLengthBytes >= 2 ) )
	{
		xSum.u32 += *(xSource.u16ptr++);
		xDataLengthBytes -= 2;
		/* Now xSource is word (32-bit) aligned. */
	}

	/* Word (32-bit) aligned, do the most part. */
	xLastSource.u32ptr = ( xSource.u32ptr + ( xDataLengthBytes / 4 ) ) - 3;

	/* In this loop, four 32-bit additions will be done, in total 16 bytes.
	Indexing with constants (0,1,2,3) gives faster code than using
	post-increments. */
	while( xSource.u32ptr < xLastSource.u32ptr )
	{
		/* Use a secondary Sum2, just to see if the addition produced an
		overflow. */
		xSum2.u32 = xSum.u32 + xSource.u32ptr[ 0 ];
		if( xSum2.u32 < xSum.u32 )
		{
			ulCarry++;
		}

		/* Now add the secondary sum to the major sum, and remember if there was
		a carry. */
		xSum.u32 = xSum2.u32 + xSource.u32ptr[ 1 ];
		if( xSum2.u32 > xSum.u32 )
		{
			ulCarry++;
		}

		/* And do the same trick once again for indexes 2 and 3 */
		xSum2.u32 = xSum.u32 + xSource.u32ptr[ 2 ];
		if( xSum2.u32 < xSum.u32 )
		{
			ulCarry++;
		}

		xSum.u32 = xSum2.u32 + xSource.u32ptr[ 3 ];

		if( xSum2.u32 > xSum.u32 )
		{
			ulCarry++;
		}

		/* And finally advance the pointer 4 * 4 = 16 bytes. */
		xSource.u32ptr += 4;
	}

	/* Now add all carries. */
	xSum.u32 = ( uint32_t )xSum.u16[ 0 ] + xSum.u16[ 1 ] + ulCarry;

	xDataLengthBytes %= 16;
	xLastSource.u8ptr = ( uint8_t * )( xSource.u8ptr + ( xDataLengthBytes & ~1 ) );

	/* Half-word aligned. */
	while( xSource.u16ptr < xLastSource.u16ptr )
	{
		/* At least one more short. */
		xSum.u32 += xSource.u16ptr[ 0 ];
		xSource.u16ptr++;
	}

	if( xDataLengthBytes & 1 )	/* Maybe one more ? */
	{
		xTerm.u8[ 0 ] = xSource.u8ptr[ 0 ];
	}
	xSum.u32 += xTerm.u32;

	/* Now add all carries again. */
	xSum.u32 = ( uint32_t ) xSum.u16[ 0 ] + xSum.u16[ 1 ];

	/* The previous summation might have given a 16-bit carry. */
	xSum.u32 = ( uint32_t ) xSum.u16[ 0 ] + xSum.u16[ 1 ];

	if( ( ulAlignBits & 1 ) != 0 )
	{
		/* Quite unlikely, but pucNextData might be non-aligned, which would
		 mean that a checksum is calculated starting at an odd position. */
		xSum.u32 = ( ( xSum.u32 & 0xff ) << 8 ) | ( ( xSum.u32 & 0xff00 ) >> 8 );
	}

	/* swap the output (little endian platform only). */
	return FreeRTOS_htons( ( (uint16_t) xSum.u32 ) );
}
/*-----------------------------------------------------------*/

void vReturnEthernetFrame( xNetworkBufferDescriptor_t * pxNetworkBuffer, BaseType_t xReleaseAfterSend )
{
xEthernetHeader_t *pxEthernetHeader;

#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
	xNetworkBufferDescriptor_t *pxNewBuffer;
#endif

	#if defined( ipconfigETHERNET_MINIMUM_PACKET_BYTES )
	{
		if( pxNetworkBuffer->xDataLength < ipconfigETHERNET_MINIMUM_PACKET_BYTES )
		{
		BaseType_t xIndex;

			FreeRTOS_printf( ( "vReturnEthernetFrame: length %d\n", pxNetworkBuffer->xDataLength ) );
			for( xIndex = pxNetworkBuffer->xDataLength; xIndex < ipconfigETHERNET_MINIMUM_PACKET_BYTES; xIndex++ )
			{
				pxNetworkBuffer->pucEthernetBuffer[ xIndex ] = 0;
			}
			pxNetworkBuffer->xDataLength = ipconfigETHERNET_MINIMUM_PACKET_BYTES;
		}
	}
	#endif

#if( ipconfigZERO_COPY_TX_DRIVER != 0 )

	if( xReleaseAfterSend == pdFALSE )
	{
		pxNewBuffer = pxDuplicateNetworkBufferWithDescriptor( pxNetworkBuffer, pxNetworkBuffer->xDataLength );
		xReleaseAfterSend = pdTRUE;
		pxNetworkBuffer = pxNewBuffer;
	}

	if( pxNetworkBuffer != NULL )
#endif
	{
		pxEthernetHeader = ( xEthernetHeader_t * ) ( pxNetworkBuffer->pucEthernetBuffer );

		/* Swap source and destination MAC addresses. */
		memcpy( ( void * ) &( pxEthernetHeader->xDestinationAddress ), ( void * ) &( pxEthernetHeader->xSourceAddress ), sizeof( pxEthernetHeader->xDestinationAddress ) );
		memcpy( ( void * ) &( pxEthernetHeader->xSourceAddress) , ( void * ) ipLOCAL_MAC_ADDRESS, ( size_t ) ipMAC_ADDRESS_LENGTH_BYTES );

		/* Send! */
		xNetworkInterfaceOutput( pxNetworkBuffer, xReleaseAfterSend );
	}
}
/*-----------------------------------------------------------*/

uint32_t FreeRTOS_GetIPAddress( void )
{
	/* Returns the IP address of the NIC. */
	return *ipLOCAL_IP_ADDRESS_POINTER;
}
/*-----------------------------------------------------------*/

void FreeRTOS_SetIPAddress( uint32_t xIpAddress )
{
	/* Sets the IP address of the NIC. */
	*ipLOCAL_IP_ADDRESS_POINTER = xIpAddress;
}
/*-----------------------------------------------------------*/

uint32_t FreeRTOS_GetGatewayAddress( void )
{
	return xNetworkAddressing.ulGatewayAddress;
}
/*-----------------------------------------------------------*/

uint32_t FreeRTOS_GetDNSServerAddress( void )
{
	return xNetworkAddressing.ulDNSServerAddress;
}
/*-----------------------------------------------------------*/

uint32_t FreeRTOS_GetNetmask( void )
{
	return xNetworkAddressing.ulNetMask;
}
/*-----------------------------------------------------------*/

const uint8_t * FreeRTOS_GetMACAddress( void )
{
	return ipLOCAL_MAC_ADDRESS;
}
/*-----------------------------------------------------------*/

void FreeRTOS_SetNetmask ( uint32_t aNetmask )
{
	xNetworkAddressing.ulNetMask = aNetmask;
}
/*-----------------------------------------------------------*/

void FreeRTOS_SetGatewayAddress ( uint32_t aGateway )
{
	xNetworkAddressing.ulGatewayAddress = aGateway;
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_DHCP == 1 )
	void vIPSetDHCPTimerEnableState( BaseType_t xEnableState )
	{
		xDHCPTimer.bActive = ( xEnableState != 0 );
	}
#endif /* ipconfigUSE_DHCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_DHCP == 1 )
	void vIPReloadDHCPTimer( uint32_t ulLeaseTime )
	{
		prvIPTimerReload( &xDHCPTimer, ulLeaseTime );
	}
#endif /* ipconfigUSE_DHCP */
/*-----------------------------------------------------------*/

#if( ipconfigDNS_USE_CALLBACKS == 1 )
	void vIPSetDnsTimerEnableState( BaseType_t xEnableState )
	{
		xDNSTimer.bActive = ( xEnableState != 0 );
	}
#endif /* ipconfigUSE_DHCP */
/*-----------------------------------------------------------*/

#if( ipconfigDNS_USE_CALLBACKS != 0 )
	void vIPReloadDNSTimer( uint32_t ulCheckTime )
	{
		prvIPTimerReload( &xDNSTimer, ulCheckTime );
	}
#endif /* ipconfigDNS_USE_CALLBACKS != 0 */
/*-----------------------------------------------------------*/

BaseType_t xIPIsNetworkTaskReady( void )
{
	return xIPTaskInitialised;
}
/*-----------------------------------------------------------*/

BaseType_t FreeRTOS_IsNetworkUp( void )
{
	return xNetworkUp;
}
/*-----------------------------------------------------------*/
