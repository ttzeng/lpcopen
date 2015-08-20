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

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_DNS.h"
#include "NetworkBufferManagement.h"

/* The ItemValue of the sockets xBoundSocketListItem member holds the socket's
port number. */
#define socketSET_SOCKET_PORT( pxSocket, usPort ) listSET_LIST_ITEM_VALUE( ( &( ( pxSocket )->xBoundSocketListItem ) ), ( usPort ) )
#define socketGET_SOCKET_PORT( pxSocket ) listGET_LIST_ITEM_VALUE( ( &( ( pxSocket )->xBoundSocketListItem ) ) )

/* Test if a socket it bound which means it is either included in
xBoundUdpSocketsList or xBoundTcpSocketsList */
#define socketSOCKET_IS_BOUND( pxSocket )      ( listLIST_ITEM_CONTAINER( & ( pxSocket )->xBoundSocketListItem ) != NULL )

/* If FreeRTOS_sendto() is called on a socket that is not bound to a port
number then, depending on the FreeRTOSIPConfig.h settings, it might be that a
port number is automatically generated for the socket.  Automatically generated
port numbers will be between socketAUTO_PORT_ALLOCATION_START_NUMBER and
0xffff. */
/* _HT_ thinks that the default of 0xc000 is pretty high */
#if !defined( socketAUTO_PORT_ALLOCATION_START_NUMBER )
	#define socketAUTO_PORT_ALLOCATION_START_NUMBER ( ( uint16_t ) 0xc000 )
#endif

/* When the automatically generated port numbers overflow, the next value used
is not set back to socketAUTO_PORT_ALLOCATION_START_NUMBER because it is likely
that the first few automatically generated ports will still be in use.  Instead
it is reset back to the value defined by this constant. */
#define socketAUTO_PORT_ALLOCATION_RESET_NUMBER ( ( uint16_t ) 0xc100 )
#define socketAUTO_PORT_ALLOCATION_MAX_NUMBER   ( ( uint16_t ) 0xff00 )

/* The number of octets that make up an IP address. */
#define socketMAX_IP_ADDRESS_OCTETS		4

/*-----------------------------------------------------------*/

/*
 * Allocate the next port number from the private allocation range.
 * TCP and UDP each have their own series of port numbers
 * ulProtocol is either ipPROTOCOL_UDP or ipPROTOCOL_TCP
 */
static uint16_t prvGetPrivatePortNumber( BaseType_t ulProtocol );

/*
 * Return the list item from within pxList that has an item value of
 * xWantedItemValue.  If there is no such list item return NULL.
 */
static const ListItem_t * pxListFindListItemWithValue( const List_t *pxList, TickType_t xWantedItemValue );

/*
 * Return pdTRUE only if pxSocket is valid and bound, as far as can be
 * determined.
 */
static BaseType_t prvValidSocket( xFreeRTOS_Socket_t *pxSocket, BaseType_t ucProtocol, BaseType_t ucIsBound );

/*
 * Before creating a socket, check the validity of the parameters used
 * and find the size of the socket space, which is different for UDP and TCP
 */
static BaseType_t prvDetermineSocketSize( BaseType_t xDomain, BaseType_t xType, BaseType_t xProtocol, size_t *pxSocketSize );

#if( ipconfigUSE_TCP == 1 )
	/*
	 * Create a txStream or a rxStream, depending on the parameter 'bInput'
	 */
	static xStreamBuffer *prvTcpCreateStream (xFreeRTOS_Socket_t *pxSocket, BaseType_t bInput);
#endif /* ipconfigUSE_TCP == 1 */

#if( ipconfigUSE_TCP == 1 )
	/*
	 * Called from FreeRTOS_send(): some checks which will be done before
	 * sending a TCP packed.
	 */
	static int32_t prvTCPSendCheck( xFreeRTOS_Socket_t *pxSocket, size_t xDataLength );
#endif /* ipconfigUSE_TCP */

#if( ipconfigUSE_TCP == 1 )
	/*
	 * When a child socket gets closed, make sure to update the child-count of the parent
	 */
	static void prvTCPSetSocketCount( xFreeRTOS_Socket_t *pxSocket );
#endif  /* ipconfigUSE_TCP == 1 */

#if( ipconfigUSE_TCP == 1 )
	/*
	 * Called from FreeRTOS_connect(): make some checks and if allowed, send a
	 * message to the IP-task to start connecting to a remote socket
	 */
	static BaseType_t prvTCPConnectStart( xFreeRTOS_Socket_t *pxSocket, struct freertos_sockaddr *pxAddress );
#endif /* ipconfigUSE_TCP */

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	/* Executed by the IP-task, it will check all sockets belonging to a set */
	static xFreeRTOS_Socket_t *prvFindSelectedSocket( xSocketSelect_t *pxSocketSet );

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
/*-----------------------------------------------------------*/

/* The list that contains mappings between sockets and port numbers.  Accesses
to this list must be protected by critical sections of one kind or another. */
List_t xBoundUdpSocketsList;

#if ipconfigUSE_TCP == 1
	List_t xBoundTcpSocketsList;
#endif /* ipconfigUSE_TCP == 1 */

static uint16_t usNextPortToUse[] =
{
	socketAUTO_PORT_ALLOCATION_START_NUMBER, /* next port for UDP */
	#define NXT_UDP_PORT_IDX 0	/* index for UDP port numbers */
	#if ipconfigUSE_TCP == 1
		socketAUTO_PORT_ALLOCATION_START_NUMBER, /* next port for TCP */
		#define NXT_TCP_PORT_IDX 1	/* index for TCP port numbers */
	#endif /* ipconfigUSE_TCP == 1 */
};

/*-----------------------------------------------------------*/

