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

/*
 * FreeRTOS_TCP_IP.c
 * Module which handles the TCP connections for FreeRTOS+TCP.
 * It depends on  FreeRTOS_TCP_WIN.c, which handles the TCP windowing
 * schemes.
 *
 * Endianness: in this module all ports and IP addresses are stored in
 * host byte-order, except fields in the IP-packets
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
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_TCP_IP.h"
#include "FreeRTOS_DHCP.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"
#include "FreeRTOS_ARP.h"
#include "FreeRTOS_TCP_WIN.h"


/* Just make sure the contents doesn't get compiled if TCP is not enabled. */
#if ipconfigUSE_TCP == 1

/*
 * The meaning of the TCP flags:
 */
#define ipTCP_FLAG_FIN			0x0001 /* No more data from sender */
#define ipTCP_FLAG_SYN			0x0002 /* Synchronize sequence numbers */
#define ipTCP_FLAG_RST			0x0004 /* Reset the connection */
#define ipTCP_FLAG_PSH			0x0008 /* Push function: please push buffered data to the recv application */
#define ipTCP_FLAG_ACK			0x0010 /* Acknowledgment field is significant */
#define ipTCP_FLAG_URG			0x0020 /* Urgent pointer field is significant */
#define ipTCP_FLAG_ECN			0x0040 /* ECN-Echo */
#define ipTCP_FLAG_CWR			0x0080 /* Congestion Window Reduced */
#define ipTCP_FLAG_NS			0x0100 /* ECN-nonce concealment protection */
#define ipTCP_FLAG_RSV			0x0E00 /* Reserved, keep 0 */

/* A mask to filter all protocol flags. */
#define ipTCP_FLAG_CTRL			0x001f

/*
 * A few values of the TCP options:
 */
#define TCP_OPT_END				0   /* End of TCP options list */
#define TCP_OPT_NOOP			1   /* "No-operation" TCP option */
#define TCP_OPT_MSS				2   /* Maximum segment size TCP option */
#define TCP_OPT_WSOPT			3   /* TCP Window Scale Option (3-byte long) */
#define TCP_OPT_SACK_P			4   /* Advertize that SACK is permitted */
#define TCP_OPT_SACK_A			5   /* SACK option with first/last */
#define TCP_OPT_TIMESTAMP		8   /* Time-stamp option */

#define TCP_OPT_MSS_LEN			4   /* Length of TCP MSS option. */
#define TCP_OPT_TIMESTAMP_LEN	10	/* fixed length of the time-stamp option */

/*
 * The macro NOW_CONNECTED() is use to determine if the connection makes a
 * transition from connected to non-connected and vice versa.
 * NOW_CONNECTED() returns true when the status has one of these values:
 * eESTABLISHED, eFIN_WAIT_1, eFIN_WAIT_2, eCLOSING, eLAST_ACK, eTIME_WAIT
 * Technically the connection status is closed earlier, but the library wants
 * to prevent that the socket will be deleted before the last ACK has been
 * and thus causing a 'RST' packet on either side.
 */
#define NOW_CONNECTED( status )\
	( ( status >= eESTABLISHED ) && ( status != eCLOSE_WAIT ) )

/*
 * The highest 4 bits in the TCP offset byte indicate the total length of the
 * TCP header, divided by 4.
 */
#define VALID_BITS_IN_TCP_OFFSET_BYTE        ( 0xF0 )

/*
 * Acknowledgements to TCP data packets may be delayed as long as more is being expected.
 * A normal delay would be 200ms.  Here a much shorter delay of 20 ms is being used to
 * gain performance.
 */
#define DELAYED_ACK_SHORT_DELAY_MS			( 2 )
#define DELAYED_ACK_LONGER_DELAY_MS			( 20 )

/*
 * The MSS (Maximum Segment Size) will be taken as large as possible. However, packets with
 * an MSS of 1460 bytes won't be transported through the internet.  The MSS will be reduced
 * to 1400 bytes.
 */
#define REDUCED_MSS_THROUGH_INTERNET		( 1400 )

/*
 * Each time a new TCP connection is being made, a new Initial Sequence Number shall be used.
 * The variable 'ulNextInitialSequenceNumber' will be incremented with a recommended value
 * of 0x102.
 */
#define INITIAL_SEQUENCE_NUMBER_INCREMENT		( 0x102UL )

/*
 * When there are no TCP options, the TCP offset equals 20 bytes, which is stored as
 * the number 5 (words) in the higher niblle of the TCP-offset byte.
 */
#define TCP_OFFSET_LENGTH_BITS			( 0xf0 )
#define TCP_OFFSET_STANDARD_LENGTH		( 0x50 )

/*
 * Each TCP socket is checked regularly to see if it can send data packets.
 * By default, the maximum number of packets sent during one check is limited to 8.
 * This amount may be further limited by setting the socket's TX window size.
 */
#if( !defined( SEND_REPEATED_COUNT ) )
	#define SEND_REPEATED_COUNT		( 8 )
#endif /* !defined( SEND_REPEATED_COUNT ) */

/*
 * The names of the different TCP states may be useful in logging.
 */
#if( ( ipconfigHAS_DEBUG_PRINTF != 0 ) || ( ipconfigHAS_PRINTF != 0 ) )
	static const char *pcStateNames[] = {
		"eCLOSED",
		"eTCP_LISTEN",
		"eCONNECT_SYN",
		"eSYN_FIRST",
		"eSYN_RECEIVED",
		"eESTABLISHED",
		"eFIN_WAIT_1",
		"eFIN_WAIT_2",
		"eCLOSE_WAIT",
		"eCLOSING",
		"eLAST_ACK",
		"eTIME_WAIT",
		"eUNKNOWN",
};
#endif /* ( ipconfigHAS_DEBUG_PRINTF != 0 ) || ( ipconfigHAS_PRINTF != 0 ) */

/*
 * Returns true if the socket must be checked.  Non-active sockets are waiting
 * for user action, either connect() or close().
 */
static BaseType_t prvTCPSocketIsActive( BaseType_t lStatus );

/*
 * Either sends a SYN or calls prvTCPSendRepeated (for regular messages).
 */
static int prvTCPSendPacket( xFreeRTOS_Socket_t *pxSocket );

/*
 * Try to send a series of messages.
 */
static BaseType_t prvTCPSendRepeated( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer );

/*
 * Return or send a packet to the other party.
 */
static void prvTCPReturnPacket( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer,
	uint32_t ulLen, BaseType_t xReleaseAfterSend );

/*
 * Initialise the data structures which keep track of the TCP windowing system.
 */
static void prvTCPCreateWindow( xFreeRTOS_Socket_t *pxSocket );

/*
 * Let ARP look-up the MAC-address of the peer and initialise the first SYN
 * packet.
 */
static BaseType_t prvTCPPrepareConnect( xFreeRTOS_Socket_t *pxSocket );

#if( ipconfigHAS_DEBUG_PRINTF != 0 )
	/*
	 * For logging and debugging: make a string showing the TCP flags.
	 */
	static const char *prvTCPFlagMeaning( UBaseType_t xFlags);
#endif /* ipconfigHAS_DEBUG_PRINTF != 0 */

/*
 * Parse the TCP option(s) received, if present.
 */
static void prvCheckOptions( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer );

/*
 * Set the initial properties in the options fields, like the preferred
 * value of MSS and whether SACK allowed.  Will be transmitted in the state
 * 'eCONNECT_SYN'.
 */
static BaseType_t prvSetSynAckOptions( xFreeRTOS_Socket_t *pxSocket, xTCPPacket_t * pxTCPPacket );

/*
 * For anti-hang protection and TCP keep-alive messages.  Called in two places:
 * after receiving a packet and after a state change.  The socket's alive timer
 * may be reset.
 */
static void prvTCPTouchSocket( xFreeRTOS_Socket_t *pxSocket );

/*
 * Prepare an outgoing message, if anything has to be sent.
 */
static int32_t prvTCPPrepareSend( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer, BaseType_t uxOptionsLength );

/*
 * Calculate when this socket needs to be checked to do (re-)transmissions.
 */
static TickType_t prvTCPNextTimeout( xFreeRTOS_Socket_t *pxSocket );

/*
 * The API FreeRTOS_send() adds data to the TX stream.  Add
 * this data to the windowing system to it can be transmitted.
 */
static void prvTCPAddTxData( xFreeRTOS_Socket_t *pxSocket );

/*
 *  Called to handle the closure of a TCP connection.
 */
static BaseType_t prvTCPHandleFin( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer );

#if(	ipconfigUSE_TCP_TIMESTAMPS == 1 )
	static BaseType_t prvTCPSetTimeStamp( BaseType_t lOffset, xFreeRTOS_Socket_t *pxSocket, xTCPHeader_t *pxTCPHeader );
#endif

/*
 * Called from prvTCPHandleState().  Find the TCP payload data and check and
 * return its length.
 */
static BaseType_t prvCheckRxData( xNetworkBufferDescriptor_t *pxNetworkBuffer, uint8_t **ppucRecvData );

/*
 * Called from prvTCPHandleState().  Check if the payload data may be accepted.
 * If so, it will be added to the socket's reception queue.
 */
static BaseType_t prvStoreRxData( xFreeRTOS_Socket_t *pxSocket, uint8_t *pucRecvData,
	xNetworkBufferDescriptor_t *pxNetworkBuffer, uint32_t ulReceiveLength );

/*
 * Set the TCP options (if any) for the outgoing packet.
 */
static BaseType_t prvSetOptions( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer );

/*
 * Called from prvTCPHandleState() as long as the TCP status is eSYN_RECEIVED to
 * eCONNECT_SYN.
 */
static BaseType_t prvHandleSynReceived( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer,
	uint32_t ulReceiveLength, BaseType_t xOptionsLength );

/*
 * Called from prvTCPHandleState() as long as the TCP status is eESTABLISHED.
 */
static BaseType_t prvHandleEstablished( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer,
	uint32_t ulReceiveLength, BaseType_t xOptionsLength );

/*
 * Called from prvTCPHandleState().  There is data to be sent.
 * If ipconfigUSE_TCP_WIN is defined, and if only an ACK must be sent, it will
 * be checked if it would better be postponed for efficiency.
 */
static BaseType_t prvSendData( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer,
	uint32_t ulReceiveLength, BaseType_t xSendLength );

/*
 * The heart of all: check incoming packet for valid data and acks and do what
 * is necessary in each state.
 */
static BaseType_t prvTCPHandleState( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer );

/*
 * Reply to a peer with the RST flag on, in case a packet can not be handled.
 */
static BaseType_t prvTCPSendReset( xNetworkBufferDescriptor_t *pxNetworkBuffer );

/*
 * Set the initial value for MSS (Maximum Segment Size) to be used.
 */
static void prvSocketSetMSS( xFreeRTOS_Socket_t *pxSocket );

/*
 * Return either a newly created socket, or the current socket in a connected
 * state (depends on the 'bReuseSocket' flag).
 */
static xFreeRTOS_Socket_t *prvHandleListen( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer );

/*
 * After a listening socket receives a new connection, it may duplicate itself.
 * The copying takes place in prvTCPSocketCopy.
 */
static BaseType_t prvTCPSocketCopy( xFreeRTOS_Socket_t *pxNewSocket, xFreeRTOS_Socket_t *pxSocket );

/*
 * prvTCPStatusAgeCheck() will see if the socket has been in a non-connected
 * state for too long.  If so, the socket will be closed, and -1 will be
 * returned.
 */
#if( ipconfigTCP_HANG_PROTECTION == 1 )
	static BaseType_t prvTCPStatusAgeCheck( xFreeRTOS_Socket_t *pxSocket );
#endif
/*-----------------------------------------------------------*/

/* Initial Sequence Number, i.e. the next initial sequence number that will be
used when a new connection is opened.  The value should be randomized to prevent
attacks from outside (spoofing). */
uint32_t ulNextInitialSequenceNumber = 0;

/*-----------------------------------------------------------*/

/* prvTCPSocketIsActive() returns true if the socket must be checked.
 * Non-active sockets are waiting for user action, either connect()
 * or close(). */
static BaseType_t prvTCPSocketIsActive( BaseType_t lStatus )
{
	switch( lStatus )
	{
	case eCLOSED:
	case eCLOSE_WAIT:
	case eFIN_WAIT_2:
	case eCLOSING:
	case eTIME_WAIT:
		return pdFALSE;
	default:
		return pdTRUE;
	}
}
/*-----------------------------------------------------------*/

#if( ipconfigTCP_HANG_PROTECTION == 1 )

	static BaseType_t prvTCPStatusAgeCheck( xFreeRTOS_Socket_t *pxSocket )
	{
	BaseType_t xResult;
		switch( pxSocket->u.xTcp.ucTcpState )
		{
		case eESTABLISHED:
			/* If the 'ipconfigTCP_KEEP_ALIVE' option is enabled, sockets in
			state ESTABLISHED can be protected using keep-alive messages. */
			xResult = pdFALSE;
			break;
		case eCLOSED:
		case eTCP_LISTEN:
		case eCLOSE_WAIT:
			/* These 3 states may last for ever, up to the owner. */
			xResult = pdFALSE;
			break;
		default:
			/* All other (non-connected) states will get anti-hanging
			protection. */
			xResult = pdTRUE;
			break;
		}
		if( xResult != pdFALSE )
		{
			/* How much time has past since the last active moment which is
			defined as A) a state change or B) a packet has arrived. */
			TickType_t xAge = xTaskGetTickCount( ) - pxSocket->u.xTcp.xLastActTime;

			/* ipconfigTCP_HANG_PROTECTION_TIME is in units of seconds. */
			if( xAge > ( ipconfigTCP_HANG_PROTECTION_TIME * configTICK_RATE_HZ ) )
			{
				#if( ipconfigHAS_DEBUG_PRINTF == 1 )
				{
					FreeRTOS_debug_printf( ( "Inactive socket closed: port %u rem %lxip:%u status %s\n",
						pxSocket->usLocPort,
						pxSocket->u.xTcp.ulRemoteIP,
						pxSocket->u.xTcp.usRemotePort,
						FreeRTOS_GetTCPStateName( ( UBaseType_t ) pxSocket->u.xTcp.ucTcpState ) ) );
				}
				#endif /* ipconfigHAS_DEBUG_PRINTF */

				/* Move to eCLOSE_WAIT, user may close the socket. */
				vTCPStateChange( pxSocket, eCLOSE_WAIT );

				/* When 'bPassQueued' true, this socket is an orphan until it
				gets connected. */
				if( pxSocket->u.xTcp.bits.bPassQueued != pdFALSE )
				{
					if( pxSocket->u.xTcp.bits.bReuseSocket == pdFALSE )
					{
						/* As it did not get connected, and the user can never
						accept() it anymore, it will be deleted now.  Called from
						the IP-task, so it's safe to call the internal Close
						function: vSocketClose(). */
						vSocketClose( pxSocket );
					}
					/* Return a negative value to tell to inform the caller
					xTCPTimerCheck()
					that the socket got closed and may not be accessed anymore. */
					xResult = -1;
				}
			}
		}
		return xResult;
	}
	/*-----------------------------------------------------------*/

#endif

/*
 * As soon as a TCP socket timer expires, this function xTCPSocketCheck
 * will be called (from xTCPTimerCheck)
 * It can send a delayed ACK or new data
 * Sequence of calling (normally) :
 * IP-Task:
 *		xTCPTimerCheck()				// Check all sockets ( declared in FreeRTOS_Sockets.c )
 *		xTCPSocketCheck()				// Either send a delayed ACK or call prvTCPSendPacket()
 *		prvTCPSendPacket()				// Either send a SYN or call prvTCPSendRepeated ( regular messages )
 *		prvTCPSendRepeated()			// Send at most 8 messages on a row
 *			prvTCPReturnPacket()		// Prepare for returning
 *			xNetworkInterfaceOutput()	// Sends data to the NIC ( declared in portable/NetworkInterface/xxx )
 */
