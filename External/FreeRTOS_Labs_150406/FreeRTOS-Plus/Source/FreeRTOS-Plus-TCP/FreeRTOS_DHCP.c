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

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_TCP_IP.h"
#include "FreeRTOS_DHCP.h"
#include "FreeRTOS_ARP.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

/* Exclude the entire file if DHCP is not enabled. */
#if( ipconfigUSE_DHCP != 0 )

#if ( ipconfigUSE_DHCP != 0 ) && ( ipconfigNETWORK_MTU < 586 )
	/* DHCP must be able to receive an options field of 312 bytes, the fixed
	part of the DHCP packet is 240 bytes, and the IP/UDP headers take 28 bytes. */
	#error ipconfigNETWORK_MTU needs to be at least 586 to use DHCP
#endif

/* Parameter widths in the DHCP packet. */
#define dhcpCLIENT_HARDWARE_ADDRESS_LENGTH		16
#define dhcpSERVER_HOST_NAME_LENGTH				64
#define dhcpBOOT_FILE_NAME_LENGTH 				128

/* Timer parameters */
#ifndef dhcpINITIAL_DHCP_TX_PERIOD
	#define dhcpINITIAL_TIMER_PERIOD			( 250 / portTICK_PERIOD_MS )
	#define dhcpINITIAL_DHCP_TX_PERIOD			( 5000 / portTICK_PERIOD_MS )
	#define dhcpMAX_TIME_TO_WAIT_FOR_ACK		( 5000 / portTICK_PERIOD_MS )
#endif

/* Codes of interest found in the DHCP options field. */
#define dhcpSUBNET_MASK_OPTION_CODE				( 1 )
#define dhcpGATEWAY_OPTION_CODE					( 3 )
#define dhcpDNS_SERVER_OPTIONS_CODE				( 6 )
#define dhcpDNS_HOSTNAME_OPTIONS_CODE			( 12 )
#define dhcpMESSAGE_TYPE_OPTION_CODE			( 53 )
#define dhcpLEASE_TIME_OPTION_CODE				( 51 )
#define dhcpCLIENT_IDENTIFIER_OPTION_CODE		( 61 )
#define dhcpPARAMETER_REQUEST_OPTION_CODE		( 55 )
#define dhcpREQUEST_IP_ADDRESS_OPTION_CODE		( 50 )
#define dhcpSERVER_IP_ADDRESS_OPTION_CODE		( 54 )

/* The four DHCP message types of interest. */
#define dhcpMESSAGE_TYPE_DISCOVER				( 1 )
#define dhcpMESSAGE_TYPE_OFFER					( 2 )
#define dhcpMESSAGE_TYPE_REQUEST				( 3 )
#define dhcpMESSAGE_TYPE_ACK					( 5 )
#define dhcpMESSAGE_TYPE_NACK					( 6 )

/* Offsets into the transmitted DHCP options fields at which various parameters
are located. */
#define dhcpCLIENT_IDENTIFIER_OFFSET			( 5 )
#define dhcpREQUESTED_IP_ADDRESS_OFFSET			( 13 )
#define dhcpDHCP_SERVER_IP_ADDRESS_OFFSET		( 19 )

/* Values used in the DHCP packets. */
#define dhcpREQUEST_OPCODE						( 1 )
#define dhcpREPLY_OPCODE						( 2 )
#define dhcpADDRESS_TYPE_ETHERNET				( 1 )
#define dhcpETHERNET_ADDRESS_LENGTH				( 6 )

/* If a lease time is not received, use the default of two days. */
#define dhcpDEFAULT_LEASE_TIME					( ( 48UL * 60UL * 60UL * 1000UL ) / portTICK_PERIOD_MS ) /* 48 hours in ticks. */

/* Don't allow the lease time to be too short. */
#define dhcpMINIMUM_LEASE_TIME					( 60000UL / portTICK_PERIOD_MS )	/* 60 seconds in ticks. */

/* Marks the end of the variable length options field in the DHCP packet. */
#define dhcpOPTION_END_BYTE 0xff

/* Offset into a DHCP message at which the first byte of the options is
located. */
#define dhcpFIRST_OPTION_BYTE_OFFSET			( 0xf0 )

