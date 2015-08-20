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

/******************************************************************************
 *
 * See the following web page for essential buffer allocation scheme usage and
 * configuration details:
 * http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/Embedded_Ethernet_Buffer_Management.html
 *
 ******************************************************************************/

/* THIS FILE SHOULD NOT BE USED IF THE PROJECT INCLUDES A MEMORY ALLOCATOR
THAT WILL FRAGMENT THE HEAP MEMORY.  For example, heap_2 must not be used,
heap_4 can be used. */


/* Standard includes. */
#include <stdint.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

/* For an Ethernet interrupt to be able to obtain a network buffer there must
be at least this number of buffers available. */
#define ipINTERRUPT_BUFFER_GET_THRESHOLD	( 3 )

/* The obtained network buffer must be large enough to hold a packet that might
replace the packet that was requested to be sent. */
#if ipconfigUSE_TCP == 1
	#define MINIMAL_BUFFER_SIZE		sizeof( xTCPPacket_t )
#else
	#define MINIMAL_BUFFER_SIZE		sizeof( xARPPacket_t )
#endif /* ipconfigUSE_TCP == 1 */

#if defined( ipconfigETHERNET_MINIMUM_PACKET_BYTES )
	/* If the NIC is not able to add padding bytes (needed for a minimum packet
	size of 64 bytes), the driver can add these bytes just before sending.
	Here make sure that all Network Buffers have a minimum length of
	'ipconfigETHERNET_MINIMUM_PACKET_BYTES bytes', which is normally 60. */
	#if( MINIMAL_BUFFER_SIZE < ipconfigETHERNET_MINIMUM_PACKET_BYTES )
		#undef MINIMAL_BUFFER_SIZE
		#define MINIMAL_BUFFER_SIZE ipconfigETHERNET_MINIMUM_PACKET_BYTES
	#endif
#endif

/* A list of free (available) xNetworkBufferDescriptor_t structures. */
static List_t xFreeBuffersList;

/* Some statistics about the use of buffers. */
static size_t uxMinimumFreeNetworkBuffers;

/* Declares the pool of xNetworkBufferDescriptor_t structures that are available to the
system.  All the network buffers referenced from xFreeBuffersList exist in this
array.  The array is not accessed directly except during initialisation, when
the xFreeBuffersList is filled (as all the buffers are free when the system is
booted). */
static xNetworkBufferDescriptor_t xNetworkBufferDescriptors[ ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ];

/* This constant is defined as false to let FreeRTOS_TCP_IP.c know that the network buffers
have a variable size: resizing may be necessary */
const BaseType_t xBufferAllocFixedSize = pdFALSE;

/* The semaphore used to obtain network buffers. */
static SemaphoreHandle_t xNetworkBufferSemaphore = NULL;

/*-----------------------------------------------------------*/