BaseType_t xTCPSocketCheck( xFreeRTOS_Socket_t *pxSocket )
{
BaseType_t xResult = 0;
BaseType_t xReady = pdFALSE;

	if( ( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED ) && ( pxSocket->u.xTcp.txStream != NULL ) )
	{
		/* The API FreeRTOS_send() might have added data to the TX stream.  Add
		this data to the windowing system to it can be transmitted. */
		prvTCPAddTxData( pxSocket );
	}

	#if ipconfigUSE_TCP_WIN == 1
	{
		if( pxSocket->u.xTcp.pxAckMessage != NULL )
		{
			/* The first task of this regular socket check is to send-out delayed
			ACK's. */
			if( pxSocket->u.xTcp.bits.bUserShutdown == 0 )
			{
				/* Earlier data was received but not yet acknowledged.  This
				function is called when the TCP timer for the socket expires, the
				ACK may be sent now. */
				if( pxSocket->u.xTcp.ucTcpState != eCLOSED )
				{
					if( xTCPWindowLoggingLevel > 1 && ipconfigTCP_MAY_LOG_PORT( pxSocket->usLocPort ) )
					{
						FreeRTOS_debug_printf( ( "Send[%u->%u] del ACK %lu SEQ %lu (len %u)\n",
							pxSocket->usLocPort,
							pxSocket->u.xTcp.usRemotePort,
							pxSocket->u.xTcp.xTcpWindow.rx.ulCurrentSequenceNumber - pxSocket->u.xTcp.xTcpWindow.rx.ulFirstSequenceNumber,
							pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber   - pxSocket->u.xTcp.xTcpWindow.tx.ulFirstSequenceNumber,
							ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER ) );
					}

					prvTCPReturnPacket( pxSocket, pxSocket->u.xTcp.pxAckMessage, ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER, ipconfigZERO_COPY_TX_DRIVER );

					#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
					{
						/* The ownership has been passed to the SEND routine,
						clear the pointer to it. */
						pxSocket->u.xTcp.pxAckMessage = NULL;
					}
					#endif /* ipconfigZERO_COPY_TX_DRIVER */
				}
				if( prvTCPNextTimeout( pxSocket ) > 1 )
				{
					/* Tell the code below that this function is ready. */
					xReady = pdTRUE;
				}
			}
			else
			{
				/* The user wants to perform an active shutdown(), skip sending
				the	delayed	ACK.  The function prvTCPSendPacket() will send the
				FIN	along with the ACK's. */
			}

			if( pxSocket->u.xTcp.pxAckMessage != NULL )
			{
				vReleaseNetworkBufferAndDescriptor( pxSocket->u.xTcp.pxAckMessage );
				pxSocket->u.xTcp.pxAckMessage = NULL;
			}
		}
	}
	#endif /* ipconfigUSE_TCP_WIN */

	if( xReady == pdFALSE )
	{
		/* The second task of this regular socket check is sending out data. */
		if( ( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED ) ||
			( pxSocket->u.xTcp.ucTcpState == eCONNECT_SYN ) )
		{
			prvTCPSendPacket( pxSocket );
		}

		/* Set the time-out for the next wakeup for this socket. */
		prvTCPNextTimeout( pxSocket );

		#if( ipconfigTCP_HANG_PROTECTION == 1 )
		{
			/* In all (non-connected) states in which keep-alive messages can not be sent
			the anti-hang protocol will close sockets that are 'hanging'. */
			xResult = prvTCPStatusAgeCheck( pxSocket );
		}
		#endif
	}

	return xResult;
}
/*-----------------------------------------------------------*/

/*
 * prvTCPSendPacket() will be called when the socket time-out has been reached.
 * It is only called by xTCPSocketCheck().
 */
static int prvTCPSendPacket( xFreeRTOS_Socket_t *pxSocket )
{
BaseType_t lResult = 0;
BaseType_t xOptionsLength;
xTCPPacket_t *pxTCPPacket;
xNetworkBufferDescriptor_t *pxNetworkBuffer;

	if( pxSocket->u.xTcp.ucTcpState != eCONNECT_SYN )
	{
		/* The connection is in s state other than SYN. */
		pxNetworkBuffer = NULL;

		/* prvTCPSendRepeated() will only create a network buffer if necessary,
		i.e. when data must be sent to the peer. */
		lResult = prvTCPSendRepeated( pxSocket, &pxNetworkBuffer );

		if( pxNetworkBuffer != NULL )
		{
			vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
		}
	}
	else
	{
		if( pxSocket->u.xTcp.ucRepCount >= 3 )
		{
			/* The connection is in the SYN status. The packet will be repeated
			to most 3 times.  When there is no response, the socket get the
			status 'eCLOSE_WAIT'. */
			FreeRTOS_debug_printf( ( "Connect: giving up %lxip:%u\n",
				pxSocket->u.xTcp.ulRemoteIP,		/* IP address of remote machine. */
				pxSocket->u.xTcp.usRemotePort ) );	/* Port on remote machine. */
			vTCPStateChange( pxSocket, eCLOSE_WAIT );
		}
		else if( ( pxSocket->u.xTcp.bits.bConnPrepared ) || ( prvTCPPrepareConnect( pxSocket ) == pdTRUE ) )
		{
			/* Or else, if the connection has been prepared, or can be prepared
			now, proceed to send the packet with the SYN flag.
			prvTCPPrepareConnect() prepares 'lastPacket' and returns pdTRUE if
			the Ethernet address of the peer or the gateway is found. */
			pxTCPPacket = ( xTCPPacket_t * )pxSocket->u.xTcp.lastPacket;

			#if( ipconfigUSE_TCP_TIMESTAMPS == 1 )
			{
				/* When TCP time stamps are enabled, but they will only be applied
				if the peer is outside the netmask, usually on the internet.
				Packages sent on a LAN are usually too big to carry time stamps. */
				if( ( ( pxSocket->u.xTcp.ulRemoteIP ^ FreeRTOS_ntohl( *ipLOCAL_IP_ADDRESS_POINTER ) ) & xNetworkAddressing.ulNetMask ) != 0ul )
				{
					pxSocket->u.xTcp.xTcpWindow.u.bits.bTimeStamps = 1;
				}
			}
			#endif

			/* About to send a SYN packet.  Call prvSetSynAckOptions() to set
			the proper options: The size of MSS and whether SACK's are
			allowed. */
			xOptionsLength = prvSetSynAckOptions( pxSocket, pxTCPPacket );

			/* Return the number of bytes to be sent. */
			lResult = ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + xOptionsLength;

			/* Set the TCP offset field:  ipSIZE_OF_TCP_HEADER equals 20 and
			xOptionsLength is always a multiple of 4.  The complete expression
			would be:
			ucTcpOffset = ( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) / 4 ) << 4 */
			pxTCPPacket->xTCPHeader.ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );

			/* Repeat Count is used for a connecting socket, to limit the number
			of tries. */
			pxSocket->u.xTcp.ucRepCount++;

			/* Send the SYN message to make a connection.  The messages is
			stored in the socket field 'lastPacket'.  It will be wrapped in a
			pseudo network buffer descriptor before it will be sent. */
			prvTCPReturnPacket( pxSocket, NULL, ( uint32_t ) lResult, pdFALSE );
		}
	}

	/* Return the total number of bytes sent. */
	return lResult;
}
/*-----------------------------------------------------------*/

/*
 * prvTCPSendRepeated will try to send a series of messages, as long as there is
 * data to be sent and as long as the transmit window isn't full.
 */
static BaseType_t prvTCPSendRepeated( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer )
{
BaseType_t lIndex;
BaseType_t lResult = 0;
BaseType_t xOptionsLength = 0;
BaseType_t xSendLength;

	/* While sending data, uxGetRxEventCount() will be called to see if the NIC
	has received any new message.  If so, sending will stop immediately to give
	priority to receiving new packets. */
	for( lIndex = 0; ( lIndex < SEND_REPEATED_COUNT ) && ( uxGetRxEventCount() == 0 ); lIndex++ )
	{
		/* prvTCPPrepareSend() might allocate a network buffer if there is data
		to be sent. */
		xSendLength = prvTCPPrepareSend( pxSocket, ppxNetworkBuffer, xOptionsLength );
		if( xSendLength <= 0 )
		{
			break;
		}

		/* And return the packet to the peer. */
		prvTCPReturnPacket (pxSocket, *ppxNetworkBuffer, ( uint32_t ) xSendLength, ipconfigZERO_COPY_TX_DRIVER );

		#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
		{
			*ppxNetworkBuffer = NULL;
		}
		#endif /* ipconfigZERO_COPY_TX_DRIVER */

		lResult += xSendLength;
	}

	/* Return the total number of bytes sent. */
	return lResult;
}
/*-----------------------------------------------------------*/

/*
 * Return (or send) a packet the the peer.  The data is stored in pxBuffer,
 * which may either point to a real network buffer or to a TCP socket field
 * called 'xTcp.lastPacket'.   A temporary xNetworkBuffer will be used to pass
 * the data to the NIC.
 */
static void prvTCPReturnPacket( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer, uint32_t ulLen, BaseType_t xReleaseAfterSend )
{
xTCPPacket_t * pxTCPPacket;
xIPHeader_t *pxIPHeader;
xEthernetHeader_t *pxEthernetHeader;
uint32_t ulFrontSpace, ulSpace;
TCPWindow_t *pxTcpWindow;
xNetworkBufferDescriptor_t xTempBuffer;
/* For sending, a pseudo network buffer will be used, as explained above. */

	if( pxNetworkBuffer == NULL )
	{
		pxNetworkBuffer = &xTempBuffer;

		#if( ipconfigUSE_LINKED_RX_MESSAGES != 0 )
		{
			xTempBuffer.pxNextBuffer = NULL;
		}
		#endif
		xTempBuffer.pucEthernetBuffer = pxSocket->u.xTcp.lastPacket;
		xTempBuffer.xDataLength = sizeof pxSocket->u.xTcp.lastPacket;
		xReleaseAfterSend = pdFALSE;
	}

	#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
	{
		if( xReleaseAfterSend == pdFALSE )
		{
			pxNetworkBuffer = pxDuplicateNetworkBufferWithDescriptor( pxNetworkBuffer, pxNetworkBuffer->xDataLength );
			if( pxNetworkBuffer == NULL )
			{
				FreeRTOS_debug_printf( ( "prvTCPReturnPacket: duplicate failed\n" ) );
			}
			xReleaseAfterSend = pdTRUE;
		}
	}
	#endif /* ipconfigZERO_COPY_TX_DRIVER */

	if( pxNetworkBuffer != NULL )
	{
		pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
		pxIPHeader = &pxTCPPacket->xIPHeader;
		pxEthernetHeader = &pxTCPPacket->xEthernetHeader;

		/* Fill the packet, using hton translations. */
		if( pxSocket != NULL )
		{
			/* Calculate the space in the RX buffer in order to advertise the
			size of this socket's reception window. */
			pxTcpWindow = &( pxSocket->u.xTcp.xTcpWindow );

			if( pxSocket->u.xTcp.rxStream != NULL )
			{
				/* An RX stream was created already, see how much space is
				available. */
				ulFrontSpace = ( uint32_t ) lStreamBufferFrontSpace( pxSocket->u.xTcp.rxStream );
			}
			else
			{
				/* No RX stream has been created, the full stream size is
				available. */
				ulFrontSpace = ( uint32_t ) pxSocket->u.xTcp.rxStreamSize;
			}

			/* Take the minimum of the RX buffer space and the RX window size. */
			ulSpace = FreeRTOS_min_uint32( pxSocket->u.xTcp.ulRxCurWinSize, pxTcpWindow->xSize.ulRxWindowLength );

			if( ( pxSocket->u.xTcp.bits.bLowWater != 0 ) || ( pxSocket->u.xTcp.bits.bRxStopped != 0 ) )
			{
				/* The low-water mark was reached, meaning there was little
				space left.  The socket will wait until the application has read
				or flushed the incoming data, and 'zero-window' will be
				advertised. */
				ulSpace = 0;
			}

			/* If possible, advertise an RX window size of at least 1 MSS, otherwise
			the peer might start 'zero window probing', i.e. sending small packets
			(1, 2, 4, 8... bytes). */
			if( ( ulSpace < pxSocket->u.xTcp.usCurMSS ) && ( ulFrontSpace >= pxSocket->u.xTcp.usCurMSS ) )
			{
				ulSpace = pxSocket->u.xTcp.usCurMSS;
			}

			/* Avoid overflow of the 16-bit win field. */
			if( ulSpace > 0xfffcUL )
			{
				ulSpace = 0xfffcUL;
			}

			pxTCPPacket->xTCPHeader.usWindow = FreeRTOS_htons( ( uint16_t ) ulSpace );

			#if( ipconfigHAS_DEBUG_PRINTF != 0 )
			{
				if( ipconfigTCP_MAY_LOG_PORT( pxSocket->usLocPort ) != pdFALSE )
				{
					if( ( xTCPWindowLoggingLevel != 0 ) && ( pxSocket->u.xTcp.bits.bWinChange != pdFALSE ) )
					{
						int32_t lFrontSpace = pxSocket->u.xTcp.rxStream ? lStreamBufferFrontSpace( pxSocket->u.xTcp.rxStream ) : 0;
						FreeRTOS_debug_printf( ( "%s: %lxip:%u: [%lu < %lu] winSize %ld\n",
						pxSocket->u.xTcp.bits.bLowWater ? "STOP" : "GO ",
							pxSocket->u.xTcp.ulRemoteIP,
							pxSocket->u.xTcp.usRemotePort,
							pxSocket->u.xTcp.bits.bLowWater ? pxSocket->u.xTcp.lLittleSpace  : lFrontSpace, pxSocket->u.xTcp.lEnoughSpace,
							(int32_t) ( pxTcpWindow->rx.ulHighestSequenceNumber - pxTcpWindow->rx.ulCurrentSequenceNumber ) ) );
					}
				}
			}
			#endif /* ipconfigHAS_DEBUG_PRINTF != 0 */

			/* The new window size has been advertised, switch off the flag. */
			pxSocket->u.xTcp.bits.bWinChange = pdFALSE;

			/* Later on, when deciding to delay an ACK, a precise estimate is needed
			of the free RX space.  At this moment, 'ulHighestRxAllowed' would be the
			highest sequence number minus 1 that the socket will accept. */
			pxSocket->u.xTcp.ulHighestRxAllowed = pxTcpWindow->rx.ulCurrentSequenceNumber + ulSpace;

			#if ipconfigTCP_KEEP_ALIVE == 1
				if( pxSocket->u.xTcp.bits.bSendKeepAlive != 0 )
				{
					/* Sending a keep-alive packet, send the current sequence number
					minus 1, which will	be recognised as a keep-alive packet an
					responded to by acknowledging the last byte. */
					pxSocket->u.xTcp.bits.bSendKeepAlive = pdFALSE;
					pxSocket->u.xTcp.bits.bWaitKeepAlive = pdTRUE;

					pxTCPPacket->xTCPHeader.ulSequenceNumber = pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber - 1;
					pxTCPPacket->xTCPHeader.ulSequenceNumber = FreeRTOS_htonl( pxTCPPacket->xTCPHeader.ulSequenceNumber );
				}
				else
			#endif
			{
				pxTCPPacket->xTCPHeader.ulSequenceNumber = FreeRTOS_htonl( pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber );

				if( ( pxTCPPacket->xTCPHeader.ucTcpFlags & ipTCP_FLAG_FIN ) != 0 )
				{
					/* Suppress FIN in case this packet carries earlier data to be
					retransmitted. */
					uint32_t ulDataLen = ( uint32_t ) ( ulLen - ( ipSIZE_OF_TCP_HEADER + ipSIZE_OF_IP_HEADER ) );
					if( ( pxTcpWindow->ulOurSequenceNumber + ulDataLen ) != pxTcpWindow->tx.ulFINSequenceNumber )
					{
						pxTCPPacket->xTCPHeader.ucTcpFlags &= ( ( uint8_t ) ~ipTCP_FLAG_FIN );
						FreeRTOS_debug_printf( ( "Suppress FIN for %lu + %lu < %lu\n",
							pxTcpWindow->ulOurSequenceNumber - pxTcpWindow->tx.ulFirstSequenceNumber,
							ulDataLen,
							pxTcpWindow->tx.ulFINSequenceNumber - pxTcpWindow->tx.ulFirstSequenceNumber ) );
					}
				}
			}

			/* Tell which sequence number is expected next time */
			pxTCPPacket->xTCPHeader.ulAckNr = FreeRTOS_htonl( pxTcpWindow->rx.ulCurrentSequenceNumber );
		}
		else
		{
			/* Sending data without a socket, probably replying with a RST flag
			Just swap the two sequence numbers. */
			vFlip_32( pxTCPPacket->xTCPHeader.ulSequenceNumber, pxTCPPacket->xTCPHeader.ulAckNr );
		}

		pxIPHeader->ucTimeToLive           = ipconfigTCP_TIME_TO_LIVE;
		pxIPHeader->usLength               = FreeRTOS_htons( ulLen );
		pxIPHeader->ulDestinationIPAddress = pxIPHeader->ulSourceIPAddress;
		pxIPHeader->ulSourceIPAddress      = *ipLOCAL_IP_ADDRESS_POINTER;
		vFlip_16( pxTCPPacket->xTCPHeader.usSourcePort, pxTCPPacket->xTCPHeader.usDestinationPort );

		/* Just an increasing number. */
		pxIPHeader->usIdentification = FreeRTOS_htons( usPacketIdentifier );
		usPacketIdentifier++;
		pxIPHeader->usFragmentOffset = 0;

		#if( ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM == 0 )
		{
			/* calculate the IP header checksum, in case the driver won't do that. */
			pxIPHeader->usHeaderChecksum = 0x00;
			pxIPHeader->usHeaderChecksum = usGenerateChecksum( 0UL, ( uint8_t * ) &( pxIPHeader->ucVersionHeaderLength ), ipSIZE_OF_IP_HEADER );
			pxIPHeader->usHeaderChecksum = ~FreeRTOS_htons( pxIPHeader->usHeaderChecksum );

			/* calculate the TCP checksum for an outgoing packet. */
			usGenerateProtocolChecksum( (uint8_t*)pxTCPPacket, pdTRUE );

			/* A calculated checksum of 0 must be inverted as 0 means the checksum
			is disabled. */
			if( pxTCPPacket->xTCPHeader.usChecksum == 0x00 )
			{
				pxTCPPacket->xTCPHeader.usChecksum = 0xffffU;
			}
		}
		#endif

	#if( ipconfigUSE_LINKED_RX_MESSAGES != 0 )
		pxNetworkBuffer->pxNextBuffer = NULL;
	#endif

		/* Important: tell NIC driver how many bytes must be sent. */
		pxNetworkBuffer->xDataLength = ulLen + ipSIZE_OF_ETH_HEADER;

		/* Fill in the destination MAC addresses. */
		memcpy( ( void * ) &( pxEthernetHeader->xDestinationAddress ), ( void * ) &( pxEthernetHeader->xSourceAddress ),
			sizeof( pxEthernetHeader->xDestinationAddress ) );

		/* The source MAC addresses is fixed to 'ipLOCAL_MAC_ADDRESS'. */
		memcpy( ( void * ) &( pxEthernetHeader->xSourceAddress) , ( void * ) ipLOCAL_MAC_ADDRESS, ( size_t ) ipMAC_ADDRESS_LENGTH_BYTES );

		/* Send! */
		xNetworkInterfaceOutput( pxNetworkBuffer, xReleaseAfterSend );

		if( xReleaseAfterSend == pdFALSE )
		{
			/* Swap-back some fields, as pxBuffer probably points to a socket field
			containing the packet header. */
			vFlip_16( pxTCPPacket->xTCPHeader.usSourcePort, pxTCPPacket->xTCPHeader.usDestinationPort);
			pxTCPPacket->xIPHeader.ulSourceIPAddress = pxTCPPacket->xIPHeader.ulDestinationIPAddress;
			memcpy( pxEthernetHeader->xSourceAddress.ucBytes, pxEthernetHeader->xDestinationAddress.ucBytes, 6);
		}
		else
		{
			/* Nothing to do: the buffer has been passed to DMA and will be released after use */
		}
	}
}
/*-----------------------------------------------------------*/