/* When walking the variable length options field, the following value is used
to ensure the walk has not gone past the end of the valid options.  2 bytes is
made up of the length byte, and minimum one byte value. */
#define dhcpMAX_OPTION_LENGTH_OF_INTEREST		( 2L )

/* Standard DHCP port numbers and magic cookie value. */
#if( ipconfigBYTE_ORDER == pdFREERTOS_LITTLE_ENDIAN )
	#define dhcpCLIENT_PORT 0x4400
	#define dhcpSERVER_PORT 0x4300
	#define dhcpCOOKIE		0x63538263
	#define dhcpBROADCAST	0x0080
#else
	#define dhcpCLIENT_PORT 0x0044
	#define dhcpSERVER_PORT 0x0043
	#define dhcpCOOKIE		0x63825363
	#define dhcpBROADCAST	0x8000
#endif /* ipconfigBYTE_ORDER */

#include "pack_struct_start.h"
struct xDHCPMessage
{
	uint8_t ucOpcode;
	uint8_t ucAddressType;
	uint8_t ucAddressLength;
	uint8_t ucHops;
	uint32_t ulTransactionID;
	uint16_t usElapsedTime;
	uint16_t usFlags;
	uint32_t ulClientIPAddress_ciaddr;
	uint32_t ulYourIPAddress_yiaddr;
	uint32_t ulServerIPAddress_siaddr;
	uint32_t ulRelayAgentIPAddress_giaddr;
	uint8_t ucClientHardwareAddress[ dhcpCLIENT_HARDWARE_ADDRESS_LENGTH ];
	uint8_t ucServerHostName[ dhcpSERVER_HOST_NAME_LENGTH ];
	uint8_t ucBootFileName[ dhcpBOOT_FILE_NAME_LENGTH ];
	uint32_t ulDHCPCookie;
	uint8_t ucFirstOptionByte;
}
#include "pack_struct_end.h"
typedef struct xDHCPMessage xDHCPMessage_t;

/* DHCP state machine states. */
typedef enum
{
	eWaitingSendFirstDiscover = 0,	/* Initial state.  Send a discover the first time it is called, and reset all timers. */
	eWaitingOffer,					/* Either resend the discover, or, if the offer is forthcoming, send a request. */
	eWaitingAcknowledge,			/* Either resend the request. */
	eLeasedAddress,					/* Resend the request at the appropriate time to renew the lease. */
	eNotUsingLeasedAddress			/* DHCP failed, and a default IP address is being used. */
} eDHCPState_t;

/* Hold information in between steps in the DHCP state machine. */
struct xDHCP_DATA
{
	uint32_t ulTransactionId;
	uint32_t ulOfferedIPAddress;
	uint32_t ulDHCPServerAddress;
	uint32_t ulLeaseTime;
	/* Hold information on the current timer state. */
	TickType_t xDHCPTxTime;
	TickType_t xDHCPTxPeriod;
	/* Try both without and with the broadcast flag */
	BaseType_t xUseBroadcast;
	/* Maintains the DHCP state machine state. */
	eDHCPState_t eDHCPState;
	/* The UDP socket used for all incoming and outgoing DHCP traffic. */
	xSocket_t xDHCPSocket;
};

typedef struct xDHCP_DATA xDHCPData_t;

/*
 * Generate a DHCP discover message and send it on the DHCP socket.
 */
static void prvSendDHCPDiscover( void );

/*
 * Interpret message received on the DHCP socket.
 */
static BaseType_t prvProcessDHCPReplies( BaseType_t xExpectedMessageType );

/*
 * Generate a DHCP request packet, and send it on the DHCP socket.
 */
static void prvSendDHCPRequest( void );

/*
 * Prepare to start a DHCP transaction.  This initialises some state variables
 * and creates the DHCP socket if necessary.
 */
static void prvInitialiseDHCP( void );

/*
 * Creates the part of outgoing DHCP messages that are common to all outgoing
 * DHCP messages.
 */
static uint8_t *prvCreatePartDHCPMessage( struct freertos_sockaddr *pxAddress, BaseType_t xOpcode, const uint8_t * const pucOptionsArray, size_t *pxOptionsArraySize );