static BaseType_t prvValidSocket( xFreeRTOS_Socket_t *pxSocket, BaseType_t ucProtocol, BaseType_t ucIsBound )
{
BaseType_t xReturn = pdTRUE;

	if( ( pxSocket == NULL ) || ( pxSocket == FREERTOS_INVALID_SOCKET ) )
	{
		xReturn = pdFALSE;
	}
	else if( ( ucIsBound != 0 ) && ( socketSOCKET_IS_BOUND( pxSocket ) == pdFALSE ) )
	{
		/* The caller expects the socket to be bound, but it isn't */
		xReturn = pdFALSE;
	}
	else if( pxSocket->ucProtocol != ucProtocol )
	{
		xReturn = pdFALSE;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

void vNetworkSocketsInit( void )
{
	vListInitialise( &xBoundUdpSocketsList );

	/* Determine the first anonymous UDP port number to get assigned.  Give it
	a random value in order to avoid confusion about port numbers being used
	earlier, before rebooting the device */
	usNextPortToUse[ NXT_UDP_PORT_IDX ] = ( uint16_t )
		( socketAUTO_PORT_ALLOCATION_START_NUMBER +
		( ipconfigRAND32() % ( socketAUTO_PORT_ALLOCATION_MAX_NUMBER - socketAUTO_PORT_ALLOCATION_RESET_NUMBER ) ) );

	#if( ipconfigUSE_TCP == 1 )
	{
		extern uint32_t ulNextInitialSequenceNumber;

		ulNextInitialSequenceNumber = ipconfigRAND32();

		/* Determine the first anonymous TCP port number to get assigned. */
		usNextPortToUse[NXT_TCP_PORT_IDX] = ( uint16_t )
			( socketAUTO_PORT_ALLOCATION_START_NUMBER +
			( ipconfigRAND32() % ( socketAUTO_PORT_ALLOCATION_MAX_NUMBER - socketAUTO_PORT_ALLOCATION_RESET_NUMBER ) ) );
		vListInitialise( &xBoundTcpSocketsList );
	}
	#endif  /* ipconfigUSE_TCP == 1 */
}
/*-----------------------------------------------------------*/

static BaseType_t prvDetermineSocketSize( BaseType_t xDomain, BaseType_t xType, BaseType_t xProtocol, size_t *pxSocketSize )
{
BaseType_t xReturn = pdPASS;
xFreeRTOS_Socket_t *pxSocket;

	/* Asserts must not appear before it has been determined that the network
	task is ready - otherwise the asserts will fail. */
	if( xIPIsNetworkTaskReady() == pdFALSE )
	{
		xReturn = pdFAIL;
	}
	else
	{
		/* Only Ethernet is currently supported. */
		configASSERT( xDomain == FREERTOS_AF_INET );
		configASSERT( listLIST_IS_INITIALISED( &xBoundUdpSocketsList ) );
		#if( ipconfigUSE_TCP == 1 )
		{
			configASSERT( listLIST_IS_INITIALISED( &xBoundTcpSocketsList ) );
		}
		#endif  /* ipconfigUSE_TCP == 1 */

		if( xProtocol == FREERTOS_IPPROTO_UDP )
		{
			if( xType != FREERTOS_SOCK_DGRAM )
			{
				xReturn = pdFAIL;
			}
			/* In case a UDP socket is created, do not allocate space for TCP data. */
			*pxSocketSize = ( sizeof( *pxSocket ) - sizeof( pxSocket->u ) ) + sizeof( pxSocket->u.xUdp );
		}
#if( ipconfigUSE_TCP == 1 )
		else if( xProtocol == FREERTOS_IPPROTO_TCP )
		{
			if( xType != FREERTOS_SOCK_STREAM )
			{
				xReturn = pdFAIL;
			}

			*pxSocketSize = ( sizeof( *pxSocket ) - sizeof( pxSocket->u ) ) + sizeof( pxSocket->u.xTcp );
		}
#endif  /* ipconfigUSE_TCP == 1 */
		else
		{
			xReturn = pdFAIL;
		}
	}
	/* In case configASSERT() is not used */
	( void )xDomain;
	return xReturn;
}
/*-----------------------------------------------------------*/

/* FreeRTOS_socket() allocates and initiates a socket */
xSocket_t FreeRTOS_socket( BaseType_t xDomain, BaseType_t xType, BaseType_t xProtocol )
{
xFreeRTOS_Socket_t *pxSocket;
size_t uxSocketSize;
EventGroupHandle_t xEventGroup;
xSocket_t xReturn;

	if( prvDetermineSocketSize( xDomain, xType, xProtocol, &uxSocketSize ) == pdFAIL )
	{
		xReturn = FREERTOS_INVALID_SOCKET;
	}
	else
	{
		/* Allocate the structure that will hold the socket information.  The size
		depends on the type of socket: UDP sockets need less space.  A define
		'pvPortMallocSocket' will used to allocate the necessary space.  By default
		it points to the FreeRTOS function 'pvPortMalloc()'. */
		pxSocket = ( xFreeRTOS_Socket_t * ) pvPortMallocSocket( uxSocketSize );

		if( pxSocket == NULL )
		{
			pxSocket = ( xFreeRTOS_Socket_t * ) FREERTOS_INVALID_SOCKET;
			iptraceFAILED_TO_CREATE_SOCKET();
		}
		else if( ( xEventGroup = xEventGroupCreate() ) == NULL )
		{
			vPortFreeSocket( pxSocket );
			pxSocket = ( xFreeRTOS_Socket_t * ) FREERTOS_INVALID_SOCKET;
			iptraceFAILED_TO_CREATE_EVENT_GROUP();
		}
		else
		{
			/* Clear the entire space to avoid nulling individual entries */
			memset( pxSocket, '\0', uxSocketSize );

			pxSocket->xEventGroup = xEventGroup;

			/* Initialise the socket's members.  The semaphore will be created if
			the socket is bound to an address, for now the pointer to the semaphore
			is just set to NULL to show it has not been created. */
			if( xProtocol == FREERTOS_IPPROTO_UDP )
			{
				vListInitialise( &( pxSocket->u.xUdp.xWaitingPacketsList ) );

				#if( ipconfigUDP_MAX_RX_PACKETS > 0 )
				{
					pxSocket->u.xUdp.xMaxPackets = ipconfigUDP_MAX_RX_PACKETS;
				}
				#endif /* ipconfigUDP_MAX_RX_PACKETS > 0 */
			}

			vListInitialiseItem( &( pxSocket->xBoundSocketListItem ) );
			listSET_LIST_ITEM_OWNER( &( pxSocket->xBoundSocketListItem ), ( void * ) pxSocket );

			pxSocket->xReceiveBlockTime = ipconfigSOCK_DEFAULT_RECEIVE_BLOCK_TIME;
			pxSocket->xSendBlockTime    = ipconfigSOCK_DEFAULT_SEND_BLOCK_TIME;
			pxSocket->ucSocketOptions   = FREERTOS_SO_UDPCKSUM_OUT;
			pxSocket->ucProtocol        = (uint8_t)xProtocol; /* protocol: UDP or TCP */

			#if( ipconfigUSE_TCP == 1 )
			{
				if( xProtocol == FREERTOS_IPPROTO_TCP )
				{
					/* StreamSize is expressed in number of bytes */
					/* Round up buffer sizes to nearest multiple of MSS */
					pxSocket->u.xTcp.usInitMSS    = pxSocket->u.xTcp.usCurMSS = ipconfigTCP_MSS;
					pxSocket->u.xTcp.rxStreamSize = ipconfigTCP_RX_BUF_LEN;
					pxSocket->u.xTcp.txStreamSize = ( int32_t ) FreeRTOS_round_up( ipconfigTCP_TX_BUF_LEN, ipconfigTCP_MSS );
					/* Use half of the buffer size of the TCP windows */
					#if ( ipconfigUSE_TCP_WIN == 1 )
					{
						pxSocket->u.xTcp.ulRxWinSize  = FreeRTOS_max_uint32( 1UL, ( uint32_t ) ( pxSocket->u.xTcp.rxStreamSize / 2 ) / ipconfigTCP_MSS );
						pxSocket->u.xTcp.ulTxWinSize  = FreeRTOS_max_uint32( 1UL, ( uint32_t ) ( pxSocket->u.xTcp.txStreamSize / 2 ) / ipconfigTCP_MSS );
					}
					#else
					{
						pxSocket->u.xTcp.ulRxWinSize  = 1;
						pxSocket->u.xTcp.ulTxWinSize  = 1;
					}
					#endif
					/* The above values are just defaults, and can be overridden by
					calling FreeRTOS_setsockopt().  No buffers will be allocated until a
					socket is connected and data is exchanged. */
				}
			}
			#endif  /* ipconfigUSE_TCP == 1 */
		}

		xReturn = ( xSocket_t ) pxSocket;
	}

	/* Remove compiler warnings in the case the configASSERT() is not defined. */
	( void ) xDomain;

	return xReturn;
}
/*-----------------------------------------------------------*/

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	xSocketSet_t FreeRTOS_CreateSocketSet( void )
	{
	xSocketSelect_t *pxSocketSet;

		pxSocketSet = ( xSocketSelect_t * ) pvPortMalloc( sizeof( *pxSocketSet ) );

		if( pxSocketSet != NULL )
		{
			memset( pxSocketSet, '\0', sizeof( *pxSocketSet ) );
			pxSocketSet->xSelectGroup = xEventGroupCreate();

			if( pxSocketSet->xSelectGroup == NULL )
			{
				vPortFree( ( void* ) pxSocketSet );
				pxSocketSet = NULL;
			}
		}

		return ( xSocketSet_t * ) pxSocketSet;
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	void FreeRTOS_DeleteSocketSet( xSocketSet_t xSocketSet )
	{
		xSocketSelect_t *pxSocketSet = ( xSocketSelect_t*) xSocketSet;

		vEventGroupDelete( pxSocketSet->xSelectGroup );
		vPortFree( ( void* ) pxSocketSet );
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	/* Add a socket to a set */
	void FreeRTOS_FD_SET( xSocket_t xSocket, xSocketSet_t xSocketSet, EventBits_t xSelectBits )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	xSocketSelect_t *pxSocketSet = ( xSocketSelect_t * ) xSocketSet;

		configASSERT( pxSocket != NULL );
		configASSERT( xSocketSet != NULL );

		/* Make sure we're not adding bits which are reserved for internal use,
		such as eSELECT_CALL_IP */
		pxSocket->xSelectBits |= ( xSelectBits & eSELECT_ALL );

		if( ( pxSocket->xSelectBits & eSELECT_ALL ) != 0 )
		{
			/* Adding a socket to a socket set. */
			pxSocket->pxSocketSet = ( xSocketSelect_t * ) xSocketSet;

			/* Now have the IP-task call vSocketSelect() to see if the set contains
			any sockets which are 'ready' and set the proper bits.
			By setting 'bApiCalled = false', vSocketSelect() knows that it was
			not called from a user API */
			pxSocketSet->bApiCalled = pdFALSE;
			prvFindSelectedSocket( pxSocketSet );
		}
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
	/* Clear select bits for a socket
	If the mask becomes 0, remove the socket from the set */
	void FreeRTOS_FD_CLR( xSocket_t xSocket, xSocketSet_t xSocketSet, EventBits_t xSelectBits )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;

		configASSERT( pxSocket != NULL );
		configASSERT( xSocketSet != NULL );

		pxSocket->xSelectBits &= ~( xSelectBits & eSELECT_ALL );
		if( ( pxSocket->xSelectBits & eSELECT_ALL ) != 0 )
		{
			pxSocket->pxSocketSet = ( xSocketSelect_t *)xSocketSet;
		}
		else
		{
			/* disconnect it from the socket set */
			pxSocket->pxSocketSet = ( xSocketSelect_t *)NULL;
		}
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
/*-----------------------------------------------------------*/


#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	/* Test if a socket belongs to a socket-set */
	EventBits_t FreeRTOS_FD_ISSET( xSocket_t xSocket, xSocketSet_t xSocketSet )
	{
	EventBits_t xReturn;
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;

		configASSERT( pxSocket != NULL );
		configASSERT( xSocketSet != NULL );

		if( xSocketSet == ( xSocketSet_t ) pxSocket->pxSocketSet )
		{
			/* Make sure we're not adding bits which are reserved for internal
			use. */
			xReturn = pxSocket->xSocketBits & eSELECT_ALL;
		}
		else
		{
			xReturn = 0;
		}

		return xReturn;
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	/* The select() statement: wait for an event to occur on any of the sockets
	included in a socket set */
	BaseType_t FreeRTOS_select( xSocketSet_t xSocketSet, TickType_t xBlockTimeTicks )
	{
	TimeOut_t xTimeOut;
	TickType_t xRemainingTime;
	xSocketSelect_t *pxSocketSet = ( xSocketSelect_t*) xSocketSet;
	BaseType_t xResult;

		configASSERT( xSocketSet != NULL );

		/* Only in the first round, check for non-blocking */
		xRemainingTime = xBlockTimeTicks;

		/* Fetch the current time */
		vTaskSetTimeOutState( &xTimeOut );

		for( ;; )
		{
			/* Find a socket which might have triggered the bit
			This function might return immediately or block for a limited time */
			xEventGroupWaitBits( pxSocketSet->xSelectGroup, eSELECT_ALL, pdFALSE, pdFALSE, xRemainingTime );

			/* Have the IP-task find the socket which had an event */
			pxSocketSet->bApiCalled = pdTRUE;
			prvFindSelectedSocket( pxSocketSet );

			xResult = ( BaseType_t ) xEventGroupGetBits( pxSocketSet->xSelectGroup );

			if( xResult != 0 )
			{
				break;
			}

			/* Has the timeout been reached? */
			if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTime ) != pdFALSE )
			{
				break;
			}
		}

		return xResult;
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION */
/*-----------------------------------------------------------*/

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	/* Send a message to the IP-task to have it check all sockets belonging to
	'pxSocketSet' */
	static xFreeRTOS_Socket_t *prvFindSelectedSocket( xSocketSelect_t *pxSocketSet )
	{
	xIPStackEvent_t xSelectEvent;
	xFreeRTOS_Socket_t *xReturn;

		xSelectEvent.eEventType = eSocketSelectEvent;
		xSelectEvent.pvData = ( void * ) pxSocketSet;

		/* while the IP-task works on the request, the API will block on
		'eSELECT_CALL_IP'.  So clear it first. */
		xEventGroupClearBits( pxSocketSet->xSelectGroup, eSELECT_CALL_IP );

		/* Now send the socket select event */
		if( xSendEventStructToIPTask( &xSelectEvent, ( TickType_t ) portMAX_DELAY ) == pdFAIL )
		{
			/* Oops, we failed to wake-up the IP task. No use to wait for it. */
			FreeRTOS_debug_printf( ( "prvFindSelectedSocket: failed\n" ) );
			xReturn = NULL;
		}
		else
		{
			/* As soon as the IP-task is ready, it will set 'eSELECT_CALL_IP' to
			wakeup the calling API */
			xEventGroupWaitBits( pxSocketSet->xSelectGroup, eSELECT_CALL_IP, pdTRUE, pdFALSE, portMAX_DELAY );

			/* Return 'pxSocket' which is set by the IP-task */
			xReturn = pxSocketSet->pxSocket;
		}

		return xReturn;
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */
/*-----------------------------------------------------------*/

/*
 * FreeRTOS_recvfrom: receive data from a bound socket
 * In this library, the function can only be used with connectionsless sockets
 * (UDP)
 */
int32_t FreeRTOS_recvfrom( xSocket_t xSocket, void *pvBuffer, size_t xBufferLength, uint32_t ulFlags, struct freertos_sockaddr *pxSourceAddress, socklen_t *pxSourceAddressLength )
{
BaseType_t lPacketCount = 0;
xNetworkBufferDescriptor_t *pxNetworkBuffer;
xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
TickType_t xRemainingTime = 0; /* Obsolete assignment, but some compilers output a warning if its not done. */
BaseType_t xTimed = pdFALSE;
TimeOut_t xTimeOut;
int32_t lReturn;

	if( prvValidSocket( pxSocket, FREERTOS_IPPROTO_UDP, pdTRUE ) == pdFALSE )
	{
		return -pdFREERTOS_ERRNO_EINVAL;
	}

	lPacketCount = ( BaseType_t )listCURRENT_LIST_LENGTH( &( pxSocket->u.xUdp.xWaitingPacketsList ) );

	/* The function prototype is designed to maintain the expected Berkeley
	sockets standard, but this implementation does not use all the parameters. */
	( void ) pxSourceAddressLength;

	while( lPacketCount == 0 )
	{
		if( xTimed == pdFALSE )
		{
			/* Only in the first round, check for non-blocking */
			xRemainingTime = pxSocket->xReceiveBlockTime;

			if( xRemainingTime == 0 )
			{
				break;
			}

			if( ( ulFlags & FREERTOS_MSG_DONTWAIT ) != 0 )
			{
				break;
			}

			/* Don't get here a second time. */
			xTimed = pdTRUE;

			/* Fetch the current time */
			vTaskSetTimeOutState( &xTimeOut );
		}

		/* Wait for arrival of data.  While waiting, the IP-task may set the
		'eSOCKET_RECEIVE' bit in 'xEventGroup', if it receives data for this
		socket, thus unblocking this API call. */
		xEventGroupWaitBits( pxSocket->xEventGroup, eSOCKET_RECEIVE, pdTRUE /*xClearOnExit*/, pdFALSE /*xWaitAllBits*/, xRemainingTime );

		lPacketCount = ( BaseType_t ) listCURRENT_LIST_LENGTH( &( pxSocket->u.xUdp.xWaitingPacketsList ) );

		if( lPacketCount != 0 )
		{
			break;
		}

		/* Has the timeout been reached ? */
		if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTime ) )
		{
			break;
		}
	}

	if( lPacketCount != 0 )
	{
		taskENTER_CRITICAL();
		{
			/* The owner of the list item is the network buffer. */
			pxNetworkBuffer = ( xNetworkBufferDescriptor_t * ) listGET_OWNER_OF_HEAD_ENTRY( &( pxSocket->u.xUdp.xWaitingPacketsList ) );

			/* Remove the network buffer from the list of buffers waiting to
			be processed by the socket. */
			uxListRemove( &( pxNetworkBuffer->xBufferListItem ) );
		}
		taskEXIT_CRITICAL();

		/* The returned value is the data length, which may have been capped to
		the receive buffer size. */
		lReturn = ( int32_t ) pxNetworkBuffer->xDataLength;

		if( pxSourceAddress != NULL )
		{
			pxSourceAddress->sin_port = pxNetworkBuffer->usPort;
			pxSourceAddress->sin_addr = pxNetworkBuffer->ulIPAddress;
		}

		if( ( ulFlags & FREERTOS_ZERO_COPY ) == 0 )
		{
			/* The zero copy flag is not set.  Truncate the length if it won't
			fit in the provided buffer. */
			if( lReturn > ( int32_t ) xBufferLength )
			{
				iptraceRECVFROM_DISCARDING_BYTES( ( xBufferLength - lReturn ) );
				lReturn = ( int32_t )xBufferLength;
			}

			/* Copy the received data into the provided buffer, then release the
			network buffer. */
			memcpy( pvBuffer, ( void * ) &( pxNetworkBuffer->pucEthernetBuffer[ ipUDP_PAYLOAD_OFFSET ] ), ( size_t )lReturn );
			vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
		}
		else
		{
			/* The zero copy flag was set.  pvBuffer is not a buffer into which
			the received data can be copied, but a pointer that must be set to
			point to the buffer in which the received data has already been
			placed. */
			*( ( void** ) pvBuffer ) = ( void * ) ( &( pxNetworkBuffer->pucEthernetBuffer[ ipUDP_PAYLOAD_OFFSET ] ) );
		}

	}
	else
	{
		lReturn = -pdFREERTOS_ERRNO_EWOULDBLOCK;
		iptraceRECVFROM_TIMEOUT();
	}

	return lReturn;
}
/*-----------------------------------------------------------*/

int32_t FreeRTOS_sendto( xSocket_t xSocket, const void *pvBuffer, size_t xTotalDataLength, uint32_t ulFlags, const struct freertos_sockaddr *pxDestinationAddress, socklen_t xDestinationAddressLength )
{
xNetworkBufferDescriptor_t *pxNetworkBuffer;
xIPStackEvent_t xStackTxEvent = { eStackTxEvent, NULL };
TimeOut_t xTimeOut;
TickType_t xTicksToWait;
int32_t lReturn = 0;
xFreeRTOS_Socket_t *pxSocket;

	pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;

	/* The function prototype is designed to maintain the expected Berkeley
	sockets standard, but this implementation does not use all the
	parameters. */
	( void ) xDestinationAddressLength;
	configASSERT( pvBuffer );

	if( xTotalDataLength <= ipMAX_UDP_PAYLOAD_LENGTH )
	{
		/* If the socket is not already bound to an address, bind it now.
		Passing NULL as the address parameter tells FreeRTOS_bind() to select
		the address to bind to. */
		if( ( socketSOCKET_IS_BOUND( pxSocket ) != pdFALSE ) ||
			( FreeRTOS_bind( xSocket, NULL, 0 ) == 0 ) )
		{
			xTicksToWait = pxSocket->xSendBlockTime;

			#if( ipconfigUSE_CALLBACKS != 0 )
			{
				if( xIsCallingFromIPTask() != pdFALSE )
				{
					/* If this send function is called from within a call-back
					handler it may not block, otherwise chances would be big to
					get a deadlock: the IP-task waiting for itself. */
					xTicksToWait = 0;
				}
			}
			#endif /* ipconfigUSE_CALLBACKS */

			if( ( ulFlags & FREERTOS_MSG_DONTWAIT ) != 0 )
			{
				xTicksToWait = 0;
			}

			if( ( ulFlags & FREERTOS_ZERO_COPY ) == 0 )
			{
				/* Zero copy is not set, so obtain a network buffer into
				which the payload will be copied. */
				vTaskSetTimeOutState( &xTimeOut );

				/* Block until a buffer becomes available, or until a
				timeout has been reached */
				pxNetworkBuffer = pxGetNetworkBufferWithDescriptor( xTotalDataLength + sizeof( xUDPPacket_t ), xTicksToWait );

				if( pxNetworkBuffer != NULL )
				{
					memcpy( ( void * ) &( pxNetworkBuffer->pucEthernetBuffer[ ipUDP_PAYLOAD_OFFSET ] ), ( void * ) pvBuffer, xTotalDataLength );

					if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdTRUE )
					{
						/* The entire block time has been used up. */
						xTicksToWait = 0;
					}
				}
			}
			else
			{
				/* When zero copy is used, pvBuffer is a pointer to the
				payload of a buffer that has already been obtained from the
				stack.  Obtain the network buffer pointer from the buffer. */
				pxNetworkBuffer = pxUDPPayloadBuffer_to_NetworkBuffer( (void*)pvBuffer );
			}

			if( pxNetworkBuffer != NULL )
			{
				pxNetworkBuffer->xDataLength = xTotalDataLength;
				pxNetworkBuffer->usPort = pxDestinationAddress->sin_port;
				pxNetworkBuffer->usBoundPort = ( uint16_t ) socketGET_SOCKET_PORT( pxSocket );
				pxNetworkBuffer->ulIPAddress = pxDestinationAddress->sin_addr;

				/* The socket options are passed to the IP layer in the
				space that will eventually get used by the Ethernet header. */
				pxNetworkBuffer->pucEthernetBuffer[ ipSOCKET_OPTIONS_OFFSET ] = pxSocket->ucSocketOptions;

				/* Tell the networking task that the packet needs sending. */
				xStackTxEvent.pvData = pxNetworkBuffer;

				/* Ask the IP-task to send this packet */
				if( xSendEventStructToIPTask( &xStackTxEvent, xTicksToWait ) == pdPASS )
				{
					/* The packet was successfully sent to the IP task. */
					lReturn = ( int32_t ) xTotalDataLength;
					#if( ipconfigUSE_CALLBACKS == 1 )
					{
						if( ipconfigIS_VALID_PROG_ADDRESS( pxSocket->u.xUdp.pHndSent ) )
						{
							pxSocket->u.xUdp.pHndSent( (xSocket_t *)pxSocket, xTotalDataLength );
						}
					}
					#endif /* ipconfigUSE_CALLBACKS */
				}
				else
				{
					/* If the buffer was allocated in this function, release
					it. */
					if( ( ulFlags & FREERTOS_ZERO_COPY ) == 0 )
					{
						vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
					}
					iptraceSTACK_TX_EVENT_LOST( ipSTACK_TX_EVENT );
				}
			}
			else
			{
				/* If errno was available, errno would be set to
				FREERTOS_ENOPKTS.  As it is, the function must return the
				number of transmitted bytes, so the calling function knows
				how	much data was actually sent. */
				iptraceNO_BUFFER_FOR_SENDTO();
			}
		}
		else
		{
			iptraceSENDTO_SOCKET_NOT_BOUND();
		}
	}
	else
	{
		/* The data is longer than the available buffer space. */
		iptraceSENDTO_DATA_TOO_LONG();
	}

	return lReturn;
} /* Tested */
/*-----------------------------------------------------------*/

/*
 * FreeRTOS_bind() : binds a sockt to a local port number
 * If port 0 is provided, a system provided port number will be assigned
 * Function can be used for both UDP and TCP sockets
 * The actual binding will be performed by the IP-task to avoid mutual access
 * to the bound-socket-lists (xBoundUdpSocketsList cq xBoundTcpSocketsList)
 */
BaseType_t FreeRTOS_bind( xSocket_t xSocket, struct freertos_sockaddr * pxAddress, socklen_t xAddressLength )
{
xIPStackEvent_t xBindEvent;
xFreeRTOS_Socket_t *pxSocket = (xFreeRTOS_Socket_t *)xSocket;
BaseType_t xReturn = 0;

	( void ) xAddressLength;

	/* Once a socket is bound to a port, it can not be bound to a different
	port number */
	if( socketSOCKET_IS_BOUND( pxSocket) != pdFALSE )
	{
		/* The socket is already bound. */
		FreeRTOS_debug_printf( ( "vSocketBind: Socket already bound to %d\n", pxSocket->usLocPort ) );
		xReturn = -pdFREERTOS_ERRNO_EINVAL;
	}
	else
	{
		/* Prepare a messages to the IP-task in order to perform the binding.
		The desired port number will be passed in 'usLocPort */
		xBindEvent.eEventType = eSocketBindEvent;
		xBindEvent.pvData = ( void * ) xSocket;
		if( pxAddress != NULL )
		{
			pxSocket->usLocPort = FreeRTOS_ntohs( pxAddress->sin_port );
		}
		else
		{
			/* Caller wants to bind to a random port number. */
			pxSocket->usLocPort = 0;
		}

		/* portMAX_DELAY is used as a the time-out parameter, as binding *must*
		succeed before the socket can be used. */
		if( xSendEventStructToIPTask( &xBindEvent, ( TickType_t ) portMAX_DELAY ) == pdFAIL )
		{
			/* Failed to wake-up the IP-task, no use to wait for it */
			FreeRTOS_debug_printf( ( "FreeRTOS_bind: send event failed\n" ) );
			xReturn = -pdFREERTOS_ERRNO_ECANCELED;
		}
		else
		{
			/* The IP-task will set the 'eSOCKET_BOUND' bit when it has done its
			job. */
			xEventGroupWaitBits( pxSocket->xEventGroup, eSOCKET_BOUND, pdTRUE /*xClearOnExit*/, pdFALSE /*xWaitAllBits*/, portMAX_DELAY );
			if( socketSOCKET_IS_BOUND( pxSocket ) == pdFALSE )
			{
				xReturn = -pdFREERTOS_ERRNO_EINVAL;
			}
		}
	}

	return xReturn;
}

/*
 * vSocketBind(): internal version of bind(), should not be called directly
 * 'ulInternal' is used for TCP sockets only: it allows to have several (connected) child sockets
 * bound to the same server port
 */
BaseType_t vSocketBind( xFreeRTOS_Socket_t *pxSocket, struct freertos_sockaddr * pxAddress, BaseType_t xAddressLength, BaseType_t ulInternal )
{
BaseType_t xReturn = 0; /* In Berkeley sockets, 0 means pass for bind(). */
List_t *pxSocketList;
#if ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND == 1
	struct freertos_sockaddr xAddress;
#endif /* ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND */

#if ipconfigUSE_TCP == 1
	if( pxSocket->ucProtocol == FREERTOS_IPPROTO_TCP )
	{
		pxSocketList = &xBoundTcpSocketsList;
	}
	else
#endif  /* ipconfigUSE_TCP == 1 */
	{
		pxSocketList = &xBoundUdpSocketsList;
	}

	/* The function prototype is designed to maintain the expected Berkeley
	sockets standard, but this implementation does not use all the parameters. */
	( void ) xAddressLength;

	configASSERT( pxSocket );
	configASSERT( pxSocket != FREERTOS_INVALID_SOCKET );

	#if ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND == 1
	{
		/* pxAddress will be NULL if sendto() was called on a socket without the
		socket being bound to an address.  In this case, automatically allocate
		an address to the socket.  There is a very tiny chance that the allocated
		port will already be in use - if that is the case, then the check below
		[pxListFindListItemWithValue()] will result in an error being returned. */
		if( pxAddress == NULL )
		{
			pxAddress = &xAddress;
			/* For now, put it to zero, will be assigned later */
			pxAddress->sin_port = 0;
		}
	}
	#endif /* ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND == 1 */

	/* Sockets must be bound before calling FreeRTOS_sendto() if
	ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND is not set to 1. */
	configASSERT( pxAddress );

	if( pxAddress != NULL )
	{
		if( pxAddress->sin_port == 0 )
		{
			pxAddress->sin_port = prvGetPrivatePortNumber( pxSocket->ucProtocol );
		}

		/* If vSocketBind() is called from the API FreeRTOS_bind() it has been
		confirmed that the socket was not yet bound to a port.  If it is called
		from the IP-task, no such check is necessary. */

		/* Check to ensure the port is not already in use.  If the bind is
		called internally, a port MAY be used by more than one socket. */
		if( ( ulInternal == pdFALSE || ( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP ) ) &&
			( pxListFindListItemWithValue( pxSocketList, ( TickType_t ) pxAddress->sin_port ) != NULL ) )
		{
			FreeRTOS_debug_printf( ( "vSocketBind: %sP port %d in use\n",
				pxSocket->ucProtocol == FREERTOS_IPPROTO_TCP ? "TC" : "UD",
				FreeRTOS_ntohs( pxAddress->sin_port ) ) );
			xReturn = -pdFREERTOS_ERRNO_EADDRINUSE;
		}
		else
		{
			/* Allocate the port number to the socket.
			This macro will set 'xBoundSocketListItem->xItemValue' */
			socketSET_SOCKET_PORT( pxSocket, pxAddress->sin_port );

			/* And also store it in a socket field 'usLocPort' in host-byte-order,
			mostly used for logging and debugging purposes */
			pxSocket->usLocPort = FreeRTOS_ntohs( pxAddress->sin_port );

			/* Add the socket to the list of bound ports. */
			{
			/* If the network driver may iterate through 'xBoundUdpSocketsList',
			 * by calling xPortHasUdpSocket(), the IP-task must temporarily
			 * suspend the scheduler to keep the list in a integer state */
				#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 1 )
				{
					vTaskSuspendAll();
				}
				#endif /* ipconfigETHERNET_DRIVER_FILTERS_PACKETS */

				/* Add the socket to 'xBoundUdpSocketsList' or 'xBoundTcpSocketsList' */
				vListInsertEnd( pxSocketList, &( pxSocket->xBoundSocketListItem ) );

				#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 1 )
				{
					xTaskResumeAll();
				}
				#endif /* ipconfigETHERNET_DRIVER_FILTERS_PACKETS */
			}
		}
	}
	else
	{
		xReturn = -pdFREERTOS_ERRNO_EADDRNOTAVAIL;
		FreeRTOS_debug_printf( ( "vSocketBind: Socket no addr\n" ) );
	}

	if( xReturn != 0 )
	{
		iptraceBIND_FAILED( xSocket, ( FreeRTOS_ntohs( pxAddress->sin_port ) ) );
	}

	return xReturn;
} /* Tested */
/*-----------------------------------------------------------*/

/*
 * Close a socket and free the allocated space
 * In case of a TCP socket: the connection will not be closed automatically
 * Subsequent messages for the closed socket will be responded to with a RST
 * The IP-task will actually close the socket, after receiving a 'eSocketCloseEvent' message
 */
BaseType_t FreeRTOS_closesocket( xSocket_t xSocket )
{
BaseType_t xResult;
#if( ipconfigUSE_TCP == 1 ) && ( ipconfigUSE_CALLBACKS == 1 )
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * )xSocket;
#endif
xIPStackEvent_t xCloseEvent;
xCloseEvent.eEventType = eSocketCloseEvent;
xCloseEvent.pvData = ( void * ) xSocket;

	if( ( xSocket == NULL ) || ( xSocket == FREERTOS_INVALID_SOCKET ) )
	{
		xResult = 0;
	}
	else
	{
		#if( ( ipconfigUSE_TCP == 1 ) && ( ipconfigUSE_CALLBACKS == 1 ) )
		{
			if( pxSocket->ucProtocol == FREERTOS_IPPROTO_TCP )
			{
				/* Make sure that IP-task won't call the user callback's anymore */
				pxSocket->u.xTcp.pHndConnected = NULL;
				pxSocket->u.xTcp.pHndReceive = NULL;
				pxSocket->u.xTcp.pHndSent = NULL;
			}
		}
		#endif  /* ( ( ipconfigUSE_TCP == 1 ) && ( ipconfigUSE_CALLBACKS == 1 ) ) */

		/* Let the IP task close the socket to keep it synchronised	with the
		packet handling. */

		/* Note when changing the time-out value below, it must be checked who is calling
		this function. If it is called by the IP-task, a deadlock could occur.
		The IP-task would only call it in case of a user call-back */
		if( xSendEventStructToIPTask( &xCloseEvent, ( TickType_t ) 0 ) == pdFAIL )
		{
			FreeRTOS_debug_printf( ( "FreeRTOS_closesocket: failed\n" ) );
			xResult = -1;
		}
		else
		{
			xResult = 1;
		}
	}

	return xResult;
}

/* This is the internal version of FreeRTOS_closesocket()
 * It will be called by the IPtask only to avoid problems with synchronicity
 */
void *vSocketClose( xFreeRTOS_Socket_t *pxSocket )
{
xNetworkBufferDescriptor_t *pxNetworkBuffer;

	#if( ipconfigUSE_TCP == 1 )
	{
		/* For TCP: clean up a little more. */
		if( pxSocket->ucProtocol == FREERTOS_IPPROTO_TCP )
		{
			#if( ipconfigUSE_TCP_WIN == 1 )
			{
				if( pxSocket->u.xTcp.pxAckMessage != NULL )
				{
					vReleaseNetworkBufferAndDescriptor( pxSocket->u.xTcp.pxAckMessage );
				}
				/* Free the resources which were claimed by the tcpWin member */
				vTCPWindowDestroy( &pxSocket->u.xTcp.xTcpWindow );
			}
			#endif /* ipconfigUSE_TCP_WIN */

			/* Free the input and output streams */
			if( pxSocket->u.xTcp.rxStream != NULL )
			{
				vPortFreeLarge( pxSocket->u.xTcp.rxStream );
			}

			if( pxSocket->u.xTcp.txStream != NULL )
			{
				vPortFreeLarge( pxSocket->u.xTcp.txStream );
			}

			/* In case this is a child socket, make sure the child-count of the
			parent socket is decreased. */
			prvTCPSetSocketCount( pxSocket );
		}
	}
	#endif  /* ipconfigUSE_TCP == 1 */

	/* Socket must be unbound first, to ensure no more packets are queued on
	it. */
	if( socketSOCKET_IS_BOUND( pxSocket ) != pdFALSE )
	{
		/* If the network driver may iterate through 'xBoundUdpSocketsList',
		by calling xPortHasUdpSocket(), the IP-task must temporarily
		suspend the scheduler to keep the list in a integer state */
		#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 1 )
		{
			vTaskSuspendAll();
		}
		#endif /* ipconfigETHERNET_DRIVER_FILTERS_PACKETS */

		uxListRemove( &( pxSocket->xBoundSocketListItem ) );

		#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 1 )
		{
			xTaskResumeAll();
		}
		#endif /* ipconfigETHERNET_DRIVER_FILTERS_PACKETS */
	}

	/* Now the socket is not bound the list of waiting packets can be
	drained. */
	if( pxSocket->ucProtocol == FREERTOS_IPPROTO_UDP )
	{
		while( listCURRENT_LIST_LENGTH( &( pxSocket->u.xUdp.xWaitingPacketsList ) ) > 0U )
		{
			pxNetworkBuffer = ( xNetworkBufferDescriptor_t * ) listGET_OWNER_OF_HEAD_ENTRY( &( pxSocket->u.xUdp.xWaitingPacketsList ) );
			uxListRemove( &( pxNetworkBuffer->xBufferListItem ) );
			vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
		}
	}

	if( pxSocket->xEventGroup )
	{
		vEventGroupDelete( pxSocket->xEventGroup );
	}

	#if( ipconfigUSE_TCP == 1 ) && ( ipconfigHAS_DEBUG_PRINTF != 0 )
	{
		if( pxSocket->ucProtocol == FREERTOS_IPPROTO_TCP )
		{
			FreeRTOS_debug_printf( ( "FreeRTOS_closesocket[%u to %lxip:%u]: buffers %lu socks %lu\n",
				pxSocket->usLocPort,
				pxSocket->u.xTcp.ulRemoteIP,
				pxSocket->u.xTcp.usRemotePort,
				uxGetNumberOfFreeNetworkBuffers(),
				listCURRENT_LIST_LENGTH( &xBoundTcpSocketsList ) ) );
		}
	}
	#endif /* ( ipconfigUSE_TCP == 1 ) && ( ipconfigHAS_DEBUG_PRINTF != 0 ) */

	/* Anf finally, after all resources have been freed, free the socket space */
	vPortFreeSocket( pxSocket );

	return 0;
} /* Tested */

/*-----------------------------------------------------------*/

#if ipconfigUSE_TCP == 1

	/*
	 * When a child socket gets closed, make sure to update the child-count of the
	 * parent.  When a listening parent socket is closed, make sure no child-sockets
	 * keep a pointer to it.
	 */
	static void prvTCPSetSocketCount( xFreeRTOS_Socket_t *pxSocketToDelete )
	{
	const ListItem_t *pxIterator;
	const MiniListItem_t *pxEnd = ( const MiniListItem_t* )listGET_END_MARKER( &xBoundTcpSocketsList );
	xFreeRTOS_Socket_t *pxOtherSocket;
	uint16_t usLocPort = pxSocketToDelete->usLocPort;

		for( pxIterator  = ( const ListItem_t * ) listGET_NEXT( pxEnd );
			 pxIterator != ( const ListItem_t * ) pxEnd;
			 pxIterator  = ( const ListItem_t * ) listGET_NEXT( pxIterator ) )
		{
			pxOtherSocket = ( xFreeRTOS_Socket_t * ) listGET_LIST_ITEM_OWNER( pxIterator );
			if( pxOtherSocket->u.xTcp.ucTcpState == eTCP_LISTEN &&
				pxOtherSocket->usLocPort == usLocPort &&
				pxOtherSocket->u.xTcp.usChildCount )
			{
				pxOtherSocket->u.xTcp.usChildCount--;
				FreeRTOS_debug_printf( ( "Lost: Socket %u now has %u / %u child%s\n",
					pxOtherSocket->usLocPort,
					pxOtherSocket->u.xTcp.usChildCount,
					pxOtherSocket->u.xTcp.usBacklog,
					pxOtherSocket->u.xTcp.usChildCount == 1 ? "" : "ren" ) );
				break;
			}
		}
	}

#endif /* ipconfigUSE_TCP == 1 */

/*-----------------------------------------------------------*/

BaseType_t FreeRTOS_setsockopt( xSocket_t xSocket, int32_t lLevel, int32_t lOptionName, const void *pvOptionValue, size_t xOptionLength )
{
/* The standard Berkeley function returns 0 for success. */
BaseType_t xReturn = -pdFREERTOS_ERRNO_EINVAL;
BaseType_t lOptionValue;
xFreeRTOS_Socket_t *pxSocket;

	pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;

	/* The function prototype is designed to maintain the expected Berkeley
	sockets standard, but this implementation does not use all the parameters. */
	( void ) lLevel;
	( void ) xOptionLength;

	configASSERT( xSocket );

	switch( lOptionName )
	{
		case FREERTOS_SO_RCVTIMEO	:
			/* Receive time out. */
			pxSocket->xReceiveBlockTime = *( ( TickType_t * ) pvOptionValue );
			xReturn = 0;
			break;

		case FREERTOS_SO_SNDTIMEO	:
			pxSocket->xSendBlockTime = *( ( TickType_t * ) pvOptionValue );
			if( pxSocket->ucProtocol == FREERTOS_IPPROTO_UDP )
			{
				/* The send time out is capped for the reason stated in the
				comments where ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS is defined
				in FreeRTOSIPConfig.h (assuming an official configuration file
				is being used. */
				if( pxSocket->xSendBlockTime > ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS )
				{
					pxSocket->xSendBlockTime = ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS;
				}
			}
			else
			{
				/* For TCP socket, it isn't necessary to limit the blocking time
				because	the FreeRTOS_send() function does not wait for a network
				buffer to become available. */
			}
			xReturn = 0;
			break;

		#if ipconfigUDP_MAX_RX_PACKETS > 0
			case FREERTOS_SO_UDP_MAX_RX_PACKETS:
				if( pxSocket->ucProtocol != FREERTOS_IPPROTO_UDP )
				{
					break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
				}
				pxSocket->u.xUdp.xMaxPackets = *( ( UBaseType_t * ) pvOptionValue );
				xReturn = 0;
				break;
		#endif /* ipconfigUDP_MAX_RX_PACKETS */

		case FREERTOS_SO_UDPCKSUM_OUT :
			/* Turn calculating of the UDP checksum on/off for this socket. */
			lOptionValue = ( BaseType_t ) pvOptionValue;

			if( lOptionValue == 0 )
			{
				pxSocket->ucSocketOptions &= ( uint8_t ) ~FREERTOS_SO_UDPCKSUM_OUT;
			}
			else
			{
				pxSocket->ucSocketOptions |= FREERTOS_SO_UDPCKSUM_OUT;
			}
			xReturn = 0;
			break;

		#if( ipconfigUSE_CALLBACKS == 1 )
			#if( ipconfigUSE_TCP == 1 )
				case FREERTOS_SO_TCP_CONN_HANDLER:	/* Set a callback for (dis)connection events */
				case FREERTOS_SO_TCP_RECV_HANDLER:	/* Install a callback for receiving TCP data. Supply pointer to 'FTcpUdpHandler_t' (see below) */
				case FREERTOS_SO_TCP_SENT_HANDLER:	/* Install a callback for sending TCP data. Supply pointer to 'FTcpUdpHandler_t' (see below) */
			#endif /* ipconfigUSE_TCP */
				case FREERTOS_SO_UDP_RECV_HANDLER:	/* Install a callback for receiving UDP data. Supply pointer to 'FTcpUdpHandler_t' (see below) */
				case FREERTOS_SO_UDP_SENT_HANDLER:	/* Install a callback for sending UDP data. Supply pointer to 'FTcpUdpHandler_t' (see below) */
					{
						#if( ipconfigUSE_TCP == 1 )
						{
							BaseType_t xProtocol =
								( lOptionName == FREERTOS_SO_UDP_RECV_HANDLER || lOptionName == FREERTOS_SO_UDP_SENT_HANDLER ) ?
								FREERTOS_IPPROTO_UDP : FREERTOS_IPPROTO_TCP;
							if( pxSocket->ucProtocol != xProtocol )
							{
								break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
							}
						}
						#else
						{
							/* No need to check if the socket has the right
							protocol, because only UDP socket can be created. */
						}
						#endif /* ipconfigUSE_TCP */

						switch( lOptionName )
						{
						#if ipconfigUSE_TCP == 1
							case FREERTOS_SO_TCP_CONN_HANDLER:
								pxSocket->u.xTcp.pHndConnected = ((FTcpUdpHandler_t *)pvOptionValue)->pOnTcpConnected;
								break;
							case FREERTOS_SO_TCP_RECV_HANDLER:
								pxSocket->u.xTcp.pHndReceive = ((FTcpUdpHandler_t *)pvOptionValue)->pOnTcpReceive;
								break;
							case FREERTOS_SO_TCP_SENT_HANDLER:
								pxSocket->u.xTcp.pHndSent = ((FTcpUdpHandler_t *)pvOptionValue)->pOnTcpSent;
								break;
						#endif /* ipconfigUSE_TCP */
						case FREERTOS_SO_UDP_RECV_HANDLER:
							pxSocket->u.xUdp.pHndReceive = ((FTcpUdpHandler_t *)pvOptionValue)->pOnUdpReceive;
							break;
						case FREERTOS_SO_UDP_SENT_HANDLER:
							pxSocket->u.xUdp.pHndSent = ((FTcpUdpHandler_t *)pvOptionValue)->pOnUdpSent;
							break;
						}
					}

					xReturn = 0;
					break;
		#endif /* ipconfigUSE_CALLBACKS */

		#if( ipconfigUSE_TCP != 0 )
			#if( ipconfigSOCKET_HAS_USER_SEMAPHORE != 0 )
				/* Each socket has a semaphore on which the using task normally
				sleeps. */
				case FREERTOS_SO_SET_SEMAPHORE:
					{
						pxSocket->pxUserSemaphore = *( ( SemaphoreHandle_t * ) pvOptionValue );
					}
					xReturn = 0;
					break;
			#endif /* ipconfigSOCKET_HAS_USER_SEMAPHORE */
			case FREERTOS_SO_SNDBUF:	/* Set the size of the send buffer, in units of MSS (TCP only) */
			case FREERTOS_SO_RCVBUF:	/* Set the size of the receive buffer, in units of MSS (TCP only) */
				{
					uint32_t ulNewValue;

					if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
					{
						FreeRTOS_debug_printf( ( "Set SO_%sBUF: wrong socket type\n",
							( lOptionName == FREERTOS_SO_SNDBUF ) ? "SND" : "RCV" ) );
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}

					if( ( ( lOptionName == FREERTOS_SO_SNDBUF ) && ( pxSocket->u.xTcp.txStream != NULL ) ) ||
						( ( lOptionName == FREERTOS_SO_RCVBUF ) && ( pxSocket->u.xTcp.rxStream != NULL ) ) )
					{
						FreeRTOS_debug_printf( ( "Set SO_%sBUF: buffer already created\n",
							( lOptionName == FREERTOS_SO_SNDBUF ) ? "SND" : "RCV" ) );
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}

					ulNewValue = *( ( uint32_t * ) pvOptionValue );

					if( lOptionName == FREERTOS_SO_SNDBUF )
					{
						/* Round up to nearest MSS size */
						ulNewValue = FreeRTOS_round_up( ulNewValue, pxSocket->u.xTcp.usInitMSS );
						pxSocket->u.xTcp.txStreamSize = ( int32_t ) ulNewValue;
					}
					else
					{
						pxSocket->u.xTcp.rxStreamSize = ( int32_t ) ulNewValue;
					}
				}
				xReturn = 0;
				break;

			case FREERTOS_SO_WIN_PROPERTIES:	/* Set all buffer and window properties in one call, parameter is pointer to xWinProperties_t */
				{
					xWinProperties_t* pxProps;

					if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
					{
						FreeRTOS_debug_printf( ( "Set SO_WIN_PROP: wrong socket type\n" ) );
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}

					if( ( pxSocket->u.xTcp.txStream != NULL ) || ( pxSocket->u.xTcp.rxStream != NULL ) )
					{
						FreeRTOS_debug_printf( ( "Set SO_WIN_PROP: buffer already created\n" ) );
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}

					pxProps = ( ( xWinProperties_t * ) pvOptionValue );
					FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_SNDBUF, &( pxProps->lTxBufSize ), sizeof( pxProps->lTxBufSize ) );
					FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_RCVBUF, &( pxProps->lRxBufSize ), sizeof( pxProps->lRxBufSize ) );
					#if( ipconfigUSE_TCP_WIN == 1 )
					{
						pxSocket->u.xTcp.ulRxWinSize = ( uint32_t )pxProps->lRxWinSize;	/* Fixed value: size of the TCP reception window */
						pxSocket->u.xTcp.ulTxWinSize = ( uint32_t )pxProps->lTxWinSize;	/* Fixed value: size of the TCP transmit window */
					}
					#else
					{
						pxSocket->u.xTcp.ulRxWinSize = 1;
						pxSocket->u.xTcp.ulTxWinSize = 1;
					}
					#endif

					/* In case the socket has already initialised its tcpWin,
					adapt the window size parameters */
					if( pxSocket->u.xTcp.xTcpWindow.u.bits.bHasInit != pdFALSE )
					{
						pxSocket->u.xTcp.xTcpWindow.xSize.ulRxWindowLength = pxSocket->u.xTcp.ulRxWinSize * pxSocket->u.xTcp.usInitMSS;
						pxSocket->u.xTcp.xTcpWindow.xSize.ulRxWindowLength = pxSocket->u.xTcp.ulTxWinSize * pxSocket->u.xTcp.usInitMSS;
					}
				}

				xReturn = 0;
				break;

			case FREERTOS_SO_REUSE_LISTEN_SOCKET:	/* If true, the server-socket will turn into a connected socket */
				{
					if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
					{
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}
					pxSocket->u.xTcp.bits.bReuseSocket = *( ( BaseType_t * ) pvOptionValue ) != 0;
				}
				xReturn = 0;
				break;

			case FREERTOS_SO_CLOSE_AFTER_SEND:		/* As soon as the last byte has been transmitted, finalise the connection */
				{
					if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
					{
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}
					pxSocket->u.xTcp.bits.bCloseAfterSend = *( ( BaseType_t * ) pvOptionValue ) != 0;
				}
				xReturn = 0;
				break;

			case FREERTOS_SO_SET_FULL_SIZE:		/* Refuse to send packets smaller than MSS  */
				{
					if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
					{
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}
					pxSocket->u.xTcp.xTcpWindow.u.bits.bSendFullSize = *( ( BaseType_t * ) pvOptionValue ) != 0;
					if( ( pxSocket->u.xTcp.xTcpWindow.u.bits.bSendFullSize == 0 ) &&
						( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED ) &&
						( FreeRTOS_outstanding( pxSocket ) != 0 ) )
					{
						pxSocket->u.xTcp.usTimeout = 1; /* to set/clear bSendFullSize */
						xSendEventToIPTask( eTCPTimerEvent );
					}
				}
				xReturn = 0;
				break;

			case FREERTOS_SO_STOP_RX:		/* Refuse to receive more packts */
				{
					if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
					{
						break;	/* will return -pdFREERTOS_ERRNO_EINVAL */
					}
					pxSocket->u.xTcp.bits.bRxStopped = *( ( BaseType_t * ) pvOptionValue ) != 0;
					pxSocket->u.xTcp.bits.bWinChange = pdTRUE;
					pxSocket->u.xTcp.usTimeout = 1; /* to set/clear bRxStopped */
					xSendEventToIPTask( eTCPTimerEvent );
				}
				xReturn = 0;
				break;

		#endif  /* ipconfigUSE_TCP == 1 */

		default :
			/* No other options are handled. */
			xReturn = -pdFREERTOS_ERRNO_ENOPROTOOPT;
			break;
	}

	return xReturn;
} /* Tested */