BaseType_t xNetworkBuffersInitialise( void )
{
BaseType_t xReturn, x;

	/* Only initialise the buffers and their associated kernel objects if they
	have not been initialised before. */
	if( xNetworkBufferSemaphore == NULL )
	{
		xNetworkBufferSemaphore = xSemaphoreCreateCounting( ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS, ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS );
		configASSERT( xNetworkBufferSemaphore );
		#if ( configQUEUE_REGISTRY_SIZE > 0 )
		{
			vQueueAddToRegistry( xNetworkBufferSemaphore, "NetBufSem" );
		}
		#endif /* configQUEUE_REGISTRY_SIZE */

		/* If the trace recorder code is included name the semaphore for viewing
		in FreeRTOS+Trace.  */
		#if( ipconfigINCLUDE_EXAMPLE_FREERTOS_PLUS_TRACE_CALLS == 1 )
		{
			extern QueueHandle_t xNetworkEventQueue;
			vTraceSetQueueName( xNetworkEventQueue, "IPStackEvent" );
			vTraceSetQueueName( xNetworkBufferSemaphore, "NetworkBufferCount" );
		}
		#endif /*  ipconfigINCLUDE_EXAMPLE_FREERTOS_PLUS_TRACE_CALLS == 1 */

		if( xNetworkBufferSemaphore != NULL )
		{
			vListInitialise( &xFreeBuffersList );

			/* Initialise all the network buffers.  No storage is allocated to
			the buffers yet. */
			for( x = 0; x < ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS; x++ )
			{
				/* Initialise and set the owner of the buffer list items. */
				xNetworkBufferDescriptors[ x ].pucEthernetBuffer = NULL;
				vListInitialiseItem( &( xNetworkBufferDescriptors[ x ].xBufferListItem ) );
				listSET_LIST_ITEM_OWNER( &( xNetworkBufferDescriptors[ x ].xBufferListItem ), &xNetworkBufferDescriptors[ x ] );

				/* Currently, all buffers are available for use. */
				vListInsert( &xFreeBuffersList, &( xNetworkBufferDescriptors[ x ].xBufferListItem ) );
			}

			uxMinimumFreeNetworkBuffers = ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS;
		}
	}

	if( xNetworkBufferSemaphore == NULL )
	{
		xReturn = pdFAIL;
	}
	else
	{
		xReturn = pdPASS;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

uint8_t *pucGetNetworkBuffer( size_t *pxRequestedSizeBytes )
{
uint8_t *pucEthernetBuffer;

	if( *pxRequestedSizeBytes < MINIMAL_BUFFER_SIZE )
	{
		/* Buffers must be at least large enough to hold a TCP-packet with
		headers, or an ARP packet, in case TCP is not included. */
		*pxRequestedSizeBytes = MINIMAL_BUFFER_SIZE;
	}

	/* Allocate a buffer large enough to store the requested Ethernet frame size
	and a pointer to a network buffer structure (hence the addition of
	ipBUFFER_PADDING bytes). */
	pucEthernetBuffer = ( uint8_t * ) pvPortMalloc( *pxRequestedSizeBytes + ipBUFFER_PADDING );
	configASSERT( pucEthernetBuffer );

	if( pucEthernetBuffer != NULL )
	{
		/* Enough space is left at the start of the buffer to place a pointer to
		the network buffer structure that references this Ethernet buffer.
		Return a pointer to the start of the Ethernet buffer itself. */
		pucEthernetBuffer += ipBUFFER_PADDING;
	}

	return pucEthernetBuffer;
}
/*-----------------------------------------------------------*/

void vReleaseNetworkBuffer( uint8_t *pucEthernetBuffer )
{
	/* There is space before the Ethernet buffer in which a pointer to the
	network buffer that references this Ethernet buffer is stored.  Remove the
	space before freeing the buffer. */
	if( pucEthernetBuffer != NULL )
	{
		pucEthernetBuffer -= ipBUFFER_PADDING;
		vPortFree( ( void * ) pucEthernetBuffer );
	}
}
/*-----------------------------------------------------------*/

xNetworkBufferDescriptor_t *pxGetNetworkBufferWithDescriptor( size_t xRequestedSizeBytes, TickType_t xBlockTimeTicks )
{
xNetworkBufferDescriptor_t *pxReturn = NULL;
size_t uxCount;

	if( ( xRequestedSizeBytes != 0 ) && ( xRequestedSizeBytes < MINIMAL_BUFFER_SIZE ) )
	{
		/* ARP packets can replace application packets, so the storage must be
		at least large enough to hold an ARP. */
		xRequestedSizeBytes = MINIMAL_BUFFER_SIZE;
	}

	/* If there is a semaphore available, there is a network buffer available. */
	if( xSemaphoreTake( xNetworkBufferSemaphore, xBlockTimeTicks ) == pdPASS )
	{
		/* Protect the structure as it is accessed from tasks and interrupts. */
		taskENTER_CRITICAL();
		{
			pxReturn = ( xNetworkBufferDescriptor_t * ) listGET_OWNER_OF_HEAD_ENTRY( &xFreeBuffersList );
			uxListRemove( &( pxReturn->xBufferListItem ) );
		}
		taskEXIT_CRITICAL();

		/* Reading UBaseType_t, no critical section needed. */
		uxCount = listCURRENT_LIST_LENGTH( &xFreeBuffersList );

		if( uxMinimumFreeNetworkBuffers > uxCount )
		{
			uxMinimumFreeNetworkBuffers = uxCount;
		}		

		/* Allocate storage of exactly the requested size to the buffer. */
		configASSERT( pxReturn->pucEthernetBuffer == NULL );
		if( xRequestedSizeBytes > 0 )
		{
			/* Extra space is obtained so a pointer to the network buffer can
			be stored at the beginning of the buffer. */
			pxReturn->pucEthernetBuffer = ( uint8_t * ) pvPortMalloc( xRequestedSizeBytes + ipBUFFER_PADDING );

			if( pxReturn->pucEthernetBuffer == NULL )
			{
				/* The attempt to allocate storage for the buffer payload failed,
				so the network buffer structure cannot be used and must be
				released. */
				vReleaseNetworkBufferAndDescriptor( pxReturn );
				pxReturn = NULL;
			}
			else
			{
				/* Store a pointer to the network buffer structure in the
				buffer storage area, then move the buffer pointer on past the
				stored pointer so the pointer value is not overwritten by the
				application when the buffer is used. */
				*( ( xNetworkBufferDescriptor_t ** ) ( pxReturn->pucEthernetBuffer ) ) = pxReturn;
				pxReturn->pucEthernetBuffer += ipBUFFER_PADDING;

				/* Store the actual size of the allocated buffer, which may be
				greater than the original requested size. */
				pxReturn->xDataLength = xRequestedSizeBytes;

				#if( ipconfigUSE_LINKED_RX_MESSAGES != 0 )
				{
					/* make sure the buffer is not linked */
					pxReturn->pxNextBuffer = NULL;
				}
				#endif /* ipconfigUSE_LINKED_RX_MESSAGES */
			}
		}
		else
		{
			/* A descriptor is being returned without an associated buffer being
			allocated. */
		}
	}

	if( pxReturn == NULL )
	{
		iptraceFAILED_TO_OBTAIN_NETWORK_BUFFER();
	}
	else
	{
		iptraceNETWORK_BUFFER_OBTAINED( pxReturn );
	}

	return pxReturn;
}
/*-----------------------------------------------------------*/

void vReleaseNetworkBufferAndDescriptor( xNetworkBufferDescriptor_t * const pxNetworkBuffer )
{
BaseType_t xListItemAlreadyInFreeList;

	/* Ensure the buffer is returned to the list of free buffers before the
	counting semaphore is 'given' to say a buffer is available.  Release the
	storage allocated to the buffer payload.  THIS FILE SHOULD NOT BE USED
	IF THE PROJECT INCLUDES A MEMORY ALLOCATOR THAT WILL FRAGMENT THE HEAP
	MEMORY.  For example, heap_2 must not be used, heap_4 can be used. */
	vReleaseNetworkBuffer( pxNetworkBuffer->pucEthernetBuffer );
	pxNetworkBuffer->pucEthernetBuffer = NULL;

	taskENTER_CRITICAL();
	{
		xListItemAlreadyInFreeList = listIS_CONTAINED_WITHIN( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );

		if( xListItemAlreadyInFreeList == pdFALSE )
		{
			vListInsertEnd( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );
		}
	}
	taskEXIT_CRITICAL();

	if( xListItemAlreadyInFreeList == pdFALSE )
	{
		xSemaphoreGive( xNetworkBufferSemaphore );
	}

	iptraceNETWORK_BUFFER_RELEASED( pxNetworkBuffer );
}
/*-----------------------------------------------------------*/

/*
 * Returns the number of free network buffers
 */
UBaseType_t uxGetNumberOfFreeNetworkBuffers( void )
{
	return listCURRENT_LIST_LENGTH( &xFreeBuffersList );
}
/*-----------------------------------------------------------*/

UBaseType_t uxGetMinimumFreeNetworkBuffers( void )
{
	return uxMinimumFreeNetworkBuffers;
}
/*-----------------------------------------------------------*/

