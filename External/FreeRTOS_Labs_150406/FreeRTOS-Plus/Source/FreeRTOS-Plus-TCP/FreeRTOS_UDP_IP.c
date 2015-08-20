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
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_ARP.h"
#include "FreeRTOS_DHCP.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

#if( ipconfigUSE_DNS == 1 )
	#include "FreeRTOS_DNS.h"
#endif

/* The expected IP version and header length coded into the IP header itself. */
#define ipIP_VERSION_AND_HEADER_LENGTH_BYTE ( ( uint8_t ) 0x45 )

/* Part of the Ethernet and IP headers are always constant when sending an IPv4
UDP packet.  This array defines the constant parts, allowing this part of the
packet to be filled in using a simple memcpy() instead of individual writes. */
UdpPacketHeader_t xDefaultPartUDPPacketHeader =
{
	/* .ucBytes : */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 	/* Ethernet source MAC address. */
		0x08, 0x00, 							/* Ethernet frame type. */
		ipIP_VERSION_AND_HEADER_LENGTH_BYTE, 	/* ucVersionHeaderLength. */
		0x00, 									/* ucDifferentiatedServicesCode. */
		0x00, 0x00, 							/* usLength. */
		0x00, 0x00, 							/* usIdentification. */
		0x00, 0x00, 							/* usFragmentOffset. */
		ipconfigUDP_TIME_TO_LIVE, 				/* ucTimeToLive */
		ipPROTOCOL_UDP, 						/* ucProtocol. */
		0x00, 0x00, 							/* usHeaderChecksum. */
		0x00, 0x00, 0x00, 0x00 					/* Source IP address. */
	}
};
/*-----------------------------------------------------------*/