/*-----------------------------------------------------------*/

/* Get a free private ('anonymous') port number */
static uint16_t prvGetPrivatePortNumber( BaseType_t ulProtocol )
{
uint16_t usResult;
BaseType_t xIndex;
const List_t *pxList;

#if ipconfigUSE_TCP == 1
	if( ulProtocol == FREERTOS_IPPROTO_TCP )
	{
		xIndex = NXT_TCP_PORT_IDX;
		pxList = &xBoundTcpSocketsList;
	}
	else
#endif
	{
		xIndex = NXT_UDP_PORT_IDX;
		pxList = &xBoundUdpSocketsList;
	}

	/* Avoid compiler warnings if ipconfigUSE_TCP is not defined. */
	( void ) ulProtocol;

	/* Assign the next port in the range.  Has it overflowed? */
	for ( ; ; )
	{
		if( ++ usNextPortToUse[xIndex] >= socketAUTO_PORT_ALLOCATION_MAX_NUMBER )
		{
			/* Don't go right back to the start of the dynamic/private port
			range numbers as any persistent sockets are likely to have been
			create first so the early port numbers may still be in use. */
			usNextPortToUse[ xIndex ] = socketAUTO_PORT_ALLOCATION_RESET_NUMBER;
		}

		usResult = FreeRTOS_htons( usNextPortToUse[ xIndex ] );

		if( pxListFindListItemWithValue( pxList, ( TickType_t ) usResult ) == NULL )
		{
			break;
		}
	}
	return usResult;
} /* Tested */
/*-----------------------------------------------------------*/