/*
 * The SYN event is very important: the sequence numbers, which have a kind of
 * random starting value, are being synchronised.  The sliding window manager
 * (in FreeRTOS_TCP_WIN.c) needs to know them, along with the Maximum Segment
 * Size (MSS) in use.
 */
static void prvTCPCreateWindow( xFreeRTOS_Socket_t *pxSocket )
{
	if( xTCPWindowLoggingLevel )
		FreeRTOS_debug_printf( ( "Limits (using): TCP Win size %lu Water %lu <= %lu <= %lu\n",
			pxSocket->u.xTcp.ulRxWinSize * ipconfigTCP_MSS,
			pxSocket->u.xTcp.lLittleSpace ,
			pxSocket->u.xTcp.lEnoughSpace,
			pxSocket->u.xTcp.rxStreamSize ) );
	vTCPWindowCreate(
		&pxSocket->u.xTcp.xTcpWindow,
		ipconfigTCP_MSS * pxSocket->u.xTcp.ulRxWinSize,
		ipconfigTCP_MSS * pxSocket->u.xTcp.ulTxWinSize,
		pxSocket->u.xTcp.xTcpWindow.rx.ulCurrentSequenceNumber,
		pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber,
		pxSocket->u.xTcp.usInitMSS);
}
/*-----------------------------------------------------------*/

/*
 * Connecting sockets have a special state: eCONNECT_SYN.  In this phase,
 * the Ethernet address of the target will be found using ARP.  In case the
 * target IP address is not within the netmask, the hardware address of the
 * gateway will be used.
 */