void vProcessGeneratedUDPPacket( xNetworkBufferDescriptor_t * const pxNetworkBuffer )
{
xUDPPacket_t *pxUDPPacket;
xIPHeader_t *pxIPHeader;
eARPLookupResult_t eReturned;
uint32_t ulIPAddress = pxNetworkBuffer->ulIPAddress;

	/* Map the UDP packet onto the start of the frame. */
	pxUDPPacket = ( xUDPPacket_t * ) pxNetworkBuffer->pucEthernetBuffer;

	/* Determine the ARP cache status for the requested IP address. */
	eReturned = eARPGetCacheEntry( &( ulIPAddress ), &( pxUDPPacket->xEthernetHeader.xDestinationAddress ) );
	if( eReturned != eCantSendPacket )
	{
		if( eReturned == eARPCacheHit )
		{
			#if( ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM == 0 )
				uint8_t xSocketOptions;
			#endif
			iptraceSENDING_UDP_PACKET( pxNetworkBuffer->ulIPAddress );

			/* Create short cuts to the data within the packet. */
			pxIPHeader = &( pxUDPPacket->xIPHeader );

		#if ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )
			/* Is it possible that the packet is not actually a UDP packet
			after all, but an ICMP packet. */
			if( pxNetworkBuffer->usPort != ipPACKET_CONTAINS_ICMP_DATA )
		#endif /* ipconfigSUPPORT_OUTGOING_PINGS */
			{
			xUDPHeader_t *pxUDPHeader;

				pxUDPHeader = &( pxUDPPacket->xUDPHeader );

				pxUDPHeader->usDestinationPort = pxNetworkBuffer->usPort;
				pxUDPHeader->usSourcePort = pxNetworkBuffer->usBoundPort;
				pxUDPHeader->usLength = ( uint16_t ) ( pxNetworkBuffer->xDataLength + sizeof( xUDPHeader_t ) );
				pxUDPHeader->usLength = FreeRTOS_htons( pxUDPHeader->usLength );
				pxUDPHeader->usChecksum = 0;
			}

			/* memcpy() the constant parts of the header information into
			the	correct location within the packet.  This fills in:
				xEthernetHeader.xSourceAddress
				xEthernetHeader.usFrameType
				xIPHeader.ucVersionHeaderLength
				xIPHeader.ucDifferentiatedServicesCode
				xIPHeader.usLength
				xIPHeader.usIdentification
				xIPHeader.usFragmentOffset
				xIPHeader.ucTimeToLive
				xIPHeader.ucProtocol
			and
				xIPHeader.usHeaderChecksum
			*/
			/* Save options now, as they will be overwritten by memcpy */
			#if( ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM == 0 )
				xSocketOptions = pxNetworkBuffer->pucEthernetBuffer[ ipSOCKET_OPTIONS_OFFSET ];
			#endif
			memcpy( ( void *) &( pxUDPPacket->xEthernetHeader.xSourceAddress ), ( void * ) xDefaultPartUDPPacketHeader.ucBytes, sizeof( xDefaultPartUDPPacketHeader ) );

		#if ipconfigSUPPORT_OUTGOING_PINGS == 1
			if( pxNetworkBuffer->usPort == ipPACKET_CONTAINS_ICMP_DATA )
			{
				pxIPHeader->ucProtocol = ipPROTOCOL_ICMP;
				pxIPHeader->usLength = ( uint16_t ) ( pxNetworkBuffer->xDataLength + sizeof( xIPHeader_t ) );
			}
			else
		#endif /* ipconfigSUPPORT_OUTGOING_PINGS */
			{
				pxIPHeader->usLength = ( uint16_t ) ( pxNetworkBuffer->xDataLength + sizeof( xIPHeader_t ) + sizeof( xUDPHeader_t ) );
			}

			/* The total transmit size adds on the Ethernet header. */
			pxNetworkBuffer->xDataLength = pxIPHeader->usLength + sizeof( xEthernetHeader_t );
			pxIPHeader->usLength = FreeRTOS_htons( pxIPHeader->usLength );
			/* HT:endian: changed back to network endian */
			pxIPHeader->ulDestinationIPAddress = pxNetworkBuffer->ulIPAddress;

			#if( ipconfigUSE_LLMNR == 1 )
			{
				/* LLMNR messages are typically used on a LAN and they're
				 * not supposed to cross routers */
				if( pxNetworkBuffer->ulIPAddress == ipLLMNR_IP_ADDR )
				{
					pxIPHeader->ucTimeToLive = 0x01;
				}
			}
			#endif

			#if( ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM == 0 )
			{
				pxIPHeader->usHeaderChecksum = 0;
				pxIPHeader->usHeaderChecksum = usGenerateChecksum( 0UL, ( uint8_t * ) &( pxIPHeader->ucVersionHeaderLength ), ipSIZE_OF_IP_HEADER );
				pxIPHeader->usHeaderChecksum = ~FreeRTOS_htons( pxIPHeader->usHeaderChecksum );

				if( ( xSocketOptions & FREERTOS_SO_UDPCKSUM_OUT ) != 0 )
				{
					usGenerateProtocolChecksum( (uint8_t*)pxUDPPacket, pdTRUE );
				}
				else
				{
					pxUDPPacket->xUDPHeader.usChecksum = 0;
				}
			}
			#endif
		}
		else if( eReturned == eARPCacheMiss )
		{
			/* Add an entry to the ARP table with a null hardware address.
			This allows the ARP timer to know that an ARP reply is
			outstanding, and perform retransmissions if necessary. */
			vARPRefreshCacheEntry( NULL, ulIPAddress );

			/* Generate an ARP for the required IP address. */
			iptracePACKET_DROPPED_TO_GENERATE_ARP( pxNetworkBuffer->ulIPAddress );
			pxNetworkBuffer->ulIPAddress = ulIPAddress;
			vARPGenerateRequestPacket( pxNetworkBuffer );
		}
		else
		{
			/* The lookup indicated that an ARP request has already been
			sent out for the queried IP address. */
			eReturned = eCantSendPacket;
		}
	}

	if( eReturned != eCantSendPacket )
	{
		/* The network driver is responsible for freeing the network buffer
		after the packet has been sent. */
		xNetworkInterfaceOutput( pxNetworkBuffer, pdTRUE );
	}
	else
	{
		/* The packet can't be sent (DHCP not completed?).  Just drop the
		packet. */
		vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
	}
}
/*-----------------------------------------------------------*/