/* pxListFindListItemWithValue: find a list item in a bound socket list
'xWantedItemValue' refers to a port number */
static const ListItem_t * pxListFindListItemWithValue( const List_t *pxList, TickType_t xWantedItemValue )
{
const ListItem_t * pxResult = NULL;

	if( ( xIPIsNetworkTaskReady() != pdFALSE ) && ( pxList != NULL ) )
	{
		const ListItem_t *pxIterator;
		const MiniListItem_t *pxEnd = ( const MiniListItem_t* )listGET_END_MARKER( pxList );
		for( pxIterator  = ( const ListItem_t * ) listGET_NEXT( pxEnd );
			 pxIterator != ( const ListItem_t * ) pxEnd;
			 pxIterator  = ( const ListItem_t * ) listGET_NEXT( pxIterator ) )
		{
			if( listGET_LIST_ITEM_VALUE( pxIterator ) == xWantedItemValue )
			{
				pxResult = pxIterator;
				break;
			}
		}
	}

	return pxResult;
} /* Tested */

/*-----------------------------------------------------------*/

xFreeRTOS_Socket_t *pxUDPSocketLookup( BaseType_t xLocalPort )
{
const ListItem_t *pxListItem;
xFreeRTOS_Socket_t *pxSocket = NULL;

	/* Looking up a socket is quite simple, find a match with the local port.

	See if there is a list item associated with the port number on the
	list of bound sockets. */
	pxListItem = pxListFindListItemWithValue( &xBoundUdpSocketsList, ( TickType_t ) xLocalPort );

	if( pxListItem != NULL )
	{
		/* The owner of the list item is the socket itself. */
		pxSocket = ( xFreeRTOS_Socket_t * ) listGET_LIST_ITEM_OWNER( pxListItem );
		configASSERT( pxSocket != NULL );
	}
	return pxSocket;
}