static BaseType_t prvTCPPrepareConnect( xFreeRTOS_Socket_t *pxSocket )
{
xTCPPacket_t *pxTCPPacket;
xIPHeader_t *pxIPHeader;
eARPLookupResult_t eReturned;
uint32_t ulRemoteIP;
xMACAddress_t xEthAddress;
BaseType_t xReturn = pdTRUE;

	#if( ipconfigHAS_PRINTF != 0 )
	{
		/* Only necessary for nicer logging. */
		memset( xEthAddress.ucBytes, '\0', sizeof( xEthAddress.ucBytes ) );
	}
	#endif /* ipconfigHAS_PRINTF != 0 */

	ulRemoteIP = FreeRTOS_htonl( pxSocket->u.xTcp.ulRemoteIP );

	/* Determine the ARP cache status for the requested IP address. */
	eReturned = eARPGetCacheEntry( &( ulRemoteIP ), &( xEthAddress ) );

	switch( eReturned )
	{
	case eARPCacheHit:		/* An ARP table lookup found a valid entry. */
		break;				/* We can now prepare the SYN packet. */
	case eARPCacheMiss:		/* An ARP table lookup did not find a valid entry. */
	case eCantSendPacket:	/* There is no IP address, or an ARP is still in progress. */
	default:
		/* Count the number of times it couldn't find the ARP address. */
		pxSocket->u.xTcp.ucRepCount++;

		FreeRTOS_debug_printf( ( "ARP for %lxip (using %lxip): rc=%d %02X:%02X:%02X %02X:%02X:%02X\n",
			pxSocket->u.xTcp.ulRemoteIP,
			FreeRTOS_htonl( ulRemoteIP ),
			eReturned,
			xEthAddress.ucBytes[ 0 ],
			xEthAddress.ucBytes[ 1 ],
			xEthAddress.ucBytes[ 2 ],
			xEthAddress.ucBytes[ 3 ],
			xEthAddress.ucBytes[ 4 ],
			xEthAddress.ucBytes[ 5 ] ) );

		/* And issue a (new) ARP request */
		FreeRTOS_OutputARPRequest( ulRemoteIP );

		xReturn = pdFALSE;
	}

	if( xReturn != pdFALSE )
	{
		/* The MAC-address of the peer (or gateway) has been found,
		now prepare the initial TCP packet and some fields in the socket. */
		pxTCPPacket = ( xTCPPacket_t * )pxSocket->u.xTcp.lastPacket;
		pxIPHeader = &pxTCPPacket->xIPHeader;

		/* reset the retry counter to zero. */
		pxSocket->u.xTcp.ucRepCount = 0;

		/* And remember that the connect/SYN data are prepared. */
		pxSocket->u.xTcp.bits.bConnPrepared = pdTRUE;

		/* Now that the Ethernet address is known, the initial packet can be
		prepared. */
		memset( pxSocket->u.xTcp.lastPacket, '\0', sizeof( pxSocket->u.xTcp.lastPacket ) );

		/* Write the Ethernet address in Source, because it will be swapped by
		prvTCPReturnPacket(). */
		memcpy( &pxTCPPacket->xEthernetHeader.xSourceAddress, &xEthAddress, sizeof( xEthAddress ) );

		/* ipIP_TYPE is already in network-byte-order. */
		pxTCPPacket->xEthernetHeader.usFrameType = ipIP_TYPE;

		pxIPHeader->ucVersionHeaderLength = 0x45;
		pxIPHeader->usLength = FreeRTOS_htons( sizeof( xTCPPacket_t ) - sizeof( pxTCPPacket->xEthernetHeader ) );
		pxIPHeader->ucTimeToLive = ipconfigTCP_TIME_TO_LIVE;

		pxIPHeader->ucProtocol = ipPROTOCOL_TCP;

		/* Addresses and ports will be stored swapped because prvTCPReturnPacket
		will swap them back while replying. */
		pxIPHeader->ulDestinationIPAddress = *ipLOCAL_IP_ADDRESS_POINTER;
		pxIPHeader->ulSourceIPAddress = FreeRTOS_htonl( pxSocket->u.xTcp.ulRemoteIP );

		pxTCPPacket->xTCPHeader.usSourcePort = FreeRTOS_htons( pxSocket->u.xTcp.usRemotePort );
		pxTCPPacket->xTCPHeader.usDestinationPort = FreeRTOS_htons( pxSocket->usLocPort );

		/* We are actively connecting, so the peer's Initial Sequence Number (ISN)
		isn't known yet. */
		pxSocket->u.xTcp.xTcpWindow.rx.ulCurrentSequenceNumber = 0;

		/* Start with ISN (Initial Sequence Number). */
		pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber = ulNextInitialSequenceNumber;

		/* And increment it with 268 for the next new connection, which is
		recommended value. */
		ulNextInitialSequenceNumber += 0x102UL;

		/* The TCP header size is 20 bytes, divided by 4 equals 5, which is put in
		the high nibble of the TCP offset field. */
		pxTCPPacket->xTCPHeader.ucTcpOffset = 0x50;

		/* Only set the SYN flag. */
		pxTCPPacket->xTCPHeader.ucTcpFlags = ipTCP_FLAG_SYN;

		/* Set the values of usInitMSS / usCurMSS for this socket. */
		prvSocketSetMSS( pxSocket );

		/* For now this is also the advertised window size. */
		pxSocket->u.xTcp.ulRxCurWinSize = pxSocket->u.xTcp.usInitMSS;

		/* The initial sequence numbers at our side are known.  Later
		vTCPWindowInit() will be called to fill in the peer's sequence numbers, but
		first wait for a SYN+ACK reply. */
		prvTCPCreateWindow( pxSocket );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

/* For logging and debugging: make a string showing the TCP flags
*/
#if( ipconfigHAS_DEBUG_PRINTF != 0 )

	static const char *prvTCPFlagMeaning( UBaseType_t xFlags)
	{
		static char retString[10];
		snprintf(retString, sizeof( retString ), "%c%c%c%c%c%c%c%c%c",
			( xFlags & ipTCP_FLAG_FIN )  ? 'F' : '.',	/* 0x0001: No more data from sender */
			( xFlags & ipTCP_FLAG_SYN )  ? 'S' : '.',	/* 0x0002: Synchronize sequence numbers */
			( xFlags & ipTCP_FLAG_RST )  ? 'R' : '.',	/* 0x0004: Reset the connection */
			( xFlags & ipTCP_FLAG_PSH )  ? 'P' : '.',	/* 0x0008: Push function: please push buffered data to the recv application */
			( xFlags & ipTCP_FLAG_ACK )  ? 'A' : '.',	/* 0x0010: Acknowledgment field is significant */
			( xFlags & ipTCP_FLAG_URG )  ? 'U' : '.',	/* 0x0020: Urgent pointer field is significant */
			( xFlags & ipTCP_FLAG_ECN )  ? 'E' : '.',	/* 0x0040: ECN-Echo */
			( xFlags & ipTCP_FLAG_CWR )  ? 'C' : '.',	/* 0x0080: Congestion Window Reduced */
			( xFlags & ipTCP_FLAG_NS )   ? 'N' : '.');	/* 0x0100: ECN-nonce concealment protection */
		return retString;
	}
	/*-----------------------------------------------------------*/

#endif /* ipconfigHAS_DEBUG_PRINTF */

/*
 * Parse the TCP option(s) received, if present.  It has already been verified
 * that: ((pxTCPHeader->ucTcpOffset & 0xf0) > 0x50), meaning that the TP header
 * is longer than the usual 20 (5 x 4) bytes.
 */
static void prvCheckOptions( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer )
{
xTCPPacket_t * pxTCPPacket;
xTCPHeader_t * pxTCPHeader;
const unsigned char *pucPtr;
const unsigned char *pucLast;
TCPWindow_t *pxTcpWindow;
BaseType_t xNewMSS;

	pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
	pxTCPHeader = &pxTCPPacket->xTCPHeader;

	/* A character pointer to iterate through the option data */
	pucPtr = pxTCPHeader->ucOptdata;
	pucLast = pucPtr + (((pxTCPHeader->ucTcpOffset >> 4) - 5) << 2);
	pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;

	/* The comparison with pucLast is only necessary in case the option data are
	corrupted, we don't like to run into invalid memory and crash. */
	while( pucPtr < pucLast )
	{
		if( pucPtr[0] == TCP_OPT_END )
		{
			/* End of options. */
			return;
		}
		if( pucPtr[0] == TCP_OPT_NOOP)
		{
			pucPtr++;

			/* NOP option, inserted to make the length a multiple of 4. */
		}
		else if( ( pucPtr[0] == TCP_OPT_MSS ) && ( pucPtr[1] == TCP_OPT_MSS_LEN ) )
		{
			/* An MSS option with the correct option length.  FreeRTOS_htons()
			is not needed here because usChar2u16() already returns a host
			endian number. */
			xNewMSS = usChar2u16( pucPtr + 2 );

			if( pxSocket->u.xTcp.usInitMSS != xNewMSS )
			{
				FreeRTOS_debug_printf( ( "MSS change %u -> %lu\n", pxSocket->u.xTcp.usInitMSS, xNewMSS ) );
			}

			if( pxSocket->u.xTcp.usInitMSS > xNewMSS )
			{
				/* our MSS was bigger than the MSS of the other party: adapt it. */
				pxSocket->u.xTcp.bits.bMssChange = pdTRUE;
				if( pxTcpWindow && pxSocket->u.xTcp.usCurMSS > xNewMSS )
				{
					/* The peer advertises a smaller MSS than this socket was
					using.  Use that as well. */
					FreeRTOS_debug_printf( ( "Change mss %d => %lu\n", pxSocket->u.xTcp.usCurMSS, xNewMSS ) );
					pxSocket->u.xTcp.usCurMSS = ( uint16_t ) xNewMSS;
				}
				pxTcpWindow->xSize.ulRxWindowLength = ( ( uint32_t ) xNewMSS ) * ( pxTcpWindow->xSize.ulRxWindowLength / ( ( uint32_t ) xNewMSS ) );
				pxTcpWindow->usMSSInit = ( uint16_t ) xNewMSS;
				pxTcpWindow->usMSS = ( uint16_t ) xNewMSS;
				pxSocket->u.xTcp.usInitMSS = ( uint16_t ) xNewMSS;
				pxSocket->u.xTcp.usCurMSS = ( uint16_t ) xNewMSS;
			}

			#if( ipconfigUSE_TCP_WIN != 1 )
				/* Without scaled windows, MSS is the only interesting option. */
				break;
			#else
				/* Or else we continue to check another option: selective ACK. */
				pucPtr += TCP_OPT_MSS_LEN;
			#endif	/* ipconfigUSE_TCP_WIN != 1 */
		}
		else
		{
			/* All other options have a length field, so that we easily
			can skip past them. */
			int len = pucPtr[ 1 ];
			if( len == 0 )
			{
				/* If the length field is zero, the options are malformed
				and we don't process them further. */
				break;
			}

			#if( ipconfigUSE_TCP_WIN == 1 )
			{
				/* Selective ACK: the peer has received a packet but it is missing earlier
				packets.  At least this packet does not need retransmission anymore
				ulTCPWindowTxSack( ) takes care of this administration. */
				if( pucPtr[0] == TCP_OPT_SACK_A )
				{
					len -= 2;
					pucPtr += 2;

					while( len >= 8 )
					{
						uint32_t ulFirst = ulChar2u32( pucPtr );
						uint32_t ulLast  = ulChar2u32( pucPtr + 4 );
						uint32_t ulCount = ulTCPWindowTxSack( &pxSocket->u.xTcp.xTcpWindow, ulFirst, ulLast );
						/* ulTCPWindowTxSack( ) returns the number of bytes which have been acked
						starting from the head position.
						Advance the tail pointer in txStream. */
						if( pxSocket->u.xTcp.txStream && ulCount > 0 )
						{
							/* Just advancing the tail index, 'ulCount' bytes have been confirmed. */
							lStreamBufferGet( pxSocket->u.xTcp.txStream, 0, NULL, ( int32_t ) ulCount, pdFALSE );
							pxSocket->xEventBits |= eSOCKET_SEND;

							#if ipconfigSUPPORT_SELECT_FUNCTION == 1
							{
								if( pxSocket->xSelectBits & eSELECT_WRITE )
								{
									/* The field 'xEventBits' is used to store regular socket events (at most 8),
									as well as 'select events', which will be left-shifted */
									pxSocket->xEventBits |= ( eSELECT_WRITE << SOCKET_EVENT_BIT_COUNT );
								}
							}
							#endif

							/* In case the socket owner has installed an OnSent handler,
							call it now. */
							#if( ipconfigUSE_CALLBACKS == 1 )
							{
								if( ipconfigIS_VALID_PROG_ADDRESS( pxSocket->u.xTcp.pHndSent ) )
								{
									pxSocket->u.xTcp.pHndSent( (xSocket_t *)pxSocket, ulCount );
								}
							}
							#endif /* ipconfigUSE_CALLBACKS == 1  */
						}
						pucPtr += 8;
						len -= 8;
					}
					/* len should be 0 by now. */
				}
				#if	ipconfigUSE_TCP_TIMESTAMPS == 1
					else if( pucPtr[0] == TCP_OPT_TIMESTAMP )
					{
						len -= 2;	/* Skip option and length byte. */
						pucPtr += 2;
						pxSocket->u.xTcp.xTcpWindow.u.bits.bTimeStamps = pdTRUE;
						pxSocket->u.xTcp.xTcpWindow.rx.ulTimeStamp = ulChar2u32( pucPtr );
						pxSocket->u.xTcp.xTcpWindow.tx.ulTimeStamp = ulChar2u32( pucPtr + 4 );
						pxSocket->u.xTcp.xTcpWindow.u.bits.bTimeStamps = 1;
					}
				#endif	/* ipconfigUSE_TCP_TIMESTAMPS == 1 */
			}
			#endif	/* ipconfigUSE_TCP_WIN == 1 */

			pucPtr += len;
		}
	}
}
/*-----------------------------------------------------------*/

/*
 * When opening a TCP connection, while SYN's are being sent, the  parties may
 * communicate what MSS (Maximum Segment Size) they intend to use.   MSS is the
 * nett size of the payload, always smaller than MTU.
*/
static BaseType_t prvSetSynAckOptions( xFreeRTOS_Socket_t *pxSocket, xTCPPacket_t * pxTCPPacket )
{
xTCPHeader_t *pxTCPHeader = &pxTCPPacket->xTCPHeader;
uint16_t usMSS = pxSocket->u.xTcp.usInitMSS;
#if	ipconfigUSE_TCP_WIN == 1
BaseType_t xOptionsLength;
#endif

	/* We send out the TCP Maximum Segment Size option with our SYN[+ACK]. */

	pxTCPHeader->ucOptdata[0] = TCP_OPT_MSS;
	pxTCPHeader->ucOptdata[1] = TCP_OPT_MSS_LEN;
	pxTCPHeader->ucOptdata[2] = ( uint8_t ) ( usMSS >> 8 );
	pxTCPHeader->ucOptdata[3] = ( uint8_t ) ( usMSS & 0xff );

	#if( ipconfigUSE_TCP_WIN == 0 )
	{
		return 4;
	}
	#else
	{
		#if( ipconfigUSE_TCP_TIMESTAMPS == 1 )
			if( pxSocket->u.xTcp.xTcpWindow.u.bits.bTimeStamps )
			{
				prvTCPSetTimeStamp( 4, pxSocket, &pxTCPPacket->xTCPHeader );
				pxTCPHeader->ucOptdata[14] = TCP_OPT_SACK_P;	/* 4: Sack-Permitted Option. */
				pxTCPHeader->ucOptdata[15] = 2;
				xOptionsLength = 16;
			}
			else
		#endif
		{
			pxTCPHeader->ucOptdata[4] = TCP_OPT_NOOP;
			pxTCPHeader->ucOptdata[5] = TCP_OPT_NOOP;
			pxTCPHeader->ucOptdata[6] = TCP_OPT_SACK_P;	/* 4: Sack-Permitted Option. */
			pxTCPHeader->ucOptdata[7] = 2;
			xOptionsLength = 8;
		}
		return xOptionsLength; /* bytes, not words. */
	}
	#endif	/* ipconfigUSE_TCP_WIN == 0 */
}

/*
 * For anti-hanging protection and TCP keep-alive messages.  Called in two
 * places: after receiving a packet and after a state change.  The socket's
 * alive timer may be reset.
 */
static void prvTCPTouchSocket( xFreeRTOS_Socket_t *pxSocket )
{
	#if( ipconfigTCP_HANG_PROTECTION == 1 )
	{
		pxSocket->u.xTcp.xLastActTime = xTaskGetTickCount( );
	}
	#endif

	#if( ipconfigTCP_KEEP_ALIVE == 1 )
	{
		pxSocket->u.xTcp.bits.bWaitKeepAlive = 0;
		pxSocket->u.xTcp.bits.bSendKeepAlive = 0;
		pxSocket->u.xTcp.ucKeepRepCount = 0;
		pxSocket->u.xTcp.xLastAliveTime = xTaskGetTickCount( );
	}
	#endif

	( void ) pxSocket;
}
/*-----------------------------------------------------------*/

/*
 * Changing to a new state. Centralised here to do specific actions such as
 * resetting the alive timer, calling the user's OnConnect handler to notify
 * that a socket has got (dis)connected, and setting bit to unblock a call to
 * FreeRTOS_select()
 */
void vTCPStateChange( xFreeRTOS_Socket_t *pxSocket, BaseType_t xTcpState )
{
xFreeRTOS_Socket_t *xParent;
BaseType_t bBefore = NOW_CONNECTED( pxSocket->u.xTcp.ucTcpState );	/* Was it connected ? */
BaseType_t bAfter  = NOW_CONNECTED( xTcpState );					/* Is it connected now ? */
#if( ipconfigHAS_DEBUG_PRINTF != 0 )
	BaseType_t xPreviousState = pxSocket->u.xTcp.ucTcpState;
#endif
#if( ipconfigUSE_CALLBACKS == 1 )
	xFreeRTOS_Socket_t *xConnected = NULL;
#endif

	/* Has the connected status changed? */
	if( bBefore != bAfter )
	{
		/* Is the socket connected now ? */
		if( bAfter != pdFALSE )
		{
			/* if bPassQueued is true, this socket is an orphan until it gets connected. */
			if( pxSocket->u.xTcp.bits.bPassQueued != pdFALSE )
			{
				/* Now that it is connected, find it's parent. */
				if( pxSocket->u.xTcp.bits.bReuseSocket != pdFALSE )
				{
					xParent = pxSocket;
				}
				else
				{
					xParent = pxSocket->u.xTcp.pxPeerSocket;
					configASSERT( xParent != NULL );
				}
				if( xParent != NULL )
				{
					if( xParent->u.xTcp.pxPeerSocket == NULL )
					{
						xParent->u.xTcp.pxPeerSocket = pxSocket;
					}

					xParent->xEventBits |= eSOCKET_ACCEPT;

					#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
					{
						/* Library support FreeRTOS_select().  Receiving a new
						connection is being translated as a READ event. */
						if( ( xParent->xSelectBits & eSELECT_READ ) != 0 )
						{
							xParent->xEventBits |= ( eSELECT_READ << SOCKET_EVENT_BIT_COUNT );
						}
					}
					#endif

					vWakeUpSocketUser( xParent );

					#if( ipconfigUSE_CALLBACKS == 1 )
					{
						if( ( ipconfigIS_VALID_PROG_ADDRESS( xParent->u.xTcp.pHndConnected ) != pdFALSE ) &&
							( xParent->u.xTcp.bits.bReuseSocket == 0 ) )
						{
							/* The listening socket does not become connected itself, in stead
							a child socket is created.
							Postpone a call the OnConnect event until the end of this function. */
							xConnected = xParent;
						}
					}
					#endif
				}

				/* Don't need to access the parent socket anymore, so the
				reference 'pxPeerSocket' may be cleared. */
				pxSocket->u.xTcp.pxPeerSocket = NULL;
				pxSocket->u.xTcp.bits.bPassQueued = pdFALSE;

				/* When true, this socket may be returned in a call to accept(). */
				pxSocket->u.xTcp.bits.bPassAccept = pdTRUE;
			}
			else
			{
				pxSocket->xEventBits |= eSOCKET_CONNECT;

				#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
				{
					if( pxSocket->xSelectBits & eSELECT_WRITE )
					{
						pxSocket->xEventBits |= ( eSELECT_WRITE << SOCKET_EVENT_BIT_COUNT );
					}
				}
				#endif
			}
		}
		else  /* bAfter == pdFALSE, connection is closed. */
		{
			/* Notify/wake-up the socket-owner by setting a semaphore. */
			pxSocket->xEventBits |= eSOCKET_CLOSED;

			#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
			{
				if( ( pxSocket->xSelectBits & eSELECT_EXCEPT ) != 0 )
				{
					pxSocket->xEventBits |= ( eSELECT_EXCEPT << SOCKET_EVENT_BIT_COUNT );
				}
			}
			#endif
		}
		#if( ipconfigUSE_CALLBACKS == 1 )
		{
			if( ( ipconfigIS_VALID_PROG_ADDRESS( pxSocket->u.xTcp.pHndConnected ) != pdFALSE ) && ( xConnected == NULL ) )
			{
				/* The 'connected' state has changed, call the user handler. */
				xConnected = pxSocket;
			}
		}
		#endif /* ipconfigUSE_CALLBACKS */

		if( prvTCPSocketIsActive( pxSocket->u.xTcp.ucTcpState ) == pdFALSE )
		{
			/* Now the socket isn't in an active state anymore so it
			won't need further attention of the IP-task.
			Setting time-out to zero means that the socket won't get checked during
			timer events. */
			pxSocket->u.xTcp.usTimeout = 0;
		}
	}
	else
	{
		if( xTcpState == eCLOSED )
		{
			/* Socket goes to status eCLOSED because of a RST.
			When nobody owns the socket yet, delete it. */
			if( ( pxSocket->u.xTcp.bits.bPassQueued != pdFALSE ) ||
				( pxSocket->u.xTcp.bits.bPassAccept != pdFALSE ) )
			{
				FreeRTOS_debug_printf( ( "vTCPStateChange: Closing socket\n" ) );
				if( pxSocket->u.xTcp.bits.bReuseSocket == pdFALSE )
				{
					FreeRTOS_closesocket( pxSocket );
				}
			}
		}
	}

	/* Fill in the new state. */
	pxSocket->u.xTcp.ucTcpState = ( uint8_t ) xTcpState;

	/* touch the alive timers because moving to another state. */
	prvTCPTouchSocket( pxSocket );

	#if( ipconfigHAS_DEBUG_PRINTF == 1 )
	{
	if( ( xTCPWindowLoggingLevel >= 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxSocket->usLocPort ) != pdFALSE ) )
		FreeRTOS_debug_printf( ( "Socket %d -> %lxip:%u State %s->%s\n",
			pxSocket->usLocPort,
			pxSocket->u.xTcp.ulRemoteIP,
			pxSocket->u.xTcp.usRemotePort,
			FreeRTOS_GetTCPStateName( ( UBaseType_t ) xPreviousState ),
			FreeRTOS_GetTCPStateName( ( UBaseType_t ) xTcpState ) ) );
	}
	#endif /* ipconfigHAS_DEBUG_PRINTF */

	#if( ipconfigUSE_CALLBACKS == 1 )
	{
		if( xConnected != NULL )
		{
			/* The 'connected' state has changed, call the OnConnect handler of the parent. */
			xConnected->u.xTcp.pHndConnected( ( xSocket_t * ) xConnected, bAfter );
		}
	}
	#endif
}
/*-----------------------------------------------------------*/

static xNetworkBufferDescriptor_t *prvTCPBufferResize( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer,
	int32_t lDataLen, BaseType_t xOptionsLength )
{
xNetworkBufferDescriptor_t *pxReturn;
int32_t lNeeded;
BaseType_t xResize;

	if( xBufferAllocFixedSize != pdFALSE )
	{
		/* Network buffers are created with a fixed size and can hold the largest
		MTU. */
		lNeeded = ipTOTAL_ETHERNET_FRAME_SIZE;
		/* and therefore, the buffer won't be too small.
		Only ask for a new network buffer in case none was supplied. */
		xResize = ( pxNetworkBuffer == NULL );
	}
	else
	{
		/* Network buffers are created with a variable size. See if it must
		grow. */
		lNeeded = FreeRTOS_max_int32( ( int32_t ) sizeof( pxSocket->u.xTcp.lastPacket ),
			ipSIZE_OF_ETH_HEADER + ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + xOptionsLength + lDataLen );
		/* In case we were called from a TCP timer event, a buffer must be
		created.  Otherwise, test 'xDataLength' of the provided buffer. */
		xResize = ( pxNetworkBuffer == NULL ) || ( pxNetworkBuffer->xDataLength < (size_t)lNeeded );
	}

	if( xResize != pdFALSE )
	{
		/* The caller didn't provide a network buffer or the provided buffer is
		too small.  As we must send-out a data packet, a buffer will be created
		here. */
		pxReturn = pxGetNetworkBufferWithDescriptor( ( uint32_t ) lNeeded, 0 );

		if( pxReturn != NULL )
		{
			/* Copy the existing data to the new created buffer. */
			if( pxNetworkBuffer )
			{
				/* Either from the previous buffer... */
				memcpy( pxReturn->pucEthernetBuffer, pxNetworkBuffer->pucEthernetBuffer, pxNetworkBuffer->xDataLength );

				/* ...and release it. */
				vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
			}
			else
			{
				/* Or from the socket field 'xTcp.lastPacket'. */
				memcpy( pxReturn->pucEthernetBuffer, pxSocket->u.xTcp.lastPacket, sizeof( pxSocket->u.xTcp.lastPacket ) );
			}
		}
	}
	else
	{
		/* xResize is false, the network buffer provided was big enough. */
		pxReturn = pxNetworkBuffer;
	}

	return pxReturn;
}
/*-----------------------------------------------------------*/

/*
 * Prepare an outgoing message, in case anything has to be sent.
 */
static int32_t prvTCPPrepareSend( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer, BaseType_t xOptionsLength )
{
int32_t lDataLen;
uint8_t *pucEthernetBuffer, *pucSendData;
xTCPPacket_t *pxTCPPacket;
int32_t lOffset;
uint32_t ulDataGot, ulDistance;
TCPWindow_t *pxTcpWindow;
xNetworkBufferDescriptor_t *pxNewBuffer;
int32_t lStreamPos;

	if( ( *ppxNetworkBuffer ) != NULL )
	{
		/* A network buffer descriptor was already supplied */
		pucEthernetBuffer = ( *ppxNetworkBuffer )->pucEthernetBuffer;
	}
	else
	{
		/* For now let it point to the last packet header */
		pucEthernetBuffer = pxSocket->u.xTcp.lastPacket;
	}

	pxTCPPacket = ( xTCPPacket_t * ) ( pucEthernetBuffer );
	pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;
	lDataLen = 0;
	lStreamPos = 0;
	pxTCPPacket->xTCPHeader.ucTcpFlags |= ipTCP_FLAG_ACK;

	if( pxSocket->u.xTcp.txStream != NULL )
	{
		/* ulTCPWindowTxGet will return the amount of data which may be sent
		along with the position in the txStream.
		Why check for MSS > 1 ?
		Because some TCP-stacks (like uIP) use it for flow-control. */
		if( pxSocket->u.xTcp.usCurMSS > 1 )
		{
			lDataLen = ( int32_t ) ulTCPWindowTxGet( pxTcpWindow, pxSocket->u.xTcp.wnd, &lStreamPos );
		}

		if( lDataLen > 0 )
		{
			/* Check if the current network buffer is big enough, if not,
			resize it. */
			pxNewBuffer = prvTCPBufferResize( pxSocket, *ppxNetworkBuffer, lDataLen, xOptionsLength );

			if( pxNewBuffer != NULL )
			{
				*ppxNetworkBuffer = pxNewBuffer;
				pucEthernetBuffer = pxNewBuffer->pucEthernetBuffer;
				pxTCPPacket = ( xTCPPacket_t * ) ( pucEthernetBuffer );

				pucSendData = pucEthernetBuffer + ipSIZE_OF_ETH_HEADER + ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + xOptionsLength;

				/* Translate the position in txStream to an offset from the tail
				marker. */
				lOffset = lStreamBufferDistance( pxSocket->u.xTcp.txStream, pxSocket->u.xTcp.txStream->lTail, lStreamPos );

				/* Here data is copied from the txStream in 'peek' mode.  Only
				when the packets are acked, the tail marker will be updated. */
				ulDataGot = ( uint32_t ) lStreamBufferGet( pxSocket->u.xTcp.txStream, lOffset, pucSendData, ( int32_t ) lDataLen, pdTRUE );

				#if( ipconfigHAS_DEBUG_PRINTF != 0 )
				{
					if( ulDataGot != ( uint32_t ) lDataLen )
					{
						FreeRTOS_debug_printf( ( "lStreamBufferGet: pos %lu offs %lu only %lu != %lu\n",
							lStreamPos, lOffset, ulDataGot, lDataLen ) );
					}
				}
				#endif

				/* If the owner of the socket requests a closure, add the FIN
				flag to the last packet. */
				if( pxSocket->u.xTcp.bits.bCloseRequested && !pxSocket->u.xTcp.bits.bFinSent )
				{
					ulDistance = ( uint32_t ) lStreamBufferDistance( pxSocket->u.xTcp.txStream, lStreamPos, pxSocket->u.xTcp.txStream->lHead );

					if( ulDistance == ulDataGot )
					{
						FreeRTOS_debug_printf( ( "CheckClose %lu <= %lu (%lu <= %lu <= %lu)\n", ulDataGot, ulDistance,
							pxSocket->u.xTcp.txStream->lTail,
							pxSocket->u.xTcp.txStream->lMid,
							pxSocket->u.xTcp.txStream->lHead ) );

						/* Although the socket sends a FIN, it will stay in
						ESTABLISHED until all current data has been received or
						delivered. */
						pxTCPPacket->xTCPHeader.ucTcpFlags |= ipTCP_FLAG_FIN;
						pxTcpWindow->tx.ulFINSequenceNumber = pxTcpWindow->ulOurSequenceNumber + ( uint32_t ) lDataLen;
						pxSocket->u.xTcp.bits.bFinSent = pdTRUE;
					}
				}
			}
			else
			{
				lDataLen = -1;
			}
		}
	}

	if( ( lDataLen >= 0 ) && ( pxSocket->u.xTcp.ucTcpState == eESTABLISHED ) )
	{
		/* See if the socket owner wants to shutdown this connection. */
		if( pxSocket->u.xTcp.bits.bUserShutdown && xTCPWindowTxDone( pxTcpWindow ) )
		{
			pxSocket->u.xTcp.bits.bUserShutdown = pdFALSE;
			pxTCPPacket->xTCPHeader.ucTcpFlags |= ipTCP_FLAG_FIN;
			pxSocket->u.xTcp.bits.bFinSent = pdTRUE;
			pxSocket->u.xTcp.bits.bWinChange = pdTRUE;
			pxTcpWindow->tx.ulFINSequenceNumber = pxTcpWindow->tx.ulCurrentSequenceNumber;
			vTCPStateChange( pxSocket, eFIN_WAIT_1 );
		}

		#if( ipconfigTCP_KEEP_ALIVE != 0 )
		{
			if( pxSocket->u.xTcp.ucKeepRepCount > 3 )
			{
				FreeRTOS_debug_printf( ( "keep-alive: giving up %lxip:%u\n",
					pxSocket->u.xTcp.ulRemoteIP,			/* IP address of remote machine. */
					pxSocket->u.xTcp.usRemotePort ) );	/* Port on remote machine. */
				vTCPStateChange( pxSocket, eCLOSE_WAIT );
				lDataLen = -1;
			}
			if( ( lDataLen == 0 ) && ( pxSocket->u.xTcp.bits.bWinChange == 0 ) )
			{
				/* If there is no data to be sent, and no window-update message,
				we might want to send a keep-alive message. */
				TickType_t xAge = xTaskGetTickCount( ) - pxSocket->u.xTcp.xLastAliveTime;
				TickType_t xMax;
				xMax = ( ipconfigTCP_KEEP_ALIVE_INTERVAL * configTICK_RATE_HZ );
				if( pxSocket->u.xTcp.ucKeepRepCount )
				{
					xMax = ( 3 * configTICK_RATE_HZ );
				}
				if( xAge > xMax )
				{
					pxSocket->u.xTcp.xLastAliveTime = xTaskGetTickCount( );
					if( xTCPWindowLoggingLevel )
						FreeRTOS_debug_printf( ( "keep-alive: %lxip:%u count %u\n",
							pxSocket->u.xTcp.ulRemoteIP,
							pxSocket->u.xTcp.usRemotePort,
							pxSocket->u.xTcp.ucKeepRepCount ) );
					pxSocket->u.xTcp.bits.bSendKeepAlive = pdTRUE;
					pxSocket->u.xTcp.usTimeout = ( ( uint16_t ) pdMS_TO_TICKS( 2500 ) );
					pxSocket->u.xTcp.ucKeepRepCount++;
				}
			}
		}
		#endif /* ipconfigTCP_KEEP_ALIVE */
	}

	/* Anything to send, a change of the advertised window size, or maybe send a
	keep-alive message? */
	if( ( lDataLen > 0 ) ||
		( pxSocket->u.xTcp.bits.bWinChange != pdFALSE ) ||
		( pxSocket->u.xTcp.bits.bSendKeepAlive != pdFALSE ) )
	{
		pxTCPPacket->xTCPHeader.ucTcpFlags &= ( ( uint8_t ) ~ipTCP_FLAG_PSH );
		pxTCPPacket->xTCPHeader.ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );

		pxTCPPacket->xTCPHeader.ucTcpFlags |= ( uint8_t ) ipTCP_FLAG_ACK;

		if( lDataLen != 0l )
		{
			pxTCPPacket->xTCPHeader.ucTcpFlags |= ( uint8_t ) ipTCP_FLAG_PSH;
		}

		#if	ipconfigUSE_TCP_TIMESTAMPS == 1
		{
			if( xOptionsLength == 0 )
			{
				if( pxSocket->u.xTcp.xTcpWindow.u.bits.bTimeStamps )
				{
					xTCPPacket_t * pxTCPPacket = ( xTCPPacket_t * ) ( pucEthernetBuffer );
					xOptionsLength = prvTCPSetTimeStamp( 0, pxSocket, &pxTCPPacket->xTCPHeader );
				}
			}
		}
		#endif

		lDataLen += ( ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + ( ( int32_t ) xOptionsLength ) );
	}

	return lDataLen;
}
/*-----------------------------------------------------------*/