/*
 * Create the DHCP socket, if it has not been created already.
 */
static void prvCreateDHCPSocket( void );

/*-----------------------------------------------------------*/

/* The next DHCP transaction Id to be used. */
static xDHCPData_t xDHCPData;

/*-----------------------------------------------------------*/

BaseType_t xIsDHCPSocket( xSocket_t xSocket )
{
BaseType_t xReturn;

	if( xDHCPData.xDHCPSocket == xSocket )
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

void vDHCPProcess( BaseType_t xReset )
{
	/* Is DHCP starting over? */
	if( xReset != pdFALSE )
	{
		xDHCPData.eDHCPState = eWaitingSendFirstDiscover;
	}

	switch( xDHCPData.eDHCPState )
	{
		case eWaitingSendFirstDiscover :

			/* Initial state.  Create the DHCP socket, timer, etc. if they
			have not already been created. */
			prvInitialiseDHCP();
			*ipLOCAL_IP_ADDRESS_POINTER = 0UL;

			/* Send the first discover request. */
			if( xDHCPData.xDHCPSocket != NULL )
			{
				xDHCPData.xDHCPTxTime = xTaskGetTickCount();
				prvSendDHCPDiscover( );
				xDHCPData.eDHCPState = eWaitingOffer;
			}
			break;

		case eWaitingOffer :

			/* Look for offers coming in. */
			if( prvProcessDHCPReplies( dhcpMESSAGE_TYPE_OFFER ) == pdPASS )
			{
				/* An offer has been made, generate the request. */
				xDHCPData.xDHCPTxTime = xTaskGetTickCount();
				xDHCPData.xDHCPTxPeriod = dhcpINITIAL_DHCP_TX_PERIOD;
				prvSendDHCPRequest( );
				xDHCPData.eDHCPState = eWaitingAcknowledge;
			}
			else
			{
				/* Is it time to send another Discover? */
				if( ( xTaskGetTickCount() - xDHCPData.xDHCPTxTime ) > xDHCPData.xDHCPTxPeriod )
				{
					/* Increase the time period, and if it has not got to the
					point of giving up - send another discovery. */
					xDHCPData.xDHCPTxPeriod <<= 1;

					if( xDHCPData.xDHCPTxPeriod <= ipconfigMAXIMUM_DISCOVER_TX_PERIOD )
					{
						xDHCPData.ulTransactionId++;
						xDHCPData.xDHCPTxTime = xTaskGetTickCount();
						xDHCPData.xUseBroadcast = !xDHCPData.xUseBroadcast;
						prvSendDHCPDiscover( );
						FreeRTOS_debug_printf( ( "vDHCPProcess: timeout %lu ticks\n",
							xDHCPData.xDHCPTxPeriod ) );
					}
					else
					{
						FreeRTOS_debug_printf( ( "vDHCPProcess: giving up %lu > %lu ticks\n",
							xDHCPData.xDHCPTxPeriod, ipconfigMAXIMUM_DISCOVER_TX_PERIOD ) );

						/* Revert to static IP address. */
						taskENTER_CRITICAL();
						{
							*ipLOCAL_IP_ADDRESS_POINTER = xNetworkAddressing.ulDefaultIPAddress;
							iptraceDHCP_REQUESTS_FAILED_USING_DEFAULT_IP_ADDRESS( xNetworkAddressing.ulDefaultIPAddress );
						}
						taskEXIT_CRITICAL();

						xDHCPData.eDHCPState = eNotUsingLeasedAddress;
						vIPSetDHCPTimerEnableState( pdFALSE );

						/* DHCP failed, the default configured IP-address will be used
						Now call vIPNetworkUpCalls() to send the network-up event, start Nabto
						and start the ARP timer*/
						vIPNetworkUpCalls( );

						/* Close socket to ensure packets don't queue on it. */
						FreeRTOS_closesocket( xDHCPData.xDHCPSocket );
						xDHCPData.xDHCPSocket = NULL;
					}
				}
			}
			break;

		case eWaitingAcknowledge :

			/* Look for acks coming in. */
			if( prvProcessDHCPReplies( dhcpMESSAGE_TYPE_ACK ) == pdPASS )
			{
				FreeRTOS_debug_printf( ( "vDHCPProcess: acked %lxip\n", FreeRTOS_ntohl( xDHCPData.ulOfferedIPAddress ) ) );

				/* DHCP completed.  The IP address can now be used, and the
				timer set to the lease timeout time. */
				*ipLOCAL_IP_ADDRESS_POINTER = xDHCPData.ulOfferedIPAddress;

				/* Setting the 'local' broadcast address, something like 192.168.1.255' */
				xNetworkAddressing.ulBroadcastAddress = ( xDHCPData.ulOfferedIPAddress & xNetworkAddressing.ulNetMask ) |  ~xNetworkAddressing.ulNetMask;
				xDHCPData.eDHCPState = eLeasedAddress;

				iptraceDHCP_SUCCEDEED( xDHCPData.ulOfferedIPAddress );

				/* DHCP failed, the default configured IP-address will be used
				Now call vIPNetworkUpCalls() to send the network-up event, start Nabto
				and start the ARP timer*/
				vIPNetworkUpCalls( );

				/* Close socket to ensure packets don't queue on it. */
				FreeRTOS_closesocket( xDHCPData.xDHCPSocket );
				xDHCPData.xDHCPSocket = NULL;

				if( xDHCPData.ulLeaseTime == 0UL )
				{
					xDHCPData.ulLeaseTime = dhcpDEFAULT_LEASE_TIME;
				}
				else if( xDHCPData.ulLeaseTime < dhcpMINIMUM_LEASE_TIME )
				{
					xDHCPData.ulLeaseTime = dhcpMINIMUM_LEASE_TIME;
				}
				else
				{
					/* The lease time is already valid. */
				}

				/* Check for clashes. */
				vARPSendGratuitous();
				vIPReloadDHCPTimer( xDHCPData.ulLeaseTime );
			}
			else
			{
				/* Is it time to send another Discover? */
				if( ( xTaskGetTickCount() - xDHCPData.xDHCPTxTime ) > xDHCPData.xDHCPTxPeriod )
				{
					/* Increase the time period, and if it has not got to the
					point of giving up - send another request. */
					xDHCPData.xDHCPTxPeriod <<= 1;

					if( xDHCPData.xDHCPTxPeriod <= ipconfigMAXIMUM_DISCOVER_TX_PERIOD )
					{
						xDHCPData.xDHCPTxTime = xTaskGetTickCount();
						prvSendDHCPRequest( );
					}
					else
					{
						/* Give up, start again. */
						xDHCPData.eDHCPState = eWaitingSendFirstDiscover;
					}
				}
			}
			break;

		case eLeasedAddress :

			/* Resend the request at the appropriate time to renew the lease. */
			prvCreateDHCPSocket();

			if( xDHCPData.xDHCPSocket != NULL )
			{
				xDHCPData.xDHCPTxTime = xTaskGetTickCount();
				xDHCPData.xDHCPTxPeriod = dhcpINITIAL_DHCP_TX_PERIOD;
				prvSendDHCPRequest( );
				xDHCPData.eDHCPState = eWaitingAcknowledge;
				/* From now on, we should be called more often */
				vIPReloadDHCPTimer( dhcpINITIAL_TIMER_PERIOD );
			}
			break;

		case eNotUsingLeasedAddress:

			vIPSetDHCPTimerEnableState( pdFALSE );
			break;
	}
}
/*-----------------------------------------------------------*/

static void prvCreateDHCPSocket( void )
{
struct freertos_sockaddr xAddress;
BaseType_t xReturn;
TickType_t xTimeoutTime = 0;

	/* Create the socket, if it has not already been created. */
	if( xDHCPData.xDHCPSocket == NULL )
	{
		xDHCPData.xDHCPSocket = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
		configASSERT( ( xDHCPData.xDHCPSocket != FREERTOS_INVALID_SOCKET ) );

		/* Ensure the Rx and Tx timeouts are zero as the DHCP executes in the
		context of the IP task. */
		FreeRTOS_setsockopt( xDHCPData.xDHCPSocket, 0, FREERTOS_SO_RCVTIMEO, ( void * ) &xTimeoutTime, sizeof( TickType_t ) );
		FreeRTOS_setsockopt( xDHCPData.xDHCPSocket, 0, FREERTOS_SO_SNDTIMEO, ( void * ) &xTimeoutTime, sizeof( TickType_t ) );

		/* Bind to the standard DHCP client port. */
		xAddress.sin_port = dhcpCLIENT_PORT;
		xReturn = vSocketBind( xDHCPData.xDHCPSocket, &xAddress, sizeof( xAddress ), pdFALSE );
		configASSERT( xReturn == 0 );

		/* Remove compiler warnings if configASSERT() is not defined. */
		( void ) xReturn;
	}
}
/*-----------------------------------------------------------*/

static void prvInitialiseDHCP( void )
{
	/* Initialise the parameters that will be set by the DHCP process. */
	if( xDHCPData.ulTransactionId == 0 )
	{
		xDHCPData.ulTransactionId = ipconfigRAND32();
	}
	else
	{
		xDHCPData.ulTransactionId++;
	}

	xDHCPData.xUseBroadcast = 0;
	xDHCPData.ulOfferedIPAddress = 0UL;
	xDHCPData.ulDHCPServerAddress = 0UL;
	xDHCPData.xDHCPTxPeriod = dhcpINITIAL_DHCP_TX_PERIOD;

	/* Create the DHCP socket if it has not already been created. */
	prvCreateDHCPSocket();
	FreeRTOS_debug_printf( ( "prvInitialiseDHCP: start after %lu ticks\n", dhcpINITIAL_TIMER_PERIOD ) );
	vIPReloadDHCPTimer( dhcpINITIAL_TIMER_PERIOD );
}
/*-----------------------------------------------------------*/

static BaseType_t prvProcessDHCPReplies( BaseType_t xExpectedMessageType )
{
uint8_t *pucUDPPayload, *pucLastByte;
struct freertos_sockaddr xClient;
uint32_t xClientLength = sizeof( xClient );
int32_t lBytes;
xDHCPMessage_t *pxDHCPMessage;
uint8_t *pucByte, ucOptionCode, ucLength;
uint32_t ulProcessed, ulParameter;
BaseType_t xReturn = pdFALSE;
const uint32_t ulMandatoryOptions = 2; /* DHCP server address, and the correct DHCP message type must be present in the options. */

	lBytes = FreeRTOS_recvfrom( xDHCPData.xDHCPSocket, ( void * ) &pucUDPPayload, 0, FREERTOS_ZERO_COPY, &xClient, &xClientLength );

	if( lBytes > 0 )
	{
		/* Map a DHCP structure onto the received data. */
		pxDHCPMessage = ( xDHCPMessage_t * ) ( pucUDPPayload );

		/* Sanity check. */
		if( ( pxDHCPMessage->ulDHCPCookie == dhcpCOOKIE ) && ( pxDHCPMessage->ucOpcode == dhcpREPLY_OPCODE ) &&
			( pxDHCPMessage->ulTransactionID == FreeRTOS_htonl( xDHCPData.ulTransactionId ) ) )
		{
			if( memcmp( ( void * ) &( pxDHCPMessage->ucClientHardwareAddress ), ( void * ) ipLOCAL_MAC_ADDRESS, sizeof( xMACAddress_t ) ) == 0 )
			{
				/* None of the essential options have been processed yet. */
				ulProcessed = 0;

				/* Walk through the options until the dhcpOPTION_END_BYTE byte
				is found, taking care not to walk off the end of the options. */
				pucByte = &( pxDHCPMessage->ucFirstOptionByte );
				pucLastByte = &( pucUDPPayload[ lBytes - dhcpMAX_OPTION_LENGTH_OF_INTEREST ] );

				while( ( *pucByte != dhcpOPTION_END_BYTE ) && ( pucByte < pucLastByte ) )
				{
					ucOptionCode = pucByte[ 0 ];
					ucLength = pucByte[ 1 ];
					pucByte += 2;

					/* In most cases, a 4-byte network-endian parameter follows,
					just get it once here and use later */
					memcpy( ( void * ) &( ulParameter ), ( void * ) pucByte, ( size_t ) sizeof( ulParameter ) );

					switch( ucOptionCode )
					{
						case dhcpMESSAGE_TYPE_OPTION_CODE	:

							if( *pucByte == ( uint8_t ) xExpectedMessageType )
							{
								/* The message type is the message type the
								state machine is expecting. */
								ulProcessed++;
							}
							else if( *pucByte == dhcpMESSAGE_TYPE_NACK )
							{
								if( dhcpMESSAGE_TYPE_ACK == ( uint8_t ) xExpectedMessageType)
								{
									/* Start again. */
									xDHCPData.eDHCPState = eWaitingSendFirstDiscover;
								}
							}
							else
							{
								/* Don't process other message types. */
							}
							break;

						case dhcpSUBNET_MASK_OPTION_CODE :

							if( ucLength == sizeof( uint32_t ) )
							{
								xNetworkAddressing.ulNetMask = ulParameter;
							}
							break;

						case dhcpGATEWAY_OPTION_CODE :

							if( ucLength == sizeof( uint32_t ) )
							{
								/* ulProcessed is not incremented in this case
								because the gateway is not essential. */
								xNetworkAddressing.ulGatewayAddress = ulParameter;
							}
							break;

						case dhcpDNS_SERVER_OPTIONS_CODE :

							/* ulProcessed is not incremented in this case
							because the DNS server is not essential.  Only the
							first DNS server address is taken. */
							xNetworkAddressing.ulDNSServerAddress = ulParameter;
							break;

						case dhcpSERVER_IP_ADDRESS_OPTION_CODE :

							if( ucLength == sizeof( uint32_t ) )
							{
								if( dhcpMESSAGE_TYPE_OFFER == ( uint8_t ) xExpectedMessageType )
								{
									/* Offers state the replying server. */
									ulProcessed++;
									xDHCPData.ulDHCPServerAddress = ulParameter;
								}
								else
								{
									/* The ack must come from the expected server. */
									if( xDHCPData.ulDHCPServerAddress == ulParameter )
									{
										ulProcessed++;
									}
								}
							}
							break;

						case dhcpLEASE_TIME_OPTION_CODE :

							if( ucLength == sizeof( &( xDHCPData.ulLeaseTime ) ) )
							{
								/* ulProcessed is not incremented in this case
								because the lease time is not essential. */
								/* HT:endian: ulLeaseTime should always be
								host-endian, so convert it now */
								xDHCPData.ulLeaseTime = FreeRTOS_ntohl( ulParameter );

								/* Convert the lease time to milliseconds
								(*1000) then ticks (/portTICK_PERIOD_MS). */
								xDHCPData.ulLeaseTime *= ( 1000UL / portTICK_PERIOD_MS );

								/* Divide the lease time by two to ensure a renew
								request is sent before the lease actually expires. */
								xDHCPData.ulLeaseTime >>= 1UL;
							}
							break;

						default :

							/* Not interested in this field. */

							break;
					}

					/* Jump over the data to find the next option code. */
					if( ucLength == 0 )
					{
						break;
					}
					else
					{
						pucByte += ucLength;
					}
				}

				/* Were all the mandatory options received? */
				if( ulProcessed >= ulMandatoryOptions )
				{
					/* HT:endian: used to be network order */
					xDHCPData.ulOfferedIPAddress = pxDHCPMessage->ulYourIPAddress_yiaddr;
					FreeRTOS_printf( ( "vDHCPProcess: offer %lxip\n", FreeRTOS_ntohl( xDHCPData.ulOfferedIPAddress ) ) );
					xReturn = pdPASS;
				}
			}
		}

		FreeRTOS_ReleaseUDPPayloadBuffer( ( void * ) pucUDPPayload );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static uint8_t *prvCreatePartDHCPMessage( struct freertos_sockaddr *pxAddress, BaseType_t xOpcode, const uint8_t * const pucOptionsArray, size_t *pxOptionsArraySize )
{
xDHCPMessage_t *pxDHCPMessage;
size_t xRequiredBufferSize = sizeof( xDHCPMessage_t ) + *pxOptionsArraySize;
uint8_t *pucUDPPayloadBuffer;

#if( ipconfigDHCP_REGISTER_HOSTNAME == 1 )
	const char *pucHostName = pcApplicationHostnameHook ();
	size_t xNameLength = strlen( pucHostName );
	uint8_t *pucPtr;

	xRequiredBufferSize += ( 2 + xNameLength );
#endif

	/* Get a buffer.  This uses a maximum delay, but the delay will be capped
	to ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS so the return value still needs to
	be test. */
	do
	{
	} while( ( pucUDPPayloadBuffer = ( uint8_t * ) FreeRTOS_GetUDPPayloadBuffer( xRequiredBufferSize, portMAX_DELAY ) ) == NULL );

	pxDHCPMessage = ( xDHCPMessage_t * ) pucUDPPayloadBuffer;

	/* Most fields need to be zero. */
	memset( ( void * ) pxDHCPMessage, 0x00, sizeof( xDHCPMessage_t ) );

	/* Create the message. */
	pxDHCPMessage->ucOpcode = ( uint8_t ) xOpcode;
	pxDHCPMessage->ucAddressType = dhcpADDRESS_TYPE_ETHERNET;
	pxDHCPMessage->ucAddressLength = dhcpETHERNET_ADDRESS_LENGTH;

	/* ulTransactionID doesn't really need a htonl() translation, but when DHCP
	times out, it is nicer to see an increasing number in this ID field */
	pxDHCPMessage->ulTransactionID = FreeRTOS_htonl( xDHCPData.ulTransactionId );
	pxDHCPMessage->ulDHCPCookie = dhcpCOOKIE;
	if( xDHCPData.xUseBroadcast )
	{
		pxDHCPMessage->usFlags = dhcpBROADCAST;
	}
	else
	{
		pxDHCPMessage->usFlags = 0;
	}

	memcpy( ( void * ) &( pxDHCPMessage->ucClientHardwareAddress[ 0 ] ), ( void * ) ipLOCAL_MAC_ADDRESS, sizeof( xMACAddress_t ) );

	/* Copy in the const part of the options options. */
	memcpy( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET ] ), ( void * ) pucOptionsArray, *pxOptionsArraySize );

	#if( ipconfigDHCP_REGISTER_HOSTNAME == 1 )
	{
		/* With this option, the hostname can be registered as well which makes
		it easier to lookup a device in a router's list of DHCP clients. */

		/* Point to where the OPTION_END was stored to add data. */
		pucPtr = &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + ( *pxOptionsArraySize - 1 ) ] );
		pucPtr[ 0 ] = dhcpDNS_HOSTNAME_OPTIONS_CODE;
		pucPtr[ 1 ] = ( uint8_t ) xNameLength;
		memcpy( ( void *) ( pucPtr + 2 ), pucHostName, xNameLength );
		pucPtr[ 2 + xNameLength ] = dhcpOPTION_END_BYTE;
		*pxOptionsArraySize += ( 2 + xNameLength );
	}
	#endif

	/* Map in the client identifier. */
	memcpy( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + dhcpCLIENT_IDENTIFIER_OFFSET ] ),
		( void * ) ipLOCAL_MAC_ADDRESS, sizeof( xMACAddress_t ) );

	/* Set the addressing. */
	pxAddress->sin_addr = ipBROADCAST_IP_ADDRESS;
	pxAddress->sin_port = ( uint16_t ) dhcpSERVER_PORT;

	return pucUDPPayloadBuffer;
}
/*-----------------------------------------------------------*/