/*-----------------------------------------------------------*/

#if ipconfigINCLUDE_FULL_INET_ADDR == 1

	uint32_t FreeRTOS_inet_addr( const char * pcIPAddress )
	{
	const uint32_t ulDecimalBase = 10;
	uint8_t ucOctet[ socketMAX_IP_ADDRESS_OCTETS ];
	const char *pcPointerOnEntering;
	uint32_t ulReturn = 0UL, ulOctetNumber, ulValue;
	BaseType_t xResult = pdPASS;

		for( ulOctetNumber = 0; ulOctetNumber < socketMAX_IP_ADDRESS_OCTETS; ulOctetNumber++ )
		{
			ulValue = 0;
			pcPointerOnEntering = pcIPAddress;

			while( ( *pcIPAddress >= '0' ) && ( *pcIPAddress <= '9' ) )
			{
				/* Move previous read characters into the next decimal
				position. */
				ulValue *= ulDecimalBase;

				/* Add the binary value of the ascii character. */
				ulValue += ( ( uint32_t ) ( *pcIPAddress ) - ( uint32_t ) '0' );

				/* Move to next character in the string. */
				pcIPAddress++;
			}

			/* Check characters were read. */
			if( pcIPAddress == pcPointerOnEntering )
			{
				xResult = pdFAIL;
			}

			/* Check the value fits in an 8-bit number. */
			if( ulValue > 0xffUL )
			{
				xResult = pdFAIL;
			}
			else
			{
				ucOctet[ ulOctetNumber ] = ( uint8_t ) ulValue;

				/* Check the next character is as expected. */
				if( ulOctetNumber < ( socketMAX_IP_ADDRESS_OCTETS - 1 ) )
				{
					if( *pcIPAddress != '.' )
					{
						xResult = pdFAIL;
					}
					else
					{
						/* Move past the dot. */
						pcIPAddress++;
					}
				}
			}

			if( xResult == pdFAIL )
			{
				/* No point going on. */
				break;
			}
		}

		if( *pcIPAddress != 0x00 )
		{
			/* Expected the end of the string. */
			xResult = pdFAIL;
		}

		if( ulOctetNumber != socketMAX_IP_ADDRESS_OCTETS )
		{
			/* Didn't read enough octets. */
			xResult = pdFAIL;
		}

		if( xResult == pdPASS )
		{
			ulReturn = FreeRTOS_inet_addr_quick( ucOctet[ 0 ], ucOctet[ 1 ], ucOctet[ 2 ], ucOctet[ 3 ] );
		}

		return ulReturn;
	}

#endif /* ipconfigINCLUDE_FULL_INET_ADDR */

/*-----------------------------------------------------------*/

/* Function to get the local address and IP port */
BaseType_t FreeRTOS_GetLocalAddress( xSocket_t xSocket, struct freertos_sockaddr *pxAddress )
{
xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;

	/* IP address of local machine. */
	pxAddress->sin_addr = *ipLOCAL_IP_ADDRESS_POINTER;

	/* Local port on this machine. */
	pxAddress->sin_port = FreeRTOS_htons( pxSocket->usLocPort );

	return sizeof( *pxAddress );
}

/*-----------------------------------------------------------*/

void vWakeUpSocketUser( xFreeRTOS_Socket_t *pxSocket )
{
/* _HT_ must work this out, now vWakeUpSocketUser will be called for any important
 * event or transition */
	#if( ipconfigSOCKET_HAS_USER_SEMAPHORE == 1 )
	{
		if( pxSocket->pxUserSemaphore != NULL )
		{
			xSemaphoreGive( pxSocket->pxUserSemaphore );
		}
	}
	#endif /* ipconfigSOCKET_HAS_USER_SEMAPHORE */

	#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
	{
		if( pxSocket->pxSocketSet )
		{
			EventBits_t xSelectBits = ( pxSocket->xEventBits >> SOCKET_EVENT_BIT_COUNT ) & eSELECT_ALL;
			if( xSelectBits != 0ul )
			{
				pxSocket->xSocketBits |= xSelectBits;
				xEventGroupSetBits( pxSocket->pxSocketSet->xSelectGroup, xSelectBits );
			}
		}

		pxSocket->xEventBits &= eSOCKET_ALL;
	}
	#endif /* ipconfigSUPPORT_SELECT_FUNCTION */

	if( ( pxSocket->xEventGroup != NULL ) && ( pxSocket->xEventBits != 0 ) )
	{
		xEventGroupSetBits( pxSocket->xEventGroup, pxSocket->xEventBits );
	}

	pxSocket->xEventBits = 0ul;
}

/*-----------------------------------------------------------*/

#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 1 )

	/* This define makes it possible for network-card drivers to inspect
	 * UDP message and see if there is any UDP socket bound to a given port
	 * number.
	 * This is probably only usefull in systems with a minimum of RAM and
	 * when lots of anonymous broadcast messages come in
	 */
	BaseType_t xPortHasUdpSocket( uint16_t usPortNr )
	{
		BaseType_t bFound;
		vTaskSuspendAll();
		bFound = pxListFindListItemWithValue( &xBoundUdpSocketsList, ( TickType_t ) usPortNr ) != NULL;
		xTaskResumeAll();
		return bFound;
	}

#endif /* ipconfigETHERNET_DRIVER_FILTERS_PACKETS */

/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	static BaseType_t bMayConnect( xFreeRTOS_Socket_t *pxSocket )
	{
		switch( pxSocket->u.xTcp.ucTcpState )
		{
			case eCLOSED:
			case eCLOSE_WAIT:	return 0;
			case eCONNECT_SYN:	return -pdFREERTOS_ERRNO_EINPROGRESS;
			default:			return -pdFREERTOS_ERRNO_EAGAIN;
		}
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	static BaseType_t prvTCPConnectStart( xFreeRTOS_Socket_t *pxSocket, struct freertos_sockaddr *pxAddress )
	{
	BaseType_t xResult = 0;

		if( prvValidSocket( pxSocket, FREERTOS_IPPROTO_TCP, pdFALSE ) == pdFALSE )
		{
			/* Not a valid socket or wrong type */
			xResult = -pdFREERTOS_ERRNO_EBADF;
		}
		else if( FreeRTOS_issocketconnected( pxSocket ) > 0 )
		{
			/* The socket is already connected. */
			xResult = -pdFREERTOS_ERRNO_EISCONN;
		}
		else if( socketSOCKET_IS_BOUND( pxSocket ) == pdFALSE )
		{
			/* Bind the socket to the port that the client task will send to.
			Non-standard, so the error returned is that returned by bind(). */
			xResult = FreeRTOS_bind( ( xSocket_t ) pxSocket, NULL, 0 );
		}

		if( xResult == 0 )
		{
			/* Check if it makes any sense to wait for a connect event, this condition
			might change while sleeping, so it must be checked within each loop */
			xResult = bMayConnect( pxSocket ); /* -EINPROGRESS, -EAGAIN, or 0 for OK */

			/* Start the connect procedure, kernel will start working on it */
			if( xResult == 0 )
			{
				pxSocket->u.xTcp.bits.bConnPrepared = pdFALSE;
				pxSocket->u.xTcp.ucRepCount = 0;

				FreeRTOS_debug_printf( ( "FreeRTOS_connect: %u to %lxip:%u\n",
					pxSocket->usLocPort, FreeRTOS_ntohl( pxAddress->sin_addr ), FreeRTOS_ntohs( pxAddress->sin_port ) ) );

				/* Port on remote machine. */
				pxSocket->u.xTcp.usRemotePort = FreeRTOS_ntohs( pxAddress->sin_port );

				/* IP address of remote machine. */
				pxSocket->u.xTcp.ulRemoteIP = FreeRTOS_ntohl( pxAddress->sin_addr );

				/* (client) internal state: socket wants to send a connect. */
				vTCPStateChange( pxSocket, eCONNECT_SYN );

				/* To start an active connect. */
				pxSocket->u.xTcp.usTimeout = 1;

				if( xSendEventToIPTask( eTCPTimerEvent ) != pdPASS )
				{
					xResult = -pdFREERTOS_ERRNO_ECANCELED;
				}
			}
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * FreeRTOS_connect: socket wants to connect to a remote port
	 */
	BaseType_t FreeRTOS_connect( xSocket_t xClientSocket, struct freertos_sockaddr *pxAddress, socklen_t xAddressLength )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t* ) xClientSocket;
	TickType_t xRemainingTime;
	BaseType_t xTimed = pdFALSE;
	BaseType_t xResult;
	TimeOut_t xTimeOut;

		( void ) xAddressLength;

		xResult = prvTCPConnectStart( pxSocket, pxAddress );

		if( xResult == 0 )
		{
			/* And wait for the result */
			for( ;; )
			{
				if( xTimed == pdFALSE )
				{
					/* Only in the first round, check for non-blocking */
					xRemainingTime = pxSocket->xReceiveBlockTime;
					if( xRemainingTime == 0 )
					{
						/* Not yet connected, correct state, non-blocking. */
						xResult = -pdFREERTOS_ERRNO_EWOULDBLOCK;
						break;
					}

					/* Don't get here a second time. */
					xTimed = pdTRUE;

					/* Fetch the current time */
					vTaskSetTimeOutState( &xTimeOut );
				}

				/* Did it get connected while sleeping ? */
				xResult = FreeRTOS_issocketconnected( pxSocket );

				/* Returns positive when connected, negative means an error */
				if( xResult < 0 )
				{
					/* Return the error */
					break;
				}

				if( xResult > 0 )
				{
					/* Socket now connected, return a zero */
					xResult = 0;
					break;
				}

				/* Is it allowed to sleep more? */
				if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTime ) )
				{
					xResult = -pdFREERTOS_ERRNO_ETIMEDOUT;
					break;
				}

				/* Go sleeping until we get any down-stream event */
				xEventGroupWaitBits( pxSocket->xEventGroup, eSOCKET_CONNECT, pdTRUE /*xClearOnExit*/, pdFALSE /*xWaitAllBits*/, xRemainingTime );
			}
		}

		return xResult;
	}