/*
 * Calculate after how much time this socket needs to be checked again.
 */
static TickType_t prvTCPNextTimeout ( xFreeRTOS_Socket_t *pxSocket )
{
TickType_t ulDelayMs = 20000;

	if( pxSocket->u.xTcp.ucTcpState == eCONNECT_SYN )
	{
		/* The socket is actively connecting to a peer. */
		if( pxSocket->u.xTcp.bits.bConnPrepared )
		{
			/* Ethernet address has been found, use progressive timeout for
			active connect(). */
			if( pxSocket->u.xTcp.ucRepCount < 3 )
			{
				ulDelayMs = ( 3000UL << ( pxSocket->u.xTcp.ucRepCount - 1 ) );
			}
			else
			{
				ulDelayMs = 11000UL;
			}
		}
		else
		{
			/* Still in the ARP phase: check every half second. */
			ulDelayMs = 500UL;
		}

		FreeRTOS_debug_printf( ( "Connect[%lxip:%u]: next timeout %u: %lu ms\n",
			pxSocket->u.xTcp.ulRemoteIP, pxSocket->u.xTcp.usRemotePort,
			pxSocket->u.xTcp.ucRepCount, ulDelayMs ) );
		pxSocket->u.xTcp.usTimeout = ( uint16_t )pdMS_TO_TICKS( ulDelayMs );
	}
	else if( pxSocket->u.xTcp.usTimeout == 0 )
	{
		/* Let the sliding window mechanism decide what time-out is appropriate. */
		BaseType_t xResult = xTCPWindowTxHasData( &pxSocket->u.xTcp.xTcpWindow, pxSocket->u.xTcp.wnd, &ulDelayMs );
		if( ulDelayMs == 0 )
		{
			ulDelayMs = xResult ? 1UL : 20000UL;
		}
		else
		{
			/* ulDelayMs contains the time to wait before a re-transmission. */
		}
		pxSocket->u.xTcp.usTimeout = ( uint16_t )pdMS_TO_TICKS( ulDelayMs );
	}
	else
	{
		/* field '.usTimeout' has already been set (by the
		keep-alive/delayed-ACK mechanism). */
	}

	/* Return the number of clock ticks before the timer expires. */
	return pxSocket->u.xTcp.usTimeout;
}
/*-----------------------------------------------------------*/

static void prvTCPAddTxData( xFreeRTOS_Socket_t *pxSocket )
{
int32_t lCount, lLength;

	/* A txStream has been created already, see if the socket has new data for
	the sliding window.

	lStreamBufferMidSpace() returns the distance between rxHead and rxMid.  It contains new
	Tx data which has not been passed to the sliding window yet.  The oldest
	data not-yet-confirmed can be found at rxTail. */
	lLength = lStreamBufferMidSpace( pxSocket->u.xTcp.txStream );

	if( lLength > 0 )
	{
		/* All data between txMid and rxHead will now be passed to the sliding
		window manager, so it can start transmitting them.

		Hand over the new data to the sliding window handler.  It will be
		split-up in chunks of 1460 bytes each (or less, depending on
		ipconfigTCP_MSS). */
		lCount = lTCPWindowTxAdd(	&pxSocket->u.xTcp.xTcpWindow,
								( uint32_t ) lLength,
								pxSocket->u.xTcp.txStream->lMid,
								pxSocket->u.xTcp.txStream->LENGTH );

		/* Move the rxMid pointer forward up to rxHead. */
		if( lCount > 0 )
		{
			vStreamBufferMoveMid( pxSocket->u.xTcp.txStream, lCount );
		}
	}
}
/*-----------------------------------------------------------*/

/*
 * prvTCPHandleFin() will be called to handle socket closure
 * The Closure starts when either a FIN has been received and accepted,
 * Or when the socket has sent a FIN flag to the peer
 * Before being called, it has been checked that both reception and transmission
 * are complete.
 */
static BaseType_t prvTCPHandleFin( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &pxTCPPacket->xTCPHeader;
uint8_t ucTcpFlags = pxTCPHeader->ucTcpFlags;
TCPWindow_t *pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;
BaseType_t xSendLength = 0;
uint32_t ulAckNr = FreeRTOS_ntohl( pxTCPHeader->ulAckNr );

	if( ( ucTcpFlags & ipTCP_FLAG_FIN ) != 0 )
	{
		pxTcpWindow->rx.ulCurrentSequenceNumber = pxTcpWindow->rx.ulFINSequenceNumber + 1;
	}
	if( pxSocket->u.xTcp.bits.bFinSent == pdFALSE )
	{
		/* We haven't yet replied with a FIN, do so now. */
		pxTcpWindow->tx.ulFINSequenceNumber = pxTcpWindow->tx.ulCurrentSequenceNumber;
		pxSocket->u.xTcp.bits.bFinSent = pdTRUE;
	}
	else
	{
		/* We did send a FIN already, see if it's ACK'd. */
		if( ulAckNr == pxTcpWindow->tx.ulFINSequenceNumber + 1 )
		{
			pxSocket->u.xTcp.bits.bFinAcked = pdTRUE;
		}
	}

	if( pxSocket->u.xTcp.bits.bFinAcked == pdFALSE )
	{
		pxTcpWindow->tx.ulCurrentSequenceNumber = pxTcpWindow->tx.ulFINSequenceNumber;
		pxTCPHeader->ucTcpFlags = ipTCP_FLAG_ACK | ipTCP_FLAG_FIN;

		/* And wait for the final ACK. */
		vTCPStateChange( pxSocket, eLAST_ACK );
	}
	else
	{
		/* Our FIN has been ACK'd, the outgoing sequence number is now fixed. */
		pxTcpWindow->tx.ulCurrentSequenceNumber = pxTcpWindow->tx.ulFINSequenceNumber + 1;
		if( pxSocket->u.xTcp.bits.bFinRecv == pdFALSE )
		{
			/* We have sent out a FIN but the peer hasn't replied with a FIN
			yet. Do nothing for the moment. */
			pxTCPHeader->ucTcpFlags = 0;
		}
		else
		{
			if( pxSocket->u.xTcp.bits.bFinLast == pdFALSE )
			{
				/* This is the third of the three-way hand shake: the last
				ACK. */
				pxTCPHeader->ucTcpFlags = ipTCP_FLAG_ACK;
			}
			else
			{
				/* The other party started the closure, so we just wait for the
				last ACK. */
				pxTCPHeader->ucTcpFlags = 0;
			}

			/* And wait for the user to close this socket. */
			vTCPStateChange( pxSocket, eCLOSE_WAIT );
		}
	}

	pxTcpWindow->ulOurSequenceNumber = pxTcpWindow->tx.ulCurrentSequenceNumber;

	if( pxTCPHeader->ucTcpFlags != 0 )
	{
		xSendLength = ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + pxTcpWindow->ucOptionLength;
	}

	pxTCPHeader->ucTcpOffset = ( uint8_t ) ( ( ipSIZE_OF_TCP_HEADER + pxTcpWindow->ucOptionLength ) << 2 );

	if( xTCPWindowLoggingLevel != 0 )
	{
		FreeRTOS_debug_printf( ( "TCP: send FIN+ACK (ack %lu, cur/nxt %lu/%lu) ourSeqNr %lu | Rx %lu\n",
			ulAckNr - pxTcpWindow->tx.ulFirstSequenceNumber,
			pxTcpWindow->tx.ulCurrentSequenceNumber - pxTcpWindow->tx.ulFirstSequenceNumber,
			pxTcpWindow->ulNextTxSequenceNumber - pxTcpWindow->tx.ulFirstSequenceNumber,
			pxTcpWindow->ulOurSequenceNumber - pxTcpWindow->tx.ulFirstSequenceNumber,
			pxTcpWindow->rx.ulCurrentSequenceNumber - pxTcpWindow->rx.ulFirstSequenceNumber ) );
	}

	return xSendLength;
}
/*-----------------------------------------------------------*/

#if	ipconfigUSE_TCP_TIMESTAMPS == 1

	static BaseType_t prvTCPSetTimeStamp( BaseType_t lOffset, xFreeRTOS_Socket_t *pxSocket, xTCPHeader_t *pxTCPHeader )
	{
	uint32_t ulTimes[2];
	uint8_t *ucOptdata = &( pxTCPHeader->ucOptdata[ lOffset ] );

		ulTimes[0]   = ( xTaskGetTickCount ( ) * 1000 ) / configTICK_RATE_HZ;
		ulTimes[0]   = FreeRTOS_htonl( ulTimes[0] );
		ulTimes[1]   = FreeRTOS_htonl( pxSocket->u.xTcp.xTcpWindow.rx.ulTimeStamp );
		ucOptdata[0] = TCP_OPT_TIMESTAMP;
		ucOptdata[1] = TCP_OPT_TIMESTAMP_LEN;
		memcpy( &(ucOptdata[2] ), ulTimes, 8);
		ucOptdata[10] = TCP_OPT_NOOP;
		ucOptdata[11] = TCP_OPT_NOOP;
		/* Do not return the same timestamps 2 times. */
		pxSocket->u.xTcp.xTcpWindow.rx.ulTimeStamp = 0ul;
		return 12;
	}

#endif
/*-----------------------------------------------------------*/

/*
 * prvCheckRxData(): called from prvTCPHandleState()
 *
 * The first thing that will be done is find the TCP payload data
 * and check the length of this data.
 */
static BaseType_t prvCheckRxData( xNetworkBufferDescriptor_t *pxNetworkBuffer, uint8_t **ppucRecvData )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &( pxTCPPacket->xTCPHeader );
int32_t lLength, lTCPHeaderLength, lReceiveLength, lUrgentLength;

	/* Determine the length and the offset of the user-data sent to this
	node.

	The size of the TCP header is given in a multiple of 4-byte words (single
	byte, needs no ntoh() translation).  A shift-right 2: is the same as
	(offset >> 4) * 4. */
    lTCPHeaderLength = ( ( BaseType_t ) ( pxTCPHeader->ucTcpOffset & VALID_BITS_IN_TCP_OFFSET_BYTE ) ) >> 2;

	/* Let pucRecvData point to the first byte received. */
	*ppucRecvData = pxNetworkBuffer->pucEthernetBuffer + ipSIZE_OF_ETH_HEADER + ipSIZE_OF_IP_HEADER + lTCPHeaderLength;

	/* Calculate lReceiveLength - the length of the TCP data received.  This is
	equal to the total packet length minus:
	( LinkLayer length (14) + IP header length (20) + size of TCP header(20 +) ).*/
	lReceiveLength = ( ( int32_t ) pxNetworkBuffer->xDataLength ) - ipSIZE_OF_ETH_HEADER;
	lLength = FreeRTOS_htons( pxTCPPacket->xIPHeader.usLength );

	if( lReceiveLength > lLength )
	{
		/* More bytes were received than the reported length, often because of
		padding bytes at the end. */
		lReceiveLength = lLength;
	}

	/* Subtract the size of the TCP and IP headers and the actual data size is
	known. */
	if( lReceiveLength > lTCPHeaderLength + ipSIZE_OF_IP_HEADER )
	{
		lReceiveLength -= lTCPHeaderLength + ipSIZE_OF_IP_HEADER;
	}
	else
	{
		lReceiveLength = 0;
	}

	/* Urgent Pointer:
	This field communicates the current value of the urgent pointer as a
	positive offset from the sequence number in this segment.  The urgent
	pointer points to the sequence number of the octet following the urgent
	data.  This field is only be interpreted in segments with the URG control
	bit set. */
	if( ( pxTCPHeader->ucTcpFlags & ipTCP_FLAG_URG ) != 0 )
	{
		/* Although we ignore the urgent data, we have to skip it. */
		lUrgentLength = FreeRTOS_htons( pxTCPHeader->usUrgent );
		*ppucRecvData += lUrgentLength;
		lReceiveLength -= FreeRTOS_min_int32( lReceiveLength, lUrgentLength );
	}

	return lReceiveLength;
}
/*-----------------------------------------------------------*/

/*
 * prvStoreRxData(): called from prvTCPHandleState()
 *
 * The second thing is to do is check if the payload data may be accepted
 * If so, they will be added to the reception queue.
 */
static BaseType_t prvStoreRxData( xFreeRTOS_Socket_t *pxSocket, uint8_t *pucRecvData,
	xNetworkBufferDescriptor_t *pxNetworkBuffer, uint32_t ulReceiveLength )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &pxTCPPacket->xTCPHeader;