static void prvSendDHCPRequest( void )
{
uint8_t *pucUDPPayloadBuffer;
struct freertos_sockaddr xAddress;
static const uint8_t ucDHCPRequestOptions[] =
{
	/* Do not change the ordering without also changing
	dhcpCLIENT_IDENTIFIER_OFFSET, dhcpREQUESTED_IP_ADDRESS_OFFSET and
	dhcpDHCP_SERVER_IP_ADDRESS_OFFSET. */
	dhcpMESSAGE_TYPE_OPTION_CODE, 1, dhcpMESSAGE_TYPE_REQUEST,		/* Message type option. */
	dhcpCLIENT_IDENTIFIER_OPTION_CODE, 6, 0, 0, 0, 0, 0, 0,			/* Client identifier. */
	dhcpREQUEST_IP_ADDRESS_OPTION_CODE, 4, 0, 0, 0, 0,				/* The IP address being requested. */
	dhcpSERVER_IP_ADDRESS_OPTION_CODE, 4, 0, 0, 0, 0,				/* The IP address of the DHCP server. */
	dhcpOPTION_END_BYTE
};
size_t xOptionsLength = sizeof( ucDHCPRequestOptions );

	pucUDPPayloadBuffer = prvCreatePartDHCPMessage( &xAddress, (uint8_t)dhcpREQUEST_OPCODE, ucDHCPRequestOptions, &xOptionsLength );

	/* Copy in the IP address being requested. */
	memcpy( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + dhcpREQUESTED_IP_ADDRESS_OFFSET ] ),
		( void * ) &( xDHCPData.ulOfferedIPAddress ), sizeof( xDHCPData.ulOfferedIPAddress ) );

	/* Copy in the address of the DHCP server being used. */
	memcpy( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + dhcpDHCP_SERVER_IP_ADDRESS_OFFSET ] ),
		( void * ) &( xDHCPData.ulDHCPServerAddress ), sizeof( xDHCPData.ulDHCPServerAddress ) );

	FreeRTOS_debug_printf( ( "vDHCPProcess: reply %lxip\n", FreeRTOS_ntohl( xDHCPData.ulOfferedIPAddress ) ) );
	iptraceSENDING_DHCP_REQUEST();

	if( FreeRTOS_sendto( xDHCPData.xDHCPSocket, pucUDPPayloadBuffer, ( sizeof( xDHCPMessage_t ) + xOptionsLength ), FREERTOS_ZERO_COPY, &xAddress, sizeof( xAddress ) ) == 0 )
	{
		/* The packet was not successfully queued for sending and must be
		returned to the stack. */
		FreeRTOS_ReleaseUDPPayloadBuffer( pucUDPPayloadBuffer );
	}
}
/*-----------------------------------------------------------*/