#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * FreeRTOS_accept: can return a new connected socket
	 * if the server socket is in listen mode and receives a connection request
	 * The new socket will be bound already to the same port number as the listing
	 * socket.
	 */
	xSocket_t FreeRTOS_accept( xSocket_t xServerSocket, struct freertos_sockaddr *pxAddress, socklen_t *pxAddressLength )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xServerSocket;
	xFreeRTOS_Socket_t *pxClientSocket = NULL;
	TickType_t xRemainingTime;
	BaseType_t xTimed = pdFALSE;
	BaseType_t pdAsk = pdFALSE;
	TimeOut_t xTimeOut;
	xIPStackEvent_t xAskEvent;

		if( prvValidSocket( pxSocket, FREERTOS_IPPROTO_TCP, pdTRUE ) == pdFALSE )
		{
			/* Not a valid socket or wrong type */
			pxClientSocket = ( xFreeRTOS_Socket_t * ) FREERTOS_INVALID_SOCKET;
		}
		else if( ( pxSocket->u.xTcp.bits.bReuseSocket == pdFALSE ) &&
				 ( pxSocket->u.xTcp.ucTcpState != eTCP_LISTEN ) )
		{
			/* Parent socket is not in listening mode */
			pxClientSocket = ( xFreeRTOS_Socket_t * ) FREERTOS_INVALID_SOCKET;
		}
		else
		{

			/* Loop will stop with breaks. */
			for( ; ; )
			{
				/* Is there a new client? */
				vTaskSuspendAll();
				{
					if( pxSocket->u.xTcp.bits.bReuseSocket == pdFALSE )
					{
						pxClientSocket = pxSocket->u.xTcp.pxPeerSocket;
					}
					else
					{
						pxClientSocket = pxSocket;
					}
					if( pxClientSocket != NULL )
					{
						pxSocket->u.xTcp.pxPeerSocket = NULL;

						/* Is it still not taken ? */
						if( pxClientSocket->u.xTcp.bits.bPassAccept )
						{
							pxClientSocket->u.xTcp.bits.bPassAccept = pdFALSE;
						}
						else
						{
							pxClientSocket = NULL;
						}
					}
				}
				xTaskResumeAll();

				if( pxClientSocket != NULL )
				{
					if( pxAddress != NULL )
					{
						/* IP address of remote machine. */
						pxAddress->sin_addr = FreeRTOS_ntohl( pxClientSocket->u.xTcp.ulRemoteIP );

						/* Port on remote machine. */
						pxAddress->sin_port = FreeRTOS_ntohs( pxClientSocket->u.xTcp.usRemotePort );
					}
					if( pxAddressLength != NULL )
					{
						*pxAddressLength = sizeof( *pxAddress );
					}

					if( pxSocket->u.xTcp.bits.bReuseSocket == pdFALSE )
					{
						pdAsk = pdTRUE;
					}
				}

				if( pdAsk != pdFALSE )
				{
					/* Ask to set an event in 'xEventGroup' as soon as a new
					client gets connected for this listening socket. */
					xAskEvent.eEventType = eTCPAcceptEvent;
					xAskEvent.pvData = ( void * ) pxSocket;
					xSendEventStructToIPTask( &xAskEvent, portMAX_DELAY );
				}

				if( pxClientSocket != NULL )
				{
					break;
				}

				if( xTimed == pdFALSE )
				{
					/* Only in the first round, check for non-blocking */
					xRemainingTime = pxSocket->xReceiveBlockTime;
					if( xRemainingTime == 0 )
					{
						break;
					}

					/* Don't get here a second time */
					xTimed = pdTRUE;

					/* Fetch the current time */
					vTaskSetTimeOutState( &xTimeOut );
				}

				/* Has the timeout been reached? */
				if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTime ) != pdFALSE )
				{
					break;
				}

				/* Go sleeping until we get any down-stream event */
				xEventGroupWaitBits( pxSocket->xEventGroup, eSOCKET_ACCEPT, pdTRUE /*xClearOnExit*/, pdFALSE /*xWaitAllBits*/, xRemainingTime );
			}
		}
		return ( xSocket_t ) pxClientSocket;
	}