TCPWindow_t *pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;
uint32_t ulSequenceNumber, ulSpace;
int32_t lOffset, lStored;
BaseType_t xResult = 0;

	ulSequenceNumber = FreeRTOS_ntohl( pxTCPHeader->ulSequenceNumber );

	if( ( ulReceiveLength > 0 ) && ( pxSocket->u.xTcp.ucTcpState >= eSYN_RECEIVED ) )
	{
		/* See if way may accept the data contents and forward it to the socket
		owner.

		If it can't be "accept"ed it may have to be stored and send a selective
		ack (SACK) option to confirm it.  In that case, xTcpWinRxStore() will be
		called later to store an out-of-order packet (in case lOffset is
		negative). */
		if ( pxSocket->u.xTcp.rxStream )
		{
			ulSpace = ( uint32_t )lStreamBufferGetSpace ( pxSocket->u.xTcp.rxStream );
		}
		else
		{
			ulSpace = ( uint32_t )pxSocket->u.xTcp.rxStreamSize;
		}

		lOffset = lTCPWindowRxCheck( pxTcpWindow, ulSequenceNumber, ulReceiveLength, ulSpace );

		if( lOffset >= 0 )
		{
			/* New data has arrived and may be made available to the user.  See
			if the head marker in rxStream may be advanced,	only if lOffset == 0.
			In case the low-water mark is reached, bLowWater will be set
			"low-water" here stands for "little space". */
			lStored = lTCPAddRxdata( pxSocket, lOffset, pucRecvData, ulReceiveLength );

			if( lStored != ( int32_t ) ulReceiveLength )
			{
				FreeRTOS_debug_printf( ( "lTCPAddRxdata: stored %ld / %lu bytes??\n", lStored, ulReceiveLength ) );

				/* Received data could not be stored.  The socket's flag
				bMallocError has been set.  The socket now has the status
				eCLOSE_WAIT and a RST packet will be sent back. */
				prvTCPSendReset( pxNetworkBuffer );
				xResult = -1;
			}
		}

		/* After a missing packet has come in, higher packets may be passed to
		the user. */
		#if( ipconfigUSE_TCP_WIN == 1 )
		{
			/* Now lTCPAddRxdata() will move the rxHead pointer forward
			so data becomes available to the user immediately
			In case the low-water mark is reached, bLowWater will be set. */
			if( ( xResult == 0 ) && ( pxTcpWindow->ulUserDataLength > 0 ) )
			{
				lTCPAddRxdata( pxSocket, 0, NULL, pxTcpWindow->ulUserDataLength );
				pxTcpWindow->ulUserDataLength = 0;
			}
		}
		#endif /* ipconfigUSE_TCP_WIN */
	}
	else
	{
		pxTcpWindow->ucOptionLength = 0;
	}

	return xResult;
}
/*-----------------------------------------------------------*/

/* Set the TCP options (if any) for the outgoing packet. */
static BaseType_t prvSetOptions( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &pxTCPPacket->xTCPHeader;
TCPWindow_t *pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;
BaseType_t xOptionsLength = pxTcpWindow->ucOptionLength;

	#if(	ipconfigUSE_TCP_WIN == 1 )
		if( xOptionsLength )
		{
			/* TCP options must be sent because a packet which is out-of-order
			was received. */
			if( xTCPWindowLoggingLevel >= 0 )
				FreeRTOS_debug_printf( ( "SACK[%d,%d]: optlen %lu sending %lu - %lu\n",
					pxSocket->usLocPort,
					pxSocket->u.xTcp.usRemotePort,
					xOptionsLength,
					FreeRTOS_ntohl( pxTcpWindow->ulOptionsData[ 1 ] ) - pxSocket->u.xTcp.xTcpWindow.rx.ulFirstSequenceNumber,
					FreeRTOS_ntohl( pxTcpWindow->ulOptionsData[ 2 ] ) - pxSocket->u.xTcp.xTcpWindow.rx.ulFirstSequenceNumber ) );
			memcpy( pxTCPHeader->ucOptdata, pxTcpWindow->ulOptionsData, ( size_t ) xOptionsLength );

			/* The header length divided by 4, goes into the higher nibble,
			effectively a shift-left 2. */
			pxTCPHeader->ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );
		}
		else
	#endif	/* ipconfigUSE_TCP_WIN */
	if( ( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED ) && ( pxSocket->u.xTcp.bits.bMssChange != 0 ) )
	{
		/* TCP options must be sent because the MSS has changed. */
		pxSocket->u.xTcp.bits.bMssChange = pdFALSE;
		if( xTCPWindowLoggingLevel >= 0 )
		{
			FreeRTOS_debug_printf( ( "MSS: sending %d\n", pxSocket->u.xTcp.usCurMSS ) );
		}

		pxTCPHeader->ucOptdata[ 0 ] = TCP_OPT_MSS;
		pxTCPHeader->ucOptdata[ 1 ] = TCP_OPT_MSS_LEN;
		pxTCPHeader->ucOptdata[ 2 ] = ( uint8_t ) ( ( pxSocket->u.xTcp.usCurMSS ) >> 8 );
		pxTCPHeader->ucOptdata[ 3 ] = ( uint8_t ) ( ( pxSocket->u.xTcp.usCurMSS ) & 0xff );
		xOptionsLength = 4;
		pxTCPHeader->ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );
	}

	#if(	ipconfigUSE_TCP_TIMESTAMPS == 1 )
	{
		if( pxSocket->u.xTcp.xTcpWindow.u.bits.bTimeStamps )
		{
			xOptionsLength += prvTCPSetTimeStamp( xOptionsLength, pxSocket, pxTCPHeader );
		}
	}
	#endif	/* ipconfigUSE_TCP_TIMESTAMPS == 1 */

	return xOptionsLength;
}
/*-----------------------------------------------------------*/

/*
 * prvHandleSynReceived(): called from prvTCPHandleState()
 *
 * Called from the states: eSYN_RECEIVED and eCONNECT_SYN
 * If the flags received are correct, the socket will move to eESTABLISHED.
 */
static BaseType_t prvHandleSynReceived( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer,
	uint32_t ulReceiveLength, BaseType_t xOptionsLength )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( (*ppxNetworkBuffer)->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &pxTCPPacket->xTCPHeader;
TCPWindow_t *pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;
uint8_t ucTcpFlags = pxTCPHeader->ucTcpFlags;
uint32_t ulSequenceNumber = FreeRTOS_ntohl( pxTCPHeader->ulSequenceNumber );
BaseType_t xSendLength = 0;

	/* Either expect a ACK or a SYN+ACK. */
	uint16_t usExpect = ipTCP_FLAG_ACK;
	if( pxSocket->u.xTcp.ucTcpState == eCONNECT_SYN )
	{
		usExpect |= ipTCP_FLAG_SYN;
	}

	if( ( ucTcpFlags & 0x17 ) != usExpect )
	{
		/* eSYN_RECEIVED: flags 0010 expected, not 0002. */
		/* eSYN_RECEIVED: flags ACK  expected, not SYN. */
		FreeRTOS_debug_printf( ( "%s: flags %04X expected, not %04X\n",
			pxSocket->u.xTcp.ucTcpState == eSYN_RECEIVED ? "eSYN_RECEIVED" : "eCONNECT_SYN",
			usExpect, ucTcpFlags ) );
		vTCPStateChange( pxSocket, eCLOSE_WAIT );
		pxTCPHeader->ucTcpFlags |= ipTCP_FLAG_RST;
		xSendLength = ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + xOptionsLength;
		pxTCPHeader->ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );
	}
	else
	{
		pxTcpWindow->usPeerPortNumber = pxSocket->u.xTcp.usRemotePort;
		pxTcpWindow->usOurPortNumber = pxSocket->usLocPort;

		if( pxSocket->u.xTcp.ucTcpState == eCONNECT_SYN )
		{
			xTCPPacket_t *pxLastTCPPacket = ( xTCPPacket_t * ) ( pxSocket->u.xTcp.lastPacket );

			/* Clear the SYN flag in lastPacket. */
			pxLastTCPPacket->xTCPHeader.ucTcpFlags = ipTCP_FLAG_ACK;

			/* This socket was the one connecting actively so now perofmr the
			synchronisation. */
			vTCPWindowInit( &pxSocket->u.xTcp.xTcpWindow,
				ulSequenceNumber, pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber, pxSocket->u.xTcp.usCurMSS );
			pxTcpWindow->rx.ulCurrentSequenceNumber = pxTcpWindow->rx.ulHighestSequenceNumber = ulSequenceNumber + 1;
			pxTcpWindow->tx.ulCurrentSequenceNumber++; /* because we send a TCP_SYN [ | TCP_ACK ]; */
			pxTcpWindow->ulNextTxSequenceNumber++;
		}
		else if( ulReceiveLength == 0 )
		{
			pxTcpWindow->rx.ulCurrentSequenceNumber = ulSequenceNumber;
		}

		/* The SYN+ACK has been confirmed, increase the next sequence number by
		1. */
		pxTcpWindow->ulOurSequenceNumber = pxTcpWindow->tx.ulFirstSequenceNumber + 1;

		FreeRTOS_debug_printf( ( "TCP: %s %d => %lxip:%d set ESTAB (sock %p)\n",
			pxSocket->u.xTcp.ucTcpState == eCONNECT_SYN ? "active" : "passive",
			pxSocket->usLocPort,
			pxSocket->u.xTcp.ulRemoteIP,
			pxSocket->u.xTcp.usRemotePort,
			pxSocket ) );

		if( ( pxSocket->u.xTcp.ucTcpState == eCONNECT_SYN ) || ( ulReceiveLength != 0 ) )
		{
			pxTCPHeader->ucTcpFlags = ipTCP_FLAG_ACK;
			xSendLength = ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + xOptionsLength;
			pxTCPHeader->ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );
		}

		/* This was the third step of connecting: SYN, SYN+ACK, ACK	so now the
		connection is established. */
		vTCPStateChange( pxSocket, eESTABLISHED );
	}

	return xSendLength;
}
/*-----------------------------------------------------------*/

/*
 * prvHandleEstablished(): called from prvTCPHandleState()
 *
 * Called if the status is eESTABLISHED.  Data reception has been handled
 * earlier.  Here the ACK's from peer will be checked, and if a FIN is received,
 * the code will check if it may be accepted, i.e. if all expected data has been
 * completely received.
 */
static BaseType_t prvHandleEstablished( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer,
	uint32_t ulReceiveLength, BaseType_t xOptionsLength )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( (*ppxNetworkBuffer)->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &pxTCPPacket->xTCPHeader;
TCPWindow_t *pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;
uint8_t ucTcpFlags = pxTCPHeader->ucTcpFlags;
uint32_t ulSequenceNumber = FreeRTOS_ntohl( pxTCPHeader->ulSequenceNumber ), ulCount;
BaseType_t xSendLength = 0, xMayClose = pdFALSE, bRxComplete, bTxDone;
int32_t lDistance, lSendResult;

	/* Remember the window size the peer is advertising. */
	pxSocket->u.xTcp.wnd = FreeRTOS_ntohs( pxTCPHeader->usWindow );

	if( ( ucTcpFlags & ipTCP_FLAG_ACK ) != 0 )
	{
		ulCount = ulTCPWindowTxAck( pxTcpWindow, FreeRTOS_ntohl( pxTCPPacket->xTCPHeader.ulAckNr ) );

		/* ulTCPWindowTxAck() returns the number of bytes which have been acked,
		starting at 'tx.ulCurrentSequenceNumber'.  Advance the tail pointer in
		txStream. */
		if( ( pxSocket->u.xTcp.txStream != NULL ) && ( ulCount > 0 ) )
		{
			/* Just advancing the tail index, 'ulCount' bytes have been
			confirmed, and because there is new space in the txStream, the
			user/owner should be woken up. */
			/* _HT_ : only in case the socket's waiting? */
			if( lStreamBufferGet( pxSocket->u.xTcp.txStream, 0, NULL, ( int32_t ) ulCount, pdFALSE ) != 0 )
			{
				pxSocket->xEventBits |= eSOCKET_SEND;

				#if ipconfigSUPPORT_SELECT_FUNCTION == 1
				{
					if( ( pxSocket->xSelectBits & eSELECT_WRITE ) != 0 )
					{
						pxSocket->xEventBits |= ( eSELECT_WRITE << SOCKET_EVENT_BIT_COUNT );
					}
				}
				#endif
			}
		}
	}

	/* If this socket has a stream for transmission, add the data to the
	outgoing segment(s). */
	if( pxSocket->u.xTcp.txStream != NULL )
	{
		prvTCPAddTxData( pxSocket );
	}

	pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber = pxTcpWindow->tx.ulCurrentSequenceNumber;

	if( ( pxSocket->u.xTcp.bits.bFinAccepted != 0 ) || ( ( ucTcpFlags & ipTCP_FLAG_FIN ) != 0 ) )
	{
		/* Peer is requesting to stop, see if we're really finished. */
		xMayClose = pdTRUE;

		/* Checks are only necessary if we haven't sent a FIN yet. */
		if( pxSocket->u.xTcp.bits.bFinSent == 0 )
		{
			/* xTCPWindowTxDone returns true when all Tx queues are empty. */
			bRxComplete = xTCPWindowRxEmpty( pxTcpWindow );
			bTxDone     = xTCPWindowTxDone( pxTcpWindow );

			if( ( bRxComplete == 0 ) || ( bTxDone == 0 ) )
			{
				/* Refusing FIN: Rx incomp 1 optlen 4 tx done 1. */
				FreeRTOS_debug_printf( ( "Refusing FIN[%u,%u]: RxCompl %lu tx done %ld\n",
					pxSocket->usLocPort,
					pxSocket->u.xTcp.usRemotePort,
					bRxComplete, bTxDone ) );
				xMayClose = pdFALSE;
			}
			else
			{
				lDistance = ( int32_t ) ( ulSequenceNumber + ulReceiveLength - pxTcpWindow->rx.ulCurrentSequenceNumber );

				if( lDistance > 1 )
				{
					FreeRTOS_debug_printf( ( "Refusing FIN: Rx not complete %ld (cur %lu high %lu)\n",
						lDistance, pxTcpWindow->rx.ulCurrentSequenceNumber - pxTcpWindow->rx.ulFirstSequenceNumber,
						pxTcpWindow->rx.ulHighestSequenceNumber - pxTcpWindow->rx.ulFirstSequenceNumber ) );

					xMayClose = pdFALSE;
				}
			}
		}

		if( xTCPWindowLoggingLevel > 0 )
		{
			FreeRTOS_debug_printf( ( "TCP: FIN received, mayClose = %ld (Rx %lu Len %ld, Tx %lu)\n",
				xMayClose, ulSequenceNumber - pxSocket->u.xTcp.xTcpWindow.rx.ulFirstSequenceNumber, ulReceiveLength,
				pxTcpWindow->tx.ulCurrentSequenceNumber - pxSocket->u.xTcp.xTcpWindow.tx.ulFirstSequenceNumber ) );
		}

		if( xMayClose != pdFALSE )
		{
			pxSocket->u.xTcp.bits.bFinAccepted = pdTRUE;
			xSendLength = prvTCPHandleFin( pxSocket, *ppxNetworkBuffer );
		}
	}

	if( xMayClose == pdFALSE )
	{
		pxTCPHeader->ucTcpFlags = ipTCP_FLAG_ACK;

		if( ulReceiveLength != 0 )
		{
			xSendLength = ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + xOptionsLength;
			/* TCP-offsett equals '( ( length / 4 ) << 4 )', resulting in a shift-left 2 */
			pxTCPHeader->ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );

			if( pxSocket->u.xTcp.bits.bFinSent != 0 )
			{
				pxTcpWindow->tx.ulCurrentSequenceNumber = pxTcpWindow->tx.ulFINSequenceNumber;
			}
		}

		/* Now get data to be transmitted. */
		/* _HT_ patch: since the MTU has be fixed at 1500 in stead of 1526, TCP
		can not	send-out both TCP options and also a full packet. Sending
		options (SACK) is always more urgent than sending data, which can be
		sent later. */
		if( xOptionsLength == 0 )
		{
			/* prvTCPPrepareSend might allocate a bigger network buffer, if
			necessary. */
			lSendResult = prvTCPPrepareSend( pxSocket, ppxNetworkBuffer, xOptionsLength );
			if( lSendResult > 0 )
			{
				xSendLength = lSendResult;
			}
		}
	}

	return xSendLength;
}
/*-----------------------------------------------------------*/

/*
 * Called from prvTCPHandleState().  There is data to be sent.  If
 * ipconfigUSE_TCP_WIN is defined, and if only an ACK must be sent, it will be
 * checked if it would better be postponed for efficiency.
 */