BaseType_t xProcessReceivedUDPPacket( xNetworkBufferDescriptor_t *pxNetworkBuffer, uint16_t usPort )
{
BaseType_t xReturn = pdPASS;
xFreeRTOS_Socket_t *pxSocket;

xUDPPacket_t *pxUDPPacket = (xUDPPacket_t *) pxNetworkBuffer->pucEthernetBuffer;

	pxSocket = pxUDPSocketLookup( usPort );

	if( pxSocket )
	{

		/* When refreshing the ARP cache with received UDP packets we must be
		careful;  hundreds of broadcast messages may pass and if we're not
		handling them, no use to fill the ARP cache with those IP addresses. */
		vARPRefreshCacheEntry( &( pxUDPPacket->xEthernetHeader.xSourceAddress ), pxUDPPacket->xIPHeader.ulSourceIPAddress );

		#if( ipconfigUSE_CALLBACKS == 1 )
		{
			/* Did the owner of this socket register a reception handler ? */
			if( ipconfigIS_VALID_PROG_ADDRESS( pxSocket->u.xUdp.pHndReceive ) )
			{
				struct freertos_sockaddr xSourceAddress, destinationAddress;
				void *pcData = ( void * ) &( pxNetworkBuffer->pucEthernetBuffer[ ipUDP_PAYLOAD_OFFSET ] );
				FOnUdpReceive xHandler = ( FOnUdpReceive ) pxSocket->u.xUdp.pHndReceive;
				xSourceAddress.sin_port = pxNetworkBuffer->usPort;
				xSourceAddress.sin_addr = pxNetworkBuffer->ulIPAddress;
				destinationAddress.sin_port = usPort;
				destinationAddress.sin_addr = pxUDPPacket->xIPHeader.ulDestinationIPAddress;

				if( xHandler( (xSocket_t *)pxSocket, ( void* ) pcData, ( size_t ) pxNetworkBuffer->xDataLength,
					&xSourceAddress, &destinationAddress ) )
				{
					xReturn = pdFAIL; /* FAIL means that we did not consume or release the buffer */
				}
			}
		}
		#endif /* ipconfigUSE_CALLBACKS */

		#if( ipconfigUDP_MAX_RX_PACKETS > 0 )
		{
			if( xReturn == pdPASS )
			{
				if ( listCURRENT_LIST_LENGTH( &( pxSocket->u.xUdp.xWaitingPacketsList ) ) >= pxSocket->u.xUdp.xMaxPackets )
				{
					FreeRTOS_debug_printf( ( "xProcessReceivedUDPPacket: buffer full %ld >= %ld port %u\n",
						listCURRENT_LIST_LENGTH( &( pxSocket->u.xUdp.xWaitingPacketsList ) ),
						pxSocket->u.xUdp.xMaxPackets, pxSocket->usLocPort ) );
					xReturn = pdFAIL; /* we did not consume or release the buffer */
				}
			}
		}
		#endif

		if( xReturn == pdPASS )
		{
			vTaskSuspendAll();
			{
				if( xReturn == pdPASS )
				{
					taskENTER_CRITICAL();
					{
						/* Add the network packet to the list of packets to be
						processed by the socket. */
						vListInsertEnd( &( pxSocket->u.xUdp.xWaitingPacketsList ), &( pxNetworkBuffer->xBufferListItem ) );
					}
					taskEXIT_CRITICAL();
				}
			}
			xTaskResumeAll();

			/* Set the socket's receive event */
			if( pxSocket->xEventGroup != NULL )
			{
				xEventGroupSetBits( pxSocket->xEventGroup, eSOCKET_RECEIVE );
			}

			#if( ipconfigSUPPORT_SELECT_FUNCTION == 1 )
			{
				if( ( pxSocket->pxSocketSet != NULL ) && ( ( pxSocket->xSelectBits & eSELECT_READ ) != 0 ) )
				{
					xEventGroupSetBits( pxSocket->pxSocketSet->xSelectGroup, eSELECT_READ );
				}
			}
			#endif

			#if( ipconfigSOCKET_HAS_USER_SEMAPHORE == 1 )
			{
				if( pxSocket->pxUserSemaphore != NULL )
				{
					xSemaphoreGive( pxSocket->pxUserSemaphore );
				}
			}
			#endif

			#if( ipconfigUSE_DHCP == 1 )
			{
				if( xIsDHCPSocket( pxSocket ) )
				{
					xSendEventToIPTask( eDHCPEvent );
				}
			}
			#endif
		}
	}
	else
	{
		/* There is no socket listening to the target port, but still it might
		be for this node. */

		#if( ipconfigUSE_DNS == 1 )
			/* a DNS reply, check for the source port.  Although the DNS client
			does open a UDP socket to send a messages, this socket will be
			closed after a short timeout.  Messages that come late (after the
			socket is closed) will be treated here. */
			if( FreeRTOS_ntohs( pxUDPPacket->xUDPHeader.usSourcePort ) == ipDNS_PORT )
			{
				vARPRefreshCacheEntry( &( pxUDPPacket->xEthernetHeader.xSourceAddress ), pxUDPPacket->xIPHeader.ulSourceIPAddress );
				xReturn = ( BaseType_t )ulDNSHandlePacket( pxNetworkBuffer );
			}
			else
		#endif

		#if( ipconfigUSE_LLMNR == 1 )
			/* a LLMNR request, check for the destination port. */
			if( usPort == FreeRTOS_ntohs( ipLLMNR_PORT ) )
			{
				vARPRefreshCacheEntry( &( pxUDPPacket->xEthernetHeader.xSourceAddress ), pxUDPPacket->xIPHeader.ulSourceIPAddress );
				xReturn = ( BaseType_t )ulDNSHandlePacket( pxNetworkBuffer );
			}
			else
		#endif /* ipconfigUSE_LLMNR */

		#if( ipconfigUSE_NBNS == 1 )
			/* a NetBIOS request, check for the destination port */
			if( usPort == FreeRTOS_ntohs( ipNBNS_PORT ) )
			{
				vARPRefreshCacheEntry( &( pxUDPPacket->xEthernetHeader.xSourceAddress ), pxUDPPacket->xIPHeader.ulSourceIPAddress );
				xReturn = ( BaseType_t )ulNBNSHandlePacket( pxNetworkBuffer );
			}
			else
		#endif /* ipconfigUSE_NBNS */
		{
			xReturn = pdFAIL;
		}
	}

	return xReturn;
}
/*-----------------------------------------------------------*/