#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * Read incoming data from a TCP socket
	 * Only after the last byte has been read, a close error might be returned
	 */
	BaseType_t FreeRTOS_recv( xSocket_t xSocket, void *pvBuffer, size_t xBufferLength, BaseType_t xFlags )
	{
	BaseType_t lByteCount;
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	TickType_t xRemainingTime;
	BaseType_t xTimed = pdFALSE;
	TimeOut_t xTimeOut;

		/* Check if the socket is valid, has type TCP and if it is bound to a
		port. */
		if( prvValidSocket( pxSocket, FREERTOS_IPPROTO_TCP, pdTRUE ) == pdFALSE )
		{
			lByteCount = -pdFREERTOS_ERRNO_EINVAL;
		}
		else
		{
			lByteCount = pxSocket->u.xTcp.rxStream ? lStreamBufferGetSize ( pxSocket->u.xTcp.rxStream ) : 0;

			while( lByteCount == 0 )
			{
				switch( pxSocket->u.xTcp.ucTcpState )
				{
				case eCLOSED:
				case eCLOSE_WAIT:	/* (server + client) waiting for a connection termination request from the local user. */
				case eCLOSING:		/* (server + client) waiting for a connection termination request acknowledgment from the remote TCP. */
					lByteCount = pxSocket->u.xTcp.bits.bMallocError ? -pdFREERTOS_ERRNO_ENOMEM : -pdFREERTOS_ERRNO_ENOTCONN;
					/* Call continue to break out of the switch and also the while
					loop. */
					continue;
				}

				if( xTimed == pdFALSE )
				{
					/* Only in the first round, check for non-blocking. */
					xRemainingTime = pxSocket->xReceiveBlockTime;

					if( xRemainingTime == 0 )
					{
						break;
					}

					if( ( xFlags & FREERTOS_MSG_DONTWAIT ) != 0 )
					{
						break;
					}

					/* Don't get here a second time. */
					xTimed = pdTRUE;

					/* Fetch the current time. */
					vTaskSetTimeOutState( &xTimeOut );
				}

				/* Has the timeout been reached? */
				if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTime ) != pdFALSE )
				{
					break;
				}

				/* Block until there is a down-stream event. */
				xEventGroupWaitBits( pxSocket->xEventGroup, eSOCKET_RECEIVE|eSOCKET_CLOSED, pdTRUE /*xClearOnExit*/, pdFALSE /*xWaitAllBits*/, xRemainingTime );

				if( pxSocket->u.xTcp.rxStream != NULL )
				{
					lByteCount = lStreamBufferGetSize ( pxSocket->u.xTcp.rxStream );
				}
				else
				{
					lByteCount = 0;
				}
			}

			if( lByteCount > 0 )
			{
				if( ( xFlags & FREERTOS_ZERO_COPY ) == 0 )
				{
					lByteCount = lStreamBufferGet( pxSocket->u.xTcp.rxStream, 0, ( uint8_t * ) pvBuffer, ( int32_t ) xBufferLength, (xFlags & FREERTOS_MSG_PEEK) != 0 );
					if( pxSocket->u.xTcp.bits.bLowWater != 0 )
					{
						/* We had reached the low-water mark, now see if the flag
						can be cleared */
						int32_t lFrontSpace = lStreamBufferFrontSpace( pxSocket->u.xTcp.rxStream );

						if( lFrontSpace >= pxSocket->u.xTcp.lEnoughSpace )
						{
							pxSocket->u.xTcp.bits.bLowWater = pdFALSE;
							pxSocket->u.xTcp.bits.bWinChange = pdTRUE;
							pxSocket->u.xTcp.usTimeout = 1; /* because bLowWater is cleared. */
							xSendEventToIPTask( eTCPTimerEvent );
						}
					}
				}
				else
				{
					lByteCount = lStreamBufferGetPtr( pxSocket->u.xTcp.rxStream, (uint8_t **)pvBuffer );
				}
			}
		}

		return lByteCount;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	static int32_t prvTCPSendCheck( xFreeRTOS_Socket_t *pxSocket, size_t xDataLength )
	{
	int32_t xResult = 1;

		/* Is this a socket of type TCP and is it already bound to a port number ? */
		if( prvValidSocket( pxSocket, FREERTOS_IPPROTO_TCP, pdTRUE ) == pdFALSE )
		{
			xResult = -pdFREERTOS_ERRNO_EINVAL;
		}
		else if( pxSocket->u.xTcp.bits.bMallocError )
		{
			xResult = -pdFREERTOS_ERRNO_ENOMEM;
		}
		else if( pxSocket->u.xTcp.ucTcpState == eCLOSED )
		{
			xResult = -pdFREERTOS_ERRNO_ENOTCONN;
		}
		else if( pxSocket->u.xTcp.bits.bFinSent )
		{
			/* This TCP connection is closing already, the FIN flag has been sent.
			Maybe it is still delivering or receiving data.
			Return OK in order not to get closed/deleted too quickly */
			xResult = 0;
		}
		else if( xDataLength == 0 )
		{
			/* send() is being called to send zero bytes */
			xResult = 0;
		}
		else if( pxSocket->u.xTcp.txStream == NULL )
		{
			/* Create the outgoing stream only when it is needed */
			prvTcpCreateStream( pxSocket, pdFALSE );

			if( pxSocket->u.xTcp.txStream == NULL )
			{
				xResult = -pdFREERTOS_ERRNO_ENOMEM;
			}
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * Send data using a TCP socket.  It is not necessary to have the socket
	 * connected already.  Outgoing data will be stored and delivered as soon as
	 * the socket gets connected.
	 */
	BaseType_t FreeRTOS_send( xSocket_t xSocket, const void *pvBuffer, size_t xDataLength, BaseType_t xFlags )
	{
	int32_t lByteCount;
	int32_t lBytesLeft;
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	TickType_t xRemainingTime;
	BaseType_t xTimed = pdFALSE;
	TimeOut_t xTimeOut;
	BaseType_t xCloseAfterSend;

		/* Prevent compiler warnings about unused parameters.  The parameter
		may be used in future versions. */
		( void ) xFlags;

		lByteCount = prvTCPSendCheck( pxSocket, xDataLength );

		if( lByteCount > 0 )
		{
			/* lBytesLeft is number of bytes to send, will count to zero. */
			lBytesLeft = ( int32_t ) xDataLength;

			/* lByteCount is number of bytes that can be sent now. */
			lByteCount = lStreamBufferGetSpace( pxSocket->u.xTcp.txStream );

			/* While there are still bytes to be sent. */
			while( lBytesLeft > 0 )
			{
				/* If txStream has space. */
				if( lByteCount > 0 )
				{
					/* Don't send more than necessary. */
					if( lByteCount > lBytesLeft )
					{
						lByteCount = lBytesLeft;
					}

					/* Is the close-after-send flag set and is this really the
					last transmission? */
					if( ( pxSocket->u.xTcp.bits.bCloseAfterSend != 0 ) && ( lByteCount == lBytesLeft ) )
					{
						xCloseAfterSend = pdTRUE;
					}
					else
					{
						xCloseAfterSend = pdFALSE;
					}

					/* The flag 'bCloseAfterSend' can be set before sending data
					using setsockopt()

					When the last data packet is being sent out, a FIN flag will
					be included to let the peer know that no more data is to be
					expected.  The use of 'bCloseAfterSend' is not mandatory, it
					is just a faster way of transferring files (e.g. when using
					FTP). */
					if( xCloseAfterSend != pdFALSE )
					{
						/* Now suspend the scheduler: sending the last data	and
						setting bCloseRequested must be done together */
						vTaskSuspendAll();
						pxSocket->u.xTcp.bits.bCloseRequested = pdTRUE;
					}

					lByteCount = lStreamBufferAdd( pxSocket->u.xTcp.txStream, 0, ( const uint8_t * ) pvBuffer, lByteCount );

					if( xCloseAfterSend != pdFALSE )
					{
						/* Now when the IP-task transmits the data, it will also
						see	that bCloseRequested is true and include the FIN
						flag to start closure of the connection. */
						xTaskResumeAll();
					}

					/* Send a message to the IP-task so it can work on this
					socket.  Data is sent, let the IP-task work on it. */
					pxSocket->u.xTcp.usTimeout = 1;

					if( xIsCallingFromIPTask() == pdFALSE )
					{
						/* Only send a TCP timer event when not called from the
						IP-task. */
						xSendEventToIPTask( eTCPTimerEvent );
					}

					lBytesLeft -= lByteCount;

					if( lBytesLeft == 0 )
					{
						break;
					}

					/* As there are still bytes left to be sent, increase the
					data pointer. */
					pvBuffer = ( void * ) ( ( ( const uint8_t * ) pvBuffer) + lByteCount );
				}

				/* Not all bytes have been sent. In case the socket is marked as
				blocking sleep for a while. */
				if( xTimed == pdFALSE )
				{
					/* Only in the first round, check for non-blocking. */
					xRemainingTime = pxSocket->xSendBlockTime;

					#if( ipconfigUSE_CALLBACKS != 0 )
					{
						if( xIsCallingFromIPTask() != pdFALSE )
						{
							/* If this send function is called from within a
							call-back handler it may not block, otherwise
							chances would be big to get a deadlock: the IP-task
							waiting for	itself. */
							xRemainingTime = 0;
						}
					}
					#endif /* ipconfigUSE_CALLBACKS */

					if( xRemainingTime == 0 )
					{
						break;
					}

					if( ( xFlags & FREERTOS_MSG_DONTWAIT ) != 0 )
					{
						break;
					}

					/* Don't get here a second time. */
					xTimed = pdTRUE;

					/* Fetch the current time. */
					vTaskSetTimeOutState( &xTimeOut );
				}
				else
				{
					/* Has the timeout been reached? */
					if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTime ) != pdFALSE )
					{
						break;
					}
				}

				/* Go sleeping until down-stream events are received. */
				xEventGroupWaitBits( pxSocket->xEventGroup, eSOCKET_SEND|eSOCKET_CLOSED, pdTRUE /*xClearOnExit*/, pdFALSE /*xWaitAllBits*/, xRemainingTime );

				lByteCount = lStreamBufferGetSpace( pxSocket->u.xTcp.txStream );
			}

			/* How much was actually sent? */
			lByteCount = ( ( int32_t ) xDataLength ) - lBytesLeft;

			if( lByteCount == 0 )
			{
				if( pxSocket->u.xTcp.ucTcpState > eESTABLISHED )
				{
					lByteCount = ( int32_t ) -pdFREERTOS_ERRNO_ENOTCONN;
				}
				else
				{
					if( ipconfigTCP_MAY_LOG_PORT( pxSocket->usLocPort ) != pdFALSE )
					{
						FreeRTOS_debug_printf( ( "FreeRTOS_send: %u -> %lxip:%d: no space\n",
							pxSocket->usLocPort,
							pxSocket->u.xTcp.ulRemoteIP,
							pxSocket->u.xTcp.usRemotePort ) );
					}

					lByteCount = ( int32_t ) -pdFREERTOS_ERRNO_ENOSPC;
				}
			}
		}

		return lByteCount;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * Request to put a socket in listen mode
	 */
	BaseType_t FreeRTOS_listen( xSocket_t xSocket, BaseType_t xBacklog )
	{
	xFreeRTOS_Socket_t *pxSocket;
	BaseType_t xResult = 0;

		pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;

		/* listen() is allowed for a valid TCP socket in Closed state and already
		bound. */
		if( prvValidSocket( pxSocket, FREERTOS_IPPROTO_TCP, pdTRUE ) == pdFALSE )
		{
			xResult = -pdFREERTOS_ERRNO_EOPNOTSUPP;
		}
		else if( ( pxSocket->u.xTcp.ucTcpState != eCLOSED ) && ( pxSocket->u.xTcp.ucTcpState != eCLOSE_WAIT ) )
		{
			/* Socket is in a wrong state. */
			xResult = -pdFREERTOS_ERRNO_EOPNOTSUPP;
		}
		else
		{
			/* Backlog is interpreted here as "the maximum number of child
			sockets. */
			pxSocket->u.xTcp.usBacklog = ( uint16_t )FreeRTOS_min_int32( 0xffff, xBacklog );

			/* This cleaning is necessary only if a listening socket is being
			reused as it might have had a previous connection. */
			if( pxSocket->u.xTcp.bits.bReuseSocket )
			{
				if( pxSocket->u.xTcp.rxStream ) vStreamBufferClear( pxSocket->u.xTcp.rxStream );
				if( pxSocket->u.xTcp.txStream ) vStreamBufferClear( pxSocket->u.xTcp.txStream );
				memset( pxSocket->u.xTcp.lastPacket, '\0', sizeof( pxSocket->u.xTcp.lastPacket ) );
				memset( &pxSocket->u.xTcp.xTcpWindow, '\0', sizeof( pxSocket->u.xTcp.xTcpWindow ) );
				memset( &pxSocket->u.xTcp.bits, '\0', sizeof( pxSocket->u.xTcp.bits ) );
				/* Now set the bReuseSocket flag again, because the bits have
				just been cleared. */
				pxSocket->u.xTcp.bits.bReuseSocket = pdTRUE;
			}

			vTCPStateChange( pxSocket, eTCP_LISTEN );
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* shutdown - shut down part of a full-duplex connection */
	BaseType_t FreeRTOS_shutdown( xSocket_t xSocket, BaseType_t xHow )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xResult;

		if( prvValidSocket( pxSocket, FREERTOS_IPPROTO_TCP, pdTRUE ) == pdFALSE )
		{
			/*_RB_ Is this comment correct?  The socket is not of a type that
			supports the listen() operation. */
			xResult = -pdFREERTOS_ERRNO_EOPNOTSUPP;
		}
		else if ( pxSocket->u.xTcp.ucTcpState != eESTABLISHED )
		{
			/*_RB_ Is this comment correct?  The socket is not of a type that
			supports the listen() operation. */
			xResult = -pdFREERTOS_ERRNO_EOPNOTSUPP;
		}
		else
		{
			pxSocket->u.xTcp.bits.bUserShutdown = pdTRUE;

			/* Let the IP-task perform the shutdown of the connection. */
			pxSocket->u.xTcp.usTimeout = 1;
			xSendEventToIPTask( eTCPTimerEvent );
			xResult = 0;
		}
		(void) xHow;

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * The xTcpTimer has expired, now check all TCP sockets for:
	 * - Active connect
	 * - Send a delayed ACK
	 * - Send new data
	 * - Send a keep-alive packet
	 * - Check for timeout (in non-connected states only)
	 */
	TickType_t xTCPTimerCheck( BaseType_t xWillSleep )
	{
	xFreeRTOS_Socket_t *pxSocket;
	TickType_t xShortest = ( TickType_t ) 10000;
	TickType_t xNow = xTaskGetTickCount();
	static TickType_t xLastTime = 0;
	TickType_t xDelta = xNow - xLastTime;
	ListItem_t* pxEnd = ( ListItem_t * ) listGET_END_MARKER( &xBoundTcpSocketsList );
	ListItem_t *pxIterator = ( ListItem_t * ) listGET_HEAD_ENTRY( &xBoundTcpSocketsList );

		xLastTime = xNow;

		if( xDelta == 0 )
		{
			xDelta = 1;
		}

		while( pxIterator != pxEnd )
		{
			pxSocket = ( xFreeRTOS_Socket_t * )listGET_LIST_ITEM_OWNER( pxIterator );
			pxIterator = ( ListItem_t * ) listGET_NEXT( pxIterator );

			/* Sockets with 'tmout == 0' do not need any regular attention. */
			if( pxSocket->u.xTcp.usTimeout == 0 )
			{
				continue;
			}

			if( xDelta < ( TickType_t ) pxSocket->u.xTcp.usTimeout )
			{
				pxSocket->u.xTcp.usTimeout = ( uint16_t ) ( ( ( TickType_t ) pxSocket->u.xTcp.usTimeout ) - xDelta );
			}
			else
			{
				pxSocket->u.xTcp.usTimeout = 0;

				/* Within this function, the socket might want to send a delayed
				ack or send out data or whatever it needs to do. */
				if( xTCPSocketCheck( pxSocket ) < 0 )
				{
					/* Continue because the socket was deleted. */
					continue;
				}
			}

			/* In xEventBits the driver may indicate that the socket has
			important events for the user.  These are only done just before the
			IP-task goes to sleep. */
			if( pxSocket->xEventBits != 0 )
			{
				if( xWillSleep != pdFALSE )
				{
					/* The IP-task is about to go to sleep, so messages can be
					sent to the socket owners. */
					vWakeUpSocketUser( pxSocket );
				}
				else
				{
					/* Or else make sure this will be called again to wake-up
					the sockets' owner. */
					xShortest = 0;
				}
			}

			if( uxGetRxEventCount() != 0 )
			{
				/* This was interrupted, but want to be called as soon as
				possible to finish checking the other sockets. */
				xShortest = 0;
				break;
			}

			if( ( pxSocket->u.xTcp.usTimeout != 0 ) && ( xShortest > ( TickType_t ) pxSocket->u.xTcp.usTimeout ) )
			{
				xShortest = ( TickType_t ) pxSocket->u.xTcp.usTimeout;
			}
		}

		return xShortest;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * TCP: as multiple sockets may be bound to the same local port number
	 * looking up a socket is a little more complex:
	 * Both a local port, and a remote port and IP address are being used
	 * For a socket in listening mode, the remote port and IP address are both 0
	 */
	xFreeRTOS_Socket_t *pxTCPSocketLookup( uint32_t ulLocalIP, BaseType_t xLocalPort, uint32_t ulRemoteIP, BaseType_t xRemotePort )
	{
	ListItem_t *pxIterator;
	xFreeRTOS_Socket_t *pxResult = NULL, *pxListenSocket = NULL;
	MiniListItem_t *pxEnd = ( MiniListItem_t* )listGET_END_MARKER( &xBoundTcpSocketsList );

		/* Parameter not yet supported. */
		( void ) ulLocalIP;

		for( pxIterator  = ( ListItem_t * ) listGET_NEXT( pxEnd );
			 pxIterator != ( ListItem_t * ) pxEnd;
			 pxIterator  = ( ListItem_t * ) listGET_NEXT( pxIterator ) )
		{
			xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) listGET_LIST_ITEM_OWNER( pxIterator );

			if( pxSocket->usLocPort == xLocalPort )
			{
				if( pxSocket->u.xTcp.ucTcpState == eTCP_LISTEN )
				{
					/* If this is a socket listening to xLocalPort, remember it
					in case there is no perfect match. */
					pxListenSocket = pxSocket;
				}
				else if( ( pxSocket->u.xTcp.usRemotePort == xRemotePort ) && ( pxSocket->u.xTcp.ulRemoteIP == ulRemoteIP ) )
				{
					/* For sockets not in listening mode, find a match with
					xLocalPort, ulRemoteIP AND xRemotePort. */
					pxResult = pxSocket;
					break;
				}
			}
		}
		if( pxResult == NULL )
		{
			/* An exact match was not found, maybe a listening socket was
			found. */
			pxResult = pxListenSocket;
		}

		return pxResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	const struct xSTREAM_BUFFER *FreeRTOS_get_rx_buf( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = (xFreeRTOS_Socket_t *)xSocket;

		return pxSocket->u.xTcp.rxStream;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	static xStreamBuffer *prvTcpCreateStream ( xFreeRTOS_Socket_t *pxSocket, BaseType_t bInput )
	{
	xStreamBuffer *pxBuffer;
	uint32_t ulLength;
	uint32_t ulSize;

		/* Now that a stream is created, the maximum size is fixed before
		creation, it could still be changed with setsockopt(). */
		if( bInput != 0 )
		{
			ulLength = ( uint32_t ) pxSocket->u.xTcp.rxStreamSize;

			if( pxSocket->u.xTcp.lLittleSpace == 0 )
			{
				pxSocket->u.xTcp.lLittleSpace  = ( 1 * pxSocket->u.xTcp.rxStreamSize ) / 5; /*_RB_ Why divide by 5?  Can this be changed to a #define? */
			}

			if( pxSocket->u.xTcp.lEnoughSpace == 0 )
			{
				pxSocket->u.xTcp.lEnoughSpace = ( 4 * pxSocket->u.xTcp.rxStreamSize ) / 5; /*_RB_ Why multiply by 4?  Maybe sock80_PERCENT?*/
			}
		}
		else
		{
			ulLength = ( uint32_t ) pxSocket->u.xTcp.txStreamSize;
		}

		ulSize = sizeof( *pxBuffer ) - sizeof( pxBuffer->ucArray ) + ulLength + 1;

		pxBuffer = ( xStreamBuffer * )pvPortMallocLarge( ulSize );

		if( pxBuffer == NULL )
		{
			FreeRTOS_debug_printf( ( "prvTcpCreateStream: malloc failed\n" ) );
			pxSocket->u.xTcp.bits.bMallocError = pdTRUE;
			vTCPStateChange( pxSocket, eCLOSE_WAIT );
		}
		else
		{
			/* Clear the markers of the stream */
			memset( pxBuffer, '\0', sizeof( *pxBuffer ) - sizeof( pxBuffer->ucArray ) );
			pxBuffer->LENGTH = ( int32_t ) ( ulLength + 1 );

			if( xTCPWindowLoggingLevel != 0 )
			{
				FreeRTOS_debug_printf( ( "prvTcpCreateStream: %cxStream created %lu bytes (total %lu)\n", bInput ? 'R' : 'T', ulLength, ulSize ) );
			}

			if( bInput != 0 )
			{
				pxSocket->u.xTcp.rxStream = pxBuffer;
			}
			else
			{
				pxSocket->u.xTcp.txStream = pxBuffer;
			}
		}

		return pxBuffer;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * Add data to the RxStream.  When ulOffset > 0, data has come in out-of-order
	 * and will be put in front of the head so it can not be popped by the user.
	 */
	/*_RB_ Should ulOffset be a uint32_t, or should it name start with 'x'? */
	int32_t lTCPAddRxdata( xFreeRTOS_Socket_t *pxSocket, BaseType_t ulOffset, const uint8_t *pcData, uint32_t ulByteCount )
	{
	xStreamBuffer *pxStream = pxSocket->u.xTcp.rxStream;
	int32_t xResult;
	#if( ipconfigUSE_CALLBACKS == 1 )
		BaseType_t bHasHandler = ipconfigIS_VALID_PROG_ADDRESS( pxSocket->u.xTcp.pHndReceive );
		const uint8_t *pucBuffer = NULL;
	#endif /* ipconfigUSE_CALLBACKS */

		/* int32_t lStreamBufferAdd( pxBuffer, lOffset, pucData, aCount )
		if( pucData != NULL ) copy data the the buffer
		if( pucData == NULL ) no copying, just advance rxHead
		if( lOffset != 0 ) Just store data which has come out-of-order
		if( lOffset == 0 ) Also advance rxHead */
		if( pxStream == NULL )
		{
			pxStream = prvTcpCreateStream( pxSocket, pdTRUE );
			if( pxStream == NULL )
			{
				return -1;
			}
		}

		#if( ipconfigUSE_CALLBACKS == 1 )
		{
			if( bHasHandler && lStreamBufferGetSize( pxStream ) == 0 && !ulOffset && pcData )
			{
				/* Data can be passed directly to the user */
				pucBuffer = pcData;

				/* Zero-copy for call-back: no need to add the bytes to the
				stream, only the pointer will be advanced by lStreamBufferAdd(). */
				pcData = NULL;
			}
		}
		#endif /* ipconfigUSE_CALLBACKS */

		xResult = lStreamBufferAdd( pxStream, ulOffset, pcData, ( int32_t ) ulByteCount );

		#if( ipconfigHAS_DEBUG_PRINTF != 0 )
		{
			if( xResult != ( int32_t ) ulByteCount )
			{
				FreeRTOS_debug_printf( ( "lTCPAddRxdata: at %ld: %ld/%lu bytes (tail %lu head %lu space %lu front %lu)\n",
					ulOffset, xResult, ulByteCount,
					pxStream->lTail,
					pxStream->lHead,
					lStreamBufferFrontSpace( pxStream ),
					pxStream->lFront ) );
			}
		}
		#endif /* ipconfigHAS_DEBUG_PRINTF */

		if( ulOffset == 0 )
		{
			/* Data is being added to rxStream at the head (offs = 0) */
			#if( ipconfigUSE_CALLBACKS == 1 )
				if( bHasHandler != pdFALSE )
				{
					/* The socket owner has installed an OnReceive handler. Pass the
					Rx data, without copying from the rxStream, to the user. */
					for (;;)
					{
						uint8_t *ucReadPtr = NULL;
						uint32_t ulCount;
						if( pucBuffer != NULL )
						{
							ucReadPtr = ( uint8_t * )pucBuffer;
							ulCount = ulByteCount;
							pucBuffer = NULL;
						}
						else
						{
							ulCount = ( uint32_t ) lStreamBufferGetPtr( pxStream, &( ucReadPtr ) );
						}

						if( ulCount == 0 )
						{
							break;
						}

						pxSocket->u.xTcp.pHndReceive( (xSocket_t *)pxSocket, ( void* )ucReadPtr, ( size_t ) ulCount );
						lStreamBufferGet( pxStream, 0, NULL, ( int32_t ) ulCount, pdFALSE );
					}
				} else
			#endif /* ipconfigUSE_CALLBACKS */
			{
				/* See if running out of space. */
				if( pxSocket->u.xTcp.bits.bLowWater == 0 )
				{
					int32_t lFrontSpace = lStreamBufferFrontSpace( pxSocket->u.xTcp.rxStream );
					if( lFrontSpace <= pxSocket->u.xTcp.lLittleSpace  )
					{
						pxSocket->u.xTcp.bits.bLowWater = pdTRUE;
						pxSocket->u.xTcp.bits.bWinChange = pdTRUE;

						/* bLowWater was reached, send the changed window size. */
						pxSocket->u.xTcp.usTimeout = 1;
						xSendEventToIPTask( eTCPTimerEvent );
					}
				}

				/* New incoming data is available, wake up the user.   User's
				semaphores will be set just before the IP-task goes asleep. */
				pxSocket->xEventBits |= eSOCKET_RECEIVE;

				#if ipconfigSUPPORT_SELECT_FUNCTION == 1
				{
					if( ( pxSocket->xSelectBits & eSELECT_READ ) != 0 )
					{
						pxSocket->xEventBits |= ( eSELECT_READ << SOCKET_EVENT_BIT_COUNT );
					}
				}
				#endif
			}
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* Returns the number of outstanding bytes in txStream */
	BaseType_t FreeRTOS_outstanding( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xResult;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xResult = -pdFREERTOS_ERRNO_EINVAL;
		}
		else if( pxSocket->u.xTcp.txStream == NULL )
		{
			xResult = 0;
		}
		else
		{
			xResult = lStreamBufferGetSize( pxSocket->u.xTcp.txStream );
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* Returns the number of bytes in rxStream */
	BaseType_t FreeRTOS_recvcount( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xResult;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xResult = -pdFREERTOS_ERRNO_EINVAL;
		}
		else if( pxSocket->u.xTcp.rxStream == NULL )
		{
			xResult = 0;
		}
		else
		{
			xResult = lStreamBufferGetSize( pxSocket->u.xTcp.rxStream );
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* Function to get the remote address and IP port */
	BaseType_t FreeRTOS_GetRemoteAddress( xSocket_t xSocket, struct freertos_sockaddr *pxAddress )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xResult;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xResult = -pdFREERTOS_ERRNO_EINVAL;
		}
		else
		{
			/* BSD style sockets communicate IP and port addresses in network
			byte order.

			IP address of remote machine. */
			pxAddress->sin_addr = FreeRTOS_htonl ( pxSocket->u.xTcp.ulRemoteIP );

			/* Port on remote machine. */
			pxAddress->sin_port = FreeRTOS_htons ( pxSocket->u.xTcp.usRemotePort );

			xResult = sizeof( ( *pxAddress ) );
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */

/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* Returns the number of bytes that may be added to txStream */
	BaseType_t FreeRTOS_maywrite( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xResult;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xResult = -pdFREERTOS_ERRNO_EINVAL;
		}
		else if( pxSocket->u.xTcp.ucTcpState != eESTABLISHED )
		{
			if( ( pxSocket->u.xTcp.ucTcpState < eCONNECT_SYN ) || ( pxSocket->u.xTcp.ucTcpState > eESTABLISHED ) )
			{
				xResult = -1;
			}
			else
			{
				xResult = 0;
			}
		}
		else if( pxSocket->u.xTcp.txStream == NULL )
		{
			xResult = pxSocket->u.xTcp.txStreamSize;
		}
		else
		{
			xResult = lStreamBufferGetSpace( pxSocket->u.xTcp.txStream );
		}

		return xResult;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP ==1 )

	BaseType_t FreeRTOS_tx_space( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xReturn;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xReturn = -pdFREERTOS_ERRNO_EINVAL;
		}
		else
		{
			if( pxSocket->u.xTcp.txStream != NULL )
			{
				xReturn = lStreamBufferGetSpace ( pxSocket->u.xTcp.txStream );
			}
			else
			{
				xReturn = pxSocket->u.xTcp.txStreamSize;
			}
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	BaseType_t FreeRTOS_tx_size( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xReturn;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xReturn = -pdFREERTOS_ERRNO_EINVAL;
		}
		else
		{
			if( pxSocket->u.xTcp.txStream != NULL )
			{
				xReturn = lStreamBufferGetSize ( pxSocket->u.xTcp.txStream );
			}
			else
			{
				xReturn = 0;
			}
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* Returns pdTRUE if TCP socket is connected. */
	BaseType_t FreeRTOS_issocketconnected( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xReturn = pdFALSE;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xReturn = -pdFREERTOS_ERRNO_EINVAL;
		}
		else
		{
			if( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED )
			{
				if( pxSocket->u.xTcp.ucTcpState < eCLOSE_WAIT )
				{
					xReturn = pdTRUE;
				}
			}
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* returns the actual size of MSS being used */
	BaseType_t FreeRTOS_mss( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xReturn;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xReturn = -pdFREERTOS_ERRNO_EINVAL;
		}
		else
		{
			/* usCurMSS is declared as uint16_t to save space.  FreeRTOS_mss()
			will often be used in signed native-size expressions cast it to
			BaseType_t. */
			xReturn = ( BaseType_t ) ( pxSocket->u.xTcp.usCurMSS );
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/* HT: for internal use only: return the connection status */
	BaseType_t FreeRTOS_connstatus( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xReturn;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xReturn = -pdFREERTOS_ERRNO_EINVAL;
		}
		else
		{
			/* Cast it to BaseType_t */
			xReturn = ( BaseType_t ) ( pxSocket->u.xTcp.ucTcpState );
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	/*
	 * Returns the number of bytes which can be read.
	 */
	BaseType_t FreeRTOS_rx_size( xSocket_t xSocket )
	{
	xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) xSocket;
	BaseType_t xReturn;

		if( pxSocket->ucProtocol != FREERTOS_IPPROTO_TCP )
		{
			xReturn = -pdFREERTOS_ERRNO_EINVAL;
		}
		else if( pxSocket->u.xTcp.rxStream != NULL )
		{
			xReturn = lStreamBufferGetSize ( pxSocket->u.xTcp.rxStream );
		}
		else
		{
			xReturn = 0;
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP == 1 )

	void FreeRTOS_netstat( void )
	{
	xIPStackEvent_t xAskEvent;

		/* Ask the IP-task to call vTCPNetStat()
		 * to avoid accessing xBoundTcpSocketsList
		 */
		xAskEvent.eEventType = eTCPNetStat;
		xAskEvent.pvData = ( void * ) NULL;
		xSendEventStructToIPTask( &xAskEvent, 1000 );
	}

#endif /* ipconfigUSE_TCP */
/*-----------------------------------------------------------*/

#if( ( ipconfigHAS_PRINTF != 0 ) && ( ipconfigUSE_TCP == 1 ) )

	void vTCPNetStat( void )
	{
	/* Show a simple listing of all created sockets and their connections */
	ListItem_t *pxIterator;
	BaseType_t count = 0;

		if( listLIST_IS_INITIALISED( &xBoundTcpSocketsList ) == pdFALSE )
		{
			FreeRTOS_printf( ( "PLUS-TCP not initialized\n" ) );
		}
		else
		{
			FreeRTOS_printf( ( "Prot Port IP-Remote       : Port  R/T Status       Alive  tmout Child\n" ) );
			for( pxIterator  = ( ListItem_t * ) listGET_HEAD_ENTRY( &xBoundTcpSocketsList );
				 pxIterator != ( ListItem_t * ) listGET_END_MARKER( &xBoundTcpSocketsList );
				 pxIterator  = ( ListItem_t * ) listGET_NEXT( pxIterator ) )
			{
				xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) listGET_LIST_ITEM_OWNER( pxIterator );
				#if ipconfigTCP_KEEP_ALIVE
					TickType_t age = xTaskGetTickCount() - pxSocket->u.xTcp.xLastAliveTime;
				#else
					TickType_t age = 0u;
				#endif
				#if( ipconfigUSE_CALLBACKS == 1 )
					void *pHndReceive = (void*)pxSocket->u.xTcp.pHndReceive;
				#else
					void *pHndReceive = (void*)NULL;
				#endif
				char ucChildText[16] = "";
				if (pxSocket->u.xTcp.ucTcpState == eTCP_LISTEN)
				{
					snprintf( ucChildText, sizeof( ucChildText ), " %d/%d",
						pxSocket->u.xTcp.usChildCount,
						pxSocket->u.xTcp.usBacklog);
				}
				if( age > 999999 )
					age = 999999;
				FreeRTOS_printf( ( "TCP %5d %-16lxip:%5d %d/%d %-13.13s %6lu %6u%s\n",
					pxSocket->usLocPort,		/* Local port on this machine */
					pxSocket->u.xTcp.ulRemoteIP,	/* IP address of remote machine */
					pxSocket->u.xTcp.usRemotePort,	/* Port on remote machine */
					pxSocket->u.xTcp.rxStream != NULL,
					pxSocket->u.xTcp.txStream != NULL,
					FreeRTOS_GetTCPStateName( pxSocket->u.xTcp.ucTcpState ),
					age,
					pxSocket->u.xTcp.usTimeout,
					ucChildText ) );
					/* Remove compiler warnings if FreeRTOS_debug_printf() is not defined. */
					( void ) pHndReceive;
				count++;
			}

			for( pxIterator  = ( ListItem_t * ) listGET_HEAD_ENTRY( &xBoundUdpSocketsList );
				 pxIterator != ( ListItem_t * ) listGET_END_MARKER( &xBoundUdpSocketsList );
				 pxIterator  = ( ListItem_t * ) listGET_NEXT( pxIterator ) )
			{
				/* Local port on this machine */
				FreeRTOS_printf( ( "UDP Port %5u\n",
					FreeRTOS_ntohs( listGET_LIST_ITEM_VALUE( pxIterator ) ) ) );
				count++;
			}

			FreeRTOS_printf( ( "FreeRTOS_netstat: %lu sockets %lu < %lu < %d buffers free\n",
				count,
				uxGetMinimumFreeNetworkBuffers( ),
				uxGetNumberOfFreeNetworkBuffers( ),
				ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ) );
		}
	}

#endif /* ( ( ipconfigHAS_PRINTF != 0 ) && ( ipconfigUSE_TCP == 1 ) ) */
/*-----------------------------------------------------------*/

#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )

	void vSocketSelect( xSocketSelect_t *pxSocketSet )
	{
	BaseType_t xRound;
	EventBits_t xSocketBits, xBitsToClear;
	#if ipconfigUSE_TCP == 1
		BaseType_t xLastRound = 1;
	#else
		BaseType_t xLastRound = 0;
	#endif

		/* These flags will be switched on after checking the socket status. */
		EventBits_t xGroupBits = 0;
		pxSocketSet->pxSocket = NULL;

		for( xRound = 0; xRound <= xLastRound; xRound++ )
		{
			const ListItem_t *pxIterator;
			const MiniListItem_t *pxEnd;
			if( xRound == 0 )
			{
				pxEnd = ( const MiniListItem_t* )listGET_END_MARKER( &xBoundUdpSocketsList );
			}
		#if ipconfigUSE_TCP == 1
			else
			{
				pxEnd = ( const MiniListItem_t* )listGET_END_MARKER( &xBoundTcpSocketsList );
			}
		#endif /* ipconfigUSE_TCP == 1 */
			for( pxIterator = ( const ListItem_t * ) ( listGET_NEXT( pxEnd ) );
				 pxIterator != ( const ListItem_t * ) pxEnd;
				 pxIterator = ( const ListItem_t * ) listGET_NEXT( pxIterator ) )
			{
				xFreeRTOS_Socket_t *pxSocket = ( xFreeRTOS_Socket_t * ) listGET_LIST_ITEM_OWNER( pxIterator );
				if( pxSocket->pxSocketSet != pxSocketSet )
				{
					/* Socket does not belong to this select group. */
					continue;
				}
				xSocketBits = 0;

				#if( ipconfigUSE_TCP == 1 )
					if( pxSocket->ucProtocol == FREERTOS_IPPROTO_TCP )
					{
						/* Check if the socket has already been accepted by the
						owner.  If not, it is useless to return it from a
						select(). */
						BaseType_t bAccepted = pdFALSE;

						if( pxSocket->u.xTcp.bits.bPassQueued == pdFALSE )
						{
							if( pxSocket->u.xTcp.bits.bPassAccept == pdFALSE )
							{
								bAccepted = pdTRUE;
							}
						}

						/* Is the set owner interested in READ events? */
						if( ( pxSocket->xSelectBits & eSELECT_READ ) != 0 )
						{
							if( pxSocket->u.xTcp.ucTcpState == eTCP_LISTEN )
							{
								if( ( pxSocket->u.xTcp.pxPeerSocket != NULL ) && ( pxSocket->u.xTcp.pxPeerSocket->u.xTcp.bits.bPassAccept != 0 ) )
								{
									xSocketBits |= eSELECT_READ;
								}
							}
							else if( ( bAccepted != 0 ) && ( FreeRTOS_recvcount( pxSocket ) > 0 ) )
							{
								xSocketBits |= eSELECT_READ;
							}
						}
						/* Is the set owner interested in EXCEPTION events? */
						if( ( pxSocket->xSelectBits & eSELECT_EXCEPT ) != 0 )
						{
							if( ( pxSocket->u.xTcp.ucTcpState == eCLOSE_WAIT ) || ( pxSocket->u.xTcp.ucTcpState == eCLOSED ) )
							{
								xSocketBits |= eSELECT_EXCEPT;
							}
						}

						/* Is the set owner interested in WRITE events? */
						if( ( pxSocket->xSelectBits & eSELECT_WRITE ) != 0 )
						{
							BaseType_t bMatch = pdFALSE;

							if( bAccepted != 0 )
							{
								if( FreeRTOS_tx_space( pxSocket ) > 0 )
								{
									bMatch = pdTRUE;
								}
							}

							if( bMatch == pdFALSE )
							{
								if( ( pxSocket->u.xTcp.bits.bConnPrepared != pdFALSE ) &&
									( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED ) &&
									( pxSocket->u.xTcp.bits.bConnPassed == pdFALSE ) )
								{
									pxSocket->u.xTcp.bits.bConnPassed = pdTRUE;
									bMatch = pdTRUE;
								}
							}

							if( bMatch != pdFALSE )
							{
								xSocketBits |= eSELECT_WRITE;
							}
						}
					}
					else
				#endif /* ipconfigUSE_TCP == 1 */
				{
					/* Select events for UDP are simpler. */
					if( ( ( pxSocket->xSelectBits & eSELECT_READ ) != 0 ) &&
						( listCURRENT_LIST_LENGTH( &( pxSocket->u.xUdp.xWaitingPacketsList ) ) > 0U ) )
					{
						xSocketBits |= eSELECT_READ;
					}
					/* The WRITE and EXCEPT bits are not used for UDP */
				}	/* if( pxSocket->ucProtocol == FREERTOS_IPPROTO_TCP ) */

				/* Each socket keeps its own event flags, which are looked-up
				by FreeRTOS_FD_ISSSET() */
				pxSocket->xSocketBits = xSocketBits;

				/* The ORed value will be used to set the bits in the event
				group. */
				xGroupBits |= xSocketBits;

			}	/* for( pxIterator ... ) */
		}	/* for( xRound = 0; xRound <= xLastRound; xRound++ ) */

		xBitsToClear = xEventGroupGetBits( pxSocketSet->xSelectGroup );

		/* Now set the necessary bits. */
		xBitsToClear = ( xBitsToClear & ~xGroupBits ) & eSELECT_ALL;
		if( xBitsToClear != 0 )
		{
			xEventGroupClearBits( pxSocketSet->xSelectGroup, xBitsToClear );
		}

		/* Now include eSELECT_CALL_IP to wakeup the caller. */
		xEventGroupSetBits( pxSocketSet->xSelectGroup, xGroupBits | eSELECT_CALL_IP );
	}

#endif /* ipconfigSUPPORT_SELECT_FUNCTION == 1 */