static BaseType_t prvSendData( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer,
	uint32_t ulReceiveLength, BaseType_t xSendLength )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( (*ppxNetworkBuffer)->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &pxTCPPacket->xTCPHeader;
TCPWindow_t *pxTcpWindow = &pxSocket->u.xTcp.xTcpWindow;
/* Find out what window size we may advertised. */
uint32_t ulFrontSpace;
int32_t lRxSpace;

	pxSocket->u.xTcp.ulRxCurWinSize = pxTcpWindow->xSize.ulRxWindowLength -
									 ( pxTcpWindow->rx.ulHighestSequenceNumber - pxTcpWindow->rx.ulCurrentSequenceNumber );

	/* Free space in rxStream. */
	if( pxSocket->u.xTcp.rxStream != NULL )
	{
		ulFrontSpace = ( uint32_t ) lStreamBufferFrontSpace( pxSocket->u.xTcp.rxStream );
	}
	else
	{
		ulFrontSpace = ( uint32_t ) pxSocket->u.xTcp.rxStreamSize;
	}

	pxSocket->u.xTcp.ulRxCurWinSize = FreeRTOS_min_uint32( ulFrontSpace, pxSocket->u.xTcp.ulRxCurWinSize );

	/* Set the time-out field, so that we'll be called by the IP-task in case no
	next message will be received. */
	lRxSpace = (int32_t)( pxSocket->u.xTcp.ulHighestRxAllowed - pxTcpWindow->rx.ulCurrentSequenceNumber );

	#if ipconfigUSE_TCP_WIN == 1
	{
		/* In case we're receiving data continuously, we might postpone sending
		an ACK to gain performance. */
		if( ( ulReceiveLength > 0 ) &&							/* Data was sent to this socket. */
			( lRxSpace > 0 ) &&									/* There is Rx space for more data. */
			( pxSocket->u.xTcp.bits.bFinSent == pdFALSE ) &&	/* Not in a closure phase. */
			( xSendLength == ( ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER ) ) && /* No Tx data or options to be sent. */
			( pxSocket->u.xTcp.ucTcpState == eESTABLISHED ) &&	/* Connection established. */
			( pxTCPHeader->ucTcpFlags == ipTCP_FLAG_ACK ) )		/* There are no other flags than an ACK. */
		{
			if( pxSocket->u.xTcp.pxAckMessage != *ppxNetworkBuffer )
			{
				/* There was still a delayed in queue, delete it. */
				if( pxSocket->u.xTcp.pxAckMessage != 0 )
				{
					vReleaseNetworkBufferAndDescriptor( pxSocket->u.xTcp.pxAckMessage );
				}

				pxSocket->u.xTcp.pxAckMessage = *ppxNetworkBuffer;
			}
			if( ( ulReceiveLength < ( uint32_t ) pxSocket->u.xTcp.usCurMSS ) ||	/* Received a small message. */
				( lRxSpace < ( int32_t ) ( 2U * pxSocket->u.xTcp.usCurMSS ) ) )	/* There are less than 2 x MSS space in the Rx buffer. */
			{
				pxSocket->u.xTcp.usTimeout = ( uint16_t ) pdMS_TO_TICKS( DELAYED_ACK_SHORT_DELAY_MS );
			}
			else
			{
				/* Normally a delayed ACK should wait 200 ms for a next incoming
				packet.  Only wait 20 ms here to gain performance.  A slow ACK
				for full-size message. */
				pxSocket->u.xTcp.usTimeout = ( uint16_t ) pdMS_TO_TICKS( DELAYED_ACK_LONGER_DELAY_MS );
			}

			if( ( xTCPWindowLoggingLevel > 1 ) && ( ipconfigTCP_MAY_LOG_PORT( pxSocket->usLocPort ) != pdFALSE ) )
			{
				FreeRTOS_debug_printf( ( "Send[%u->%u] del ACK %lu SEQ %lu (len %lu) tmout %u d %lu\n",
					pxSocket->usLocPort,
					pxSocket->u.xTcp.usRemotePort,
					pxTcpWindow->rx.ulCurrentSequenceNumber - pxTcpWindow->rx.ulFirstSequenceNumber,
					pxSocket->u.xTcp.xTcpWindow.ulOurSequenceNumber - pxTcpWindow->tx.ulFirstSequenceNumber,
					xSendLength,
					pxSocket->u.xTcp.usTimeout, lRxSpace ) );
			}

			*ppxNetworkBuffer = NULL;
			xSendLength = 0;
		}
		else if( pxSocket->u.xTcp.pxAckMessage != NULL )
		{
			/* As an ACK is not being delayed, remove any earlier delayed ACK
			message. */
			if( pxSocket->u.xTcp.pxAckMessage != *ppxNetworkBuffer )
			{
				vReleaseNetworkBufferAndDescriptor( pxSocket->u.xTcp.pxAckMessage );
			}

			pxSocket->u.xTcp.pxAckMessage = NULL;
		}
	}
	#else
	{
		/* Remove compiler warnings. */
		( void ) ulReceiveLength;
		( void ) pxTCPHeader;
		( void ) lRxSpace;
	}
	#endif /* ipconfigUSE_TCP_WIN */

	if( xSendLength != 0 )
	{
		if( ( xTCPWindowLoggingLevel > 1 ) && ( ipconfigTCP_MAY_LOG_PORT( pxSocket->usLocPort ) != pdFALSE ) )
		{
			FreeRTOS_debug_printf( ( "Send[%u->%u] imm ACK %lu SEQ %lu (len %lu)\n",
				pxSocket->usLocPort,
				pxSocket->u.xTcp.usRemotePort,
				pxTcpWindow->rx.ulCurrentSequenceNumber - pxTcpWindow->rx.ulFirstSequenceNumber,
				pxTcpWindow->ulOurSequenceNumber - pxTcpWindow->tx.ulFirstSequenceNumber,
				xSendLength ) );
		}

		prvTCPReturnPacket( pxSocket, *ppxNetworkBuffer, ( uint32_t ) xSendLength, pdFALSE );
	}

	return xSendLength;
}
/*-----------------------------------------------------------*/

/*
 * prvTCPHandleState()
 * is the most important function of this TCP stack
 * We've tried to keep it (relatively short) by putting a lot of code in
 * the static functions above:
 *
 *		prvCheckRxData()
 *		prvStoreRxData()
 *		prvSetOptions()
 *		prvHandleSynReceived()
 *		prvHandleEstablished()
 *		prvSendData()
 *
 * As these functions are declared static, and they're called from one location
 * only, most compilers will inline them, thus avoiding a call and return.
 */
static BaseType_t prvTCPHandleState( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t **ppxNetworkBuffer )
{
xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( (*ppxNetworkBuffer)->pucEthernetBuffer );
xTCPHeader_t *pxTCPHeader = &( pxTCPPacket->xTCPHeader );
BaseType_t xSendLength = 0;
uint32_t ulReceiveLength;	/* Number of bytes contained in the TCP message. */
uint8_t *pucRecvData;
uint32_t ulSequenceNumber = FreeRTOS_ntohl (pxTCPHeader->ulSequenceNumber);

	/* xOptionsLength: the size of the options to be sent (always a multiple of
	4 bytes)
	1. in the SYN phase, we shall communicate the MSS
	2. in case of a SACK, Selective ACK, ack a segment which comes in
	out-of-order. */
BaseType_t xOptionsLength = 0;
uint8_t ucTcpFlags = pxTCPHeader->ucTcpFlags;
TCPWindow_t *pxTcpWindow = &( pxSocket->u.xTcp.xTcpWindow );

	/* First get the length and the position of the received data, if any.
	pucRecvData will point to the first byte of the TCP payload. */
	ulReceiveLength = ( uint32_t ) prvCheckRxData( *ppxNetworkBuffer, &pucRecvData );

	if( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED )
	{
		if ( pxTcpWindow->rx.ulCurrentSequenceNumber == ulSequenceNumber + 1 )
		{
			/* This is most probably a keep-alive message from peer.  Setting
			'bWinChange' doesn't cause a window-size-change, the flag is used
			here to force sending an immediate ACK. */
			pxSocket->u.xTcp.bits.bWinChange = 1;
		}
	}

	/* Keep track of the highest sequence number that might be expected within
	this connection. */
	if( ( ( int32_t ) ( ulSequenceNumber + ulReceiveLength - pxTcpWindow->rx.ulHighestSequenceNumber ) ) > 0 )
	{
		pxTcpWindow->rx.ulHighestSequenceNumber = ulSequenceNumber + ulReceiveLength;
	}

	/* Storing data may result in a fatal error if malloc() fails. */
	if( prvStoreRxData( pxSocket, pucRecvData, *ppxNetworkBuffer, ulReceiveLength ) < 0 )
	{
		xSendLength = -1;
	}
	else
	{
		xOptionsLength = prvSetOptions( pxSocket, *ppxNetworkBuffer );

		if( ( pxSocket->u.xTcp.ucTcpState == eSYN_RECEIVED ) && ( ( ucTcpFlags & ipTCP_FLAG_CTRL ) == ipTCP_FLAG_SYN ) )
		{
			FreeRTOS_debug_printf( ( "eSYN_RECEIVED: ACK expected, not SYN: peer missed our SYN+ACK\n" ) );

			/* In eSYN_RECEIVED a simple ACK is expected, but apparently the
			'SYN+ACK' didn't arrive.  Step back to the previous state in which
			a first incoming SYN is handled.  The SYN was counted already so
			decrease it first. */
			vTCPStateChange( pxSocket, eSYN_FIRST );
		}

		if( ( ( ucTcpFlags & ipTCP_FLAG_FIN ) != 0 ) && ( pxSocket->u.xTcp.bits.bFinRecv == 0 ) )
		{
			/* It's the first time a FIN has been received, remember its
			sequence number. */
			pxTcpWindow->rx.ulFINSequenceNumber = ulSequenceNumber + ulReceiveLength;
			pxSocket->u.xTcp.bits.bFinRecv = pdTRUE;

			/* Was peer the first one to send a FIN? */
			if( pxSocket->u.xTcp.bits.bFinSent == pdFALSE )
			{
				/* If so, don't send the-last-ACK. */
				pxSocket->u.xTcp.bits.bFinLast = pdTRUE;
			}
		}

		switch (pxSocket->u.xTcp.ucTcpState)
		{
		case eCLOSED:		/* (server + client) no connection state at all. */
			/* Nothing to do for a closed socket, except waiting for the
			owner. */
			break;

		case eTCP_LISTEN:	/* (server) waiting for a connection request from
							any remote TCP and port. */
			/* The listen state was handled in xProcessReceivedTCPPacket().
			Should not come here. */
			break;

		case eSYN_FIRST:	/* (server) Just received a SYN request for a server
							socket. */
			{
				/* A new socket has been created, reply with a SYN+ACK.
				Acknowledge with seq+1 because the SYN is seen as pseudo data
				with len = 1. */
				xOptionsLength = prvSetSynAckOptions (pxSocket, pxTCPPacket);
				pxTCPHeader->ucTcpFlags = ipTCP_FLAG_SYN | ipTCP_FLAG_ACK;

				xSendLength = ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + xOptionsLength;

				/* Set the TCP offset field:  ipSIZE_OF_TCP_HEADER equals 20 and
				xOptionsLength is a multiple of 4.  The complete expression is:
				ucTcpOffset = ( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) / 4 ) << 4 */
				pxTCPHeader->ucTcpOffset = ( uint8_t )( ( ipSIZE_OF_TCP_HEADER + xOptionsLength ) << 2 );
				vTCPStateChange( pxSocket, eSYN_RECEIVED );

				pxTcpWindow->rx.ulCurrentSequenceNumber = pxTcpWindow->rx.ulHighestSequenceNumber = ulSequenceNumber + 1;
				pxTcpWindow->tx.ulCurrentSequenceNumber = pxTcpWindow->ulNextTxSequenceNumber = pxTcpWindow->tx.ulFirstSequenceNumber + 1; /* because we send a TCP_SYN. */
			}
			break;

		case eCONNECT_SYN:	/* (client) also called SYN_SENT: we've just send a
							SYN, expect	a SYN+ACK and send a ACK now. */
			/* Fall through */
		case eSYN_RECEIVED:	/* (server) we've had a SYN, replied with SYN+SCK
							expect a ACK and do nothing. */
			xSendLength = prvHandleSynReceived( pxSocket, ppxNetworkBuffer, ulReceiveLength, xOptionsLength );
			break;

		case eESTABLISHED:	/* (server + client) an open connection, data
							received can be	delivered to the user. The normal
							state for the data transfer phase of the connection
							The closing states are also handled here with the
							use of some flags. */
			xSendLength = prvHandleEstablished( pxSocket, ppxNetworkBuffer, ulReceiveLength, xOptionsLength );
			break;

		case eLAST_ACK:		/* (server + client) waiting for an acknowledgement
							of the connection termination request previously
							sent to the remote TCP (which includes an
							acknowledgement of its connection termination
							request). */
			/* Fall through */
		case eFIN_WAIT_1:	/* (server + client) waiting for a connection termination request from the remote TCP,
							 * or an acknowledgement of the connection termination request previously sent. */
			/* Fall through */
		case eFIN_WAIT_2:	/* (server + client) waiting for a connection termination request from the remote TCP. */
			xSendLength = prvTCPHandleFin( pxSocket, *ppxNetworkBuffer );
			break;

		case eCLOSE_WAIT:	/* (server + client) waiting for a connection
							termination request from the local user.  Nothing to
							do, connection is closed, wait for owner to close
							this socket. */
			break;

		case eCLOSING:		/* (server + client) waiting for a connection
							termination request acknowledgement from the remote
							TCP. */
			break;

		case eTIME_WAIT:	/* (either server or client) waiting for enough time
							to pass to be sure the remote TCP received the
							acknowledgement of its connection termination
							request. [According to RFC 793 a connection can stay
							in TIME-WAIT for a maximum of four minutes known as
							a MSL (maximum segment lifetime).]  These states are
							implemented implicitly by settings flags like
							'bFinSent', 'bFinRecv', and 'bFinAcked'. */
			break;
		}
	}

	if( xSendLength > 0 )
	{
		xSendLength = prvSendData( pxSocket, ppxNetworkBuffer, ulReceiveLength, xSendLength );
	}

	return xSendLength;
}
/*-----------------------------------------------------------*/

static BaseType_t prvTCPSendReset( xNetworkBufferDescriptor_t *pxNetworkBuffer )
{
#if( ipconfigIGNORE_UNKNOWN_PACKETS == 1 )
	/* Configured to ignore unknown packets just suppress a compiler warning. */
	( void ) pxNetworkBuffer;
#else

xTCPPacket_t *pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
const BaseType_t xSendLength = ipSIZE_OF_IP_HEADER + ipSIZE_OF_TCP_HEADER + 0;	/* plus 0 options. */

	pxTCPPacket->xTCPHeader.ucTcpFlags = ipTCP_FLAG_ACK | ipTCP_FLAG_RST;
	pxTCPPacket->xTCPHeader.ucTcpOffset = ( ipSIZE_OF_TCP_HEADER + 0 ) << 2;

	prvTCPReturnPacket( NULL, pxNetworkBuffer, ( uint32_t ) xSendLength, pdFALSE );

#endif /* !ipconfigIGNORE_UNKNOWN_PACKETS */

	/* The packet was not consumed. */
	return pdFAIL;
}
/*-----------------------------------------------------------*/

static void prvSocketSetMSS( xFreeRTOS_Socket_t *pxSocket )
{
uint32_t ulMSS = ipconfigTCP_MSS;

	if( ( ( FreeRTOS_ntohl( pxSocket->u.xTcp.ulRemoteIP ) ^ *ipLOCAL_IP_ADDRESS_POINTER ) & xNetworkAddressing.ulNetMask ) != 0ul )
	{
		/* Data for this peer will pass through a router, and maybe through
		the internet.  Limit the MSS to 1400 bytes or less. */
		ulMSS = FreeRTOS_min_uint32( REDUCED_MSS_THROUGH_INTERNET, ulMSS );
	}

	FreeRTOS_debug_printf( ( "prvSocketSetMSS: %lu bytes for %lxip:%u\n", ulMSS, pxSocket->u.xTcp.ulRemoteIP, pxSocket->u.xTcp.usRemotePort ) );

	pxSocket->u.xTcp.usInitMSS = pxSocket->u.xTcp.usCurMSS = ( uint16_t ) ulMSS;
}
/*-----------------------------------------------------------*/