static void prvSendDHCPDiscover( void )
{
uint8_t *pucUDPPayloadBuffer;
struct freertos_sockaddr xAddress;
static const uint8_t ucDHCPDiscoverOptions[] =
{
	/* Do not change the ordering without also changing dhcpCLIENT_IDENTIFIER_OFFSET. */
	dhcpMESSAGE_TYPE_OPTION_CODE, 1, dhcpMESSAGE_TYPE_DISCOVER,					/* Message type option. */
	dhcpCLIENT_IDENTIFIER_OPTION_CODE, 6, 0, 0, 0, 0, 0, 0,						/* Client identifier. */
	dhcpPARAMETER_REQUEST_OPTION_CODE, 3, dhcpSUBNET_MASK_OPTION_CODE, dhcpGATEWAY_OPTION_CODE, dhcpDNS_SERVER_OPTIONS_CODE,	/* Parameter request option. */
	dhcpOPTION_END_BYTE
};
size_t xOptionsLength = sizeof( ucDHCPDiscoverOptions );

	pucUDPPayloadBuffer = prvCreatePartDHCPMessage( &xAddress, (uint8_t)dhcpREQUEST_OPCODE, ucDHCPDiscoverOptions, &xOptionsLength );

	FreeRTOS_debug_printf( ( "vDHCPProcess: discover\n" ) );
	iptraceSENDING_DHCP_DISCOVER();

	if( FreeRTOS_sendto( xDHCPData.xDHCPSocket, pucUDPPayloadBuffer, ( sizeof( xDHCPMessage_t ) + xOptionsLength ), FREERTOS_ZERO_COPY, &xAddress, sizeof( xAddress ) ) == 0 )
	{
		/* The packet was not successfully queued for sending and must be
		returned to the stack. */
		FreeRTOS_ReleaseUDPPayloadBuffer( pucUDPPayloadBuffer );
	}
}
/*-----------------------------------------------------------*/

#endif /* ipconfigUSE_DHCP != 0 */