/*
 *	FreeRTOS_TCP_IP has only 2 public functions, this is the second one:
 *	xProcessReceivedTCPPacket()
 *		prvTCPHandleState()
 *			prvTCPPrepareSend()
 *				prvTCPReturnPacket()
 *				xNetworkInterfaceOutput()	// Sends data to the NIC
 *		prvTCPSendRepeated()
 *			prvTCPReturnPacket()		// Prepare for returning
 *			xNetworkInterfaceOutput()	// Sends data to the NIC
*/
BaseType_t xProcessReceivedTCPPacket( xNetworkBufferDescriptor_t *pxNetworkBuffer )
{
xFreeRTOS_Socket_t *pxSocket;
xTCPPacket_t * pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
uint16_t ucTcpFlags = pxTCPPacket->xTCPHeader.ucTcpFlags;
uint32_t ulLocalIP = FreeRTOS_htonl( pxTCPPacket->xIPHeader.ulDestinationIPAddress );
uint16_t xLocalPort = FreeRTOS_htons( pxTCPPacket->xTCPHeader.usDestinationPort );
uint32_t ulRemoteIP = FreeRTOS_htonl( pxTCPPacket->xIPHeader.ulSourceIPAddress );
uint16_t xRemotePort = FreeRTOS_htons( pxTCPPacket->xTCPHeader.usSourcePort );
BaseType_t xResult = pdPASS;

	/* Find the destination socket, and if not found: return a socket listing to
	the destination PORT. */
	pxSocket = ( xFreeRTOS_Socket_t * ) pxTCPSocketLookup( ulLocalIP, xLocalPort, ulRemoteIP, xRemotePort );

	if( ( pxSocket == NULL ) || ( prvTCPSocketIsActive( pxSocket->u.xTcp.ucTcpState ) == pdFALSE ) )
	{
		/* A TCP messages is received but either there is no socket with the
		given port number or the there is a socket, but it is in one of these
		non-active states:  eCLOSED, eCLOSE_WAIT, eFIN_WAIT_2, eCLOSING, or
		eTIME_WAIT. */

		FreeRTOS_debug_printf( ( "TCP: No active socket on port %d (%lxip:%d)\n", xLocalPort, ulRemoteIP, xRemotePort ) );

		/* Never answer a RST with a RST. */
		if( ( ucTcpFlags & ipTCP_FLAG_RST ) == 0 )
		{
			prvTCPSendReset( pxNetworkBuffer );
		}

		/* The packet can't be handled. */
		xResult = pdFAIL;
	}
	else
	{
		pxSocket->u.xTcp.ucRepCount = 0;

		if( pxSocket->u.xTcp.ucTcpState == eTCP_LISTEN )
		{
			/* The matching socket is in a listening state.  Test if the peer
			has set the SYN flag. */
			if( ( ucTcpFlags & ipTCP_FLAG_CTRL ) != ipTCP_FLAG_SYN )
			{
				/* What happens: maybe after a reboot, a client doesn't know the
				connection had gone.  Send a RST in order to get a new connect
				request. */
				#if( ipconfigHAS_DEBUG_PRINTF == 1 )
				{
				FreeRTOS_debug_printf( ( "TCP: Server can't handle flags: %s from %lxip:%u to port %u\n",
					prvTCPFlagMeaning( ( UBaseType_t ) ucTcpFlags ), ulRemoteIP, xRemotePort, xLocalPort ) );
				}
				#endif /* ipconfigHAS_DEBUG_PRINTF */

				if( ( ucTcpFlags & ipTCP_FLAG_RST ) == 0 )
				{
					prvTCPSendReset( pxNetworkBuffer );
				}
				xResult = pdFAIL;
			}
			else
			{
				/* prvHandleListen() will either return a newly created socket
				(if bReuseSocket is false), otherwise it returns the current
				socket which will later get connected. */
				pxSocket = prvHandleListen( pxSocket, pxNetworkBuffer );

				if( pxSocket == NULL )
				{
					xResult = pdFAIL;
				}
			}
		}	/* if( pxSocket->u.xTcp.ucTcpState == eTCP_LISTEN ). */
		else
		{
			/* This is not a socket in listening mode. Check for the RST
			flag. */
			if( ( ucTcpFlags & ipTCP_FLAG_RST ) != 0 )
			{
				/* The target socket is not in a listening state, any RST packet
				will cause the socket to be closed. */
				FreeRTOS_debug_printf( ( "TCP: RST received from %lxip:%u for %u\n", ulRemoteIP, xRemotePort, xLocalPort ) );
				vTCPStateChange( pxSocket, eCLOSED );

				/* The packet cannot be handled. */
				xResult = pdFAIL;
			}
			else if( ( ( ucTcpFlags & ipTCP_FLAG_CTRL ) == ipTCP_FLAG_SYN ) && ( pxSocket->u.xTcp.ucTcpState >= eESTABLISHED ) )
			{
				/* SYN flag while this socket is already connected. */
				FreeRTOS_debug_printf( ( "TCP: SYN unexpected from %lxip:%u\n", ulRemoteIP, xRemotePort ) );

				/* The packet cannot be handled. */
				xResult = pdFAIL;
			}
			else
			{
				/* Update the copy of the TCP header only (skipping eth and IP
				headers).  It might be used later on, whenever data must be sent
				to the peer. */
				const BaseType_t lOffset = ipSIZE_OF_ETH_HEADER + ipSIZE_OF_IP_HEADER;
				memcpy( pxSocket->u.xTcp.lastPacket + lOffset, pxNetworkBuffer->pucEthernetBuffer + lOffset, ipSIZE_OF_TCP_HEADER );
			}
		}
	}

	if( xResult != pdFAIL )
	{
		/* Touch the alive timers because we received a message	for this
		socket. */
		prvTCPTouchSocket( pxSocket );

		/* Parse the TCP option(s), if present. */
		/* _HT_ : if we're in the SYN phase, and peer does not send a MSS option,
		then we MUST assume an MSS size of 536 bytes for backward compatibility. */

		/* When there are no TCP options, the TCP offset equals 20 bytes, which is stored as
		the number 5 (words) in the higher niblle of the TCP-offset byte. */
		if( ( pxTCPPacket->xTCPHeader.ucTcpOffset & TCP_OFFSET_LENGTH_BITS ) > TCP_OFFSET_STANDARD_LENGTH )
		{
			prvCheckOptions( pxSocket, pxNetworkBuffer );
		}


		#if( ipconfigUSE_TCP_WIN == 1 )
		{
			pxSocket->u.xTcp.wnd = FreeRTOS_ntohs( pxTCPPacket->xTCPHeader.usWindow );
		}
		#endif

		/* In prvTCPHandleState() the incoming messages will be handled
		depending on the current state of the connection. */
		if( prvTCPHandleState( pxSocket, &pxNetworkBuffer ) > 0 )
		{
			/* prvTCPHandleState() has sent a message, see if there are more to
			be transmitted. */
			#if( ipconfigUSE_TCP_WIN == 1 )
			{
				prvTCPSendRepeated( pxSocket, &pxNetworkBuffer );
			}
			#endif /* ipconfigUSE_TCP_WIN */
		}

		if( pxNetworkBuffer != NULL )
		{
			/* We must check if the buffer is unequal to NULL, because the
			socket might keep a reference to it in case a delayed ACK must be
			sent. */
			vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
			pxNetworkBuffer = NULL;
		}

		/* And finally, calculate when this socket wants to be woken up. */
		prvTCPNextTimeout ( pxSocket );
		/* Return pdPASS to tell that the network buffer is 'consumed'. */
		xResult = pdPASS;
	}

	/* pdPASS being returned means the buffer has been consumed. */
	return xResult;
}
/*-----------------------------------------------------------*/

static xFreeRTOS_Socket_t *prvHandleListen( xFreeRTOS_Socket_t *pxSocket, xNetworkBufferDescriptor_t *pxNetworkBuffer )
{
xTCPPacket_t * pxTCPPacket = ( xTCPPacket_t * ) ( pxNetworkBuffer->pucEthernetBuffer );
xFreeRTOS_Socket_t *pxReturn;

	/* A pure SYN (without ACK) has come in, create a new socket to answer
	it. */
	if( pxSocket->u.xTcp.bits.bReuseSocket != pdFALSE )
	{
		/* The flag bReuseSocket indicates that the same instance of the
		listening socket should be used for the connection. */
		pxReturn = pxSocket;
		pxSocket->u.xTcp.bits.bPassQueued = pdTRUE;
		pxSocket->u.xTcp.pxPeerSocket = pxSocket;
	}
	else
	{
		/* The socket does not have the bReuseSocket flag set meaning create a
		new socket when a connection comes in. */
		pxReturn = NULL;

		if( pxSocket->u.xTcp.usChildCount >= pxSocket->u.xTcp.usBacklog )
		{
			FreeRTOS_printf( ( "Check: Socket %u already has %u / %u child%s\n",
				pxSocket->usLocPort,
				pxSocket->u.xTcp.usChildCount,
				pxSocket->u.xTcp.usBacklog,
				pxSocket->u.xTcp.usChildCount == 1 ? "" : "ren" ) );
			prvTCPSendReset( pxNetworkBuffer );
		}
		else
		{
			xFreeRTOS_Socket_t *pxNewSocket = (xFreeRTOS_Socket_t *)
				FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP );

			if( ( pxNewSocket == NULL ) || ( pxNewSocket == FREERTOS_INVALID_SOCKET ) )
			{
				FreeRTOS_debug_printf( ( "TCP: Listen: new socket failed\n" ) );
				prvTCPSendReset( pxNetworkBuffer );
			}
			else if( prvTCPSocketCopy( pxNewSocket, pxSocket ) != pdFALSE )
			{
				/* The socket will be connected immediately, no time for the
				owner to setsockopt's, therefore copy properties of the server
				socket to the new socket.  Only the binding might fail (due to
				lack of resources). */
				pxReturn = pxNewSocket;
			}
		}
	}

	if( pxReturn != NULL )
	{
		pxReturn->u.xTcp.usRemotePort = FreeRTOS_htons( pxTCPPacket->xTCPHeader.usSourcePort );
		pxReturn->u.xTcp.ulRemoteIP = FreeRTOS_htonl( pxTCPPacket->xIPHeader.ulSourceIPAddress );
		pxReturn->u.xTcp.xTcpWindow.ulOurSequenceNumber = ulNextInitialSequenceNumber;

		/* Here is the SYN action. */
		pxReturn->u.xTcp.xTcpWindow.rx.ulCurrentSequenceNumber = FreeRTOS_ntohl( pxTCPPacket->xTCPHeader.ulSequenceNumber );
		prvSocketSetMSS( pxReturn );

		prvTCPCreateWindow( pxReturn );

		/* It is recommended to increase the ISS for each new connection with a value of 0x102. */
		ulNextInitialSequenceNumber += INITIAL_SEQUENCE_NUMBER_INCREMENT;

		vTCPStateChange( pxReturn, eSYN_FIRST );

		/* Make a copy of the header up to the TCP header.  It is needed later
		on, whenever data must be sent to the peer. */
		memcpy( pxReturn->u.xTcp.lastPacket, pxNetworkBuffer->pucEthernetBuffer, sizeof( pxReturn->u.xTcp.lastPacket ) );
	}
	return pxReturn;
}
/*-----------------------------------------------------------*/

/*
 * Duplicates a socket after a listening socket receives a connection.
 */
static BaseType_t prvTCPSocketCopy( xFreeRTOS_Socket_t *pxNewSocket, xFreeRTOS_Socket_t *pxSocket )
{
struct freertos_sockaddr xAddress;

	pxNewSocket->xReceiveBlockTime = pxSocket->xReceiveBlockTime;
	pxNewSocket->xSendBlockTime = pxSocket->xSendBlockTime;
	pxNewSocket->ucSocketOptions = pxSocket->ucSocketOptions;
	pxNewSocket->u.xTcp.rxStreamSize = pxSocket->u.xTcp.rxStreamSize;
	pxNewSocket->u.xTcp.txStreamSize = pxSocket->u.xTcp.txStreamSize;
	pxNewSocket->u.xTcp.lLittleSpace = pxSocket->u.xTcp.lLittleSpace ;
	pxNewSocket->u.xTcp.lEnoughSpace = pxSocket->u.xTcp.lEnoughSpace;
	pxNewSocket->u.xTcp.ulRxWinSize  = pxSocket->u.xTcp.ulRxWinSize;
	pxNewSocket->u.xTcp.ulTxWinSize  = pxSocket->u.xTcp.ulTxWinSize;

	#if( ipconfigSOCKET_HAS_USER_SEMAPHORE == 1 )
	{
		pxNewSocket->pxUserSemaphore = pxSocket->pxUserSemaphore;
	}
	#endif /* ipconfigSOCKET_HAS_USER_SEMAPHORE */

	#if( ipconfigUSE_CALLBACKS == 1 )
	{
		/* In case call-backs are used, copy them from parent to child. */
		pxNewSocket->u.xTcp.pHndConnected = pxSocket->u.xTcp.pHndConnected;
		pxNewSocket->u.xTcp.pHndReceive = pxSocket->u.xTcp.pHndReceive;
		pxNewSocket->u.xTcp.pHndSent = pxSocket->u.xTcp.pHndSent;
	}
	#endif /* ipconfigUSE_CALLBACKS */

	#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
	{
		/* Child socket of listening sockets will inherit the Socket Set
		Otherwise the owner has no chance of including it into the set. */
		if( pxSocket->pxSocketSet )
		{
			pxNewSocket->pxSocketSet = pxSocket->pxSocketSet;
			pxNewSocket->xSelectBits = pxSocket->xSelectBits | eSELECT_READ | eSELECT_EXCEPT;
		}
	}
	#endif /* ipconfigSUPPORT_SELECT_FUNCTION */

	/* And bind it to the same local port as its parent. */
	xAddress.sin_addr = *ipLOCAL_IP_ADDRESS_POINTER;
	xAddress.sin_port = FreeRTOS_htons( pxSocket->usLocPort );

	#if( ipconfigTCP_HANG_PROTECTION == 1 )
	{
		/* Only when there is anti-hanging protection, a socket may become an
		orphan temporarily.  Once this socket is really connected, the owner of
		the server socket will be notified. */

		/* When bPassQueued is true, the socket is an orphan until it gets
		connected. */
		pxNewSocket->u.xTcp.bits.bPassQueued = pdTRUE;
		pxNewSocket->u.xTcp.pxPeerSocket = pxSocket;
	}
	#else
	{
		/* A reference to the new socket may be stored and the socket is marked
		as 'passable'. */

		/* When bPassAccept is pdTURE this socket may be returned in a call to
		accept(). */
		pxNewSocket->u.xTcp.bits.bPassAccept = pdTRUE;
		if(pxSocket->u.xTcp.pxPeerSocket == NULL )
		{
			pxSocket->u.xTcp.pxPeerSocket = pxNewSocket;
		}
	}
	#endif

	pxSocket->u.xTcp.usChildCount++;

	FreeRTOS_debug_printf( ( "Gain: Socket %u now has %u / %u child%s\n",
		pxSocket->usLocPort,
		pxSocket->u.xTcp.usChildCount,
		pxSocket->u.xTcp.usBacklog,
		pxSocket->u.xTcp.usChildCount == 1 ? "" : "ren" ) );

	/* Now bind the child socket to the same port as the listening socket. */
	if( vSocketBind ( pxNewSocket, &xAddress, sizeof( xAddress ), pdTRUE ) != 0 )
	{
		FreeRTOS_debug_printf( ( "TCP: Listen: new socket bind error\n" ) );
		vSocketClose( pxNewSocket );
		return pdFALSE;
	}

	return pdTRUE;
}
/*-----------------------------------------------------------*/

#if( ( ipconfigHAS_DEBUG_PRINTF != 0 ) || ( ipconfigHAS_PRINTF != 0 ) )

	const char *FreeRTOS_GetTCPStateName( UBaseType_t ulState )
	{
		if( ulState >= ARRAY_SIZE( pcStateNames ) )
		{
			ulState = ARRAY_SIZE( pcStateNames ) - 1;
		}
		return pcStateNames[ ulState ];
	}

#endif /* ( ( ipconfigHAS_DEBUG_PRINTF != 0 ) || ( ipconfigHAS_PRINTF != 0 ) ) */
/*-----------------------------------------------------------*/

/*
 * In the API accept(), the user asks is there is a new client?  As API's can
 * not walk through the xBoundTcpSocketsList the IP-task will do this.
 */
BaseType_t xTCPCheckNewClient( xFreeRTOS_Socket_t *pxSocket )
{
TickType_t xLocalPort = FreeRTOS_htons( pxSocket->usLocPort );
ListItem_t *pxIterator;
xFreeRTOS_Socket_t *pxFound;
BaseType_t xResult = pdFALSE;

	/* Here xBoundTcpSocketsList can be accessed safely IP-task is the only one
	who has access. */
	for( pxIterator = ( ListItem_t * ) listGET_HEAD_ENTRY( &xBoundTcpSocketsList );
		pxIterator != ( ListItem_t * ) listGET_END_MARKER( &xBoundTcpSocketsList );
		pxIterator = ( ListItem_t * ) listGET_NEXT( pxIterator ) )
	{
		if( listGET_LIST_ITEM_VALUE( pxIterator ) == xLocalPort )
		{
			pxFound = ( xFreeRTOS_Socket_t * ) listGET_LIST_ITEM_OWNER( pxIterator );
			if( pxFound->ucProtocol == FREERTOS_IPPROTO_TCP && pxFound->u.xTcp.bits.bPassAccept )
			{
				pxSocket->u.xTcp.pxPeerSocket = pxFound;
				FreeRTOS_debug_printf( ( "xTCPCheckNewClient[0]: client on port %u\n", pxSocket->usLocPort ) );
				xResult = pdTRUE;
				break;
			}
		}
	}
	return xResult;
}
/*-----------------------------------------------------------*/

#endif /* ipconfigUSE_TCP == 1 */

