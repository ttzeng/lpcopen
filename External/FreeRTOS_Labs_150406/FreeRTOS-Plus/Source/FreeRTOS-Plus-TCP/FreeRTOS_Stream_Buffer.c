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

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"

/*
 * lStreamBufferAdd( )
 * Adds data to a stream buffer.  If lOffset > 0, data will be written at
 * an offset from lHead while lHead will not be moved yet.  This possibility
 * will be used when TCP data is received while earlier data is still missing.
 * If 'pucData' equals NULL, the function is called to advance 'lHead' only.
 */
int32_t lStreamBufferAdd( xStreamBuffer *pxBuffer, int32_t lOffset, const uint8_t *pucData, int32_t lCount )
{
int32_t lSpace, lNextHead, lFirst;

	lSpace = lStreamBufferGetSpace( pxBuffer );

	/* If lOffset > 0, items can be placed in front of lHead */
	if( lSpace > lOffset )
	{
		lSpace -= lOffset;
	}
	else
	{
		lSpace = 0;
	}

	/* The number of bytes that can be written is the minimum of the number of
	bytes requested and the number available. */
	lCount = FreeRTOS_min_int32( lSpace, lCount );

	if( lCount != 0 )
	{
		lNextHead = pxBuffer->lHead;

		if( lOffset != 0 )
		{
			/* ( lOffset > 0 ) means: write in front if the lHead marker */
			lNextHead += lOffset;
			if( lNextHead >= pxBuffer->LENGTH )
			{
				lNextHead -= pxBuffer->LENGTH;
			}
		}

		if( pucData != NULL )
		{
			/* Calculate the number of bytes that can be added in the first
			write - which may be less than the total number of bytes that need
			to be added if the buffer will wrap back to the beginning. */
			lFirst = FreeRTOS_min_int32( pxBuffer->LENGTH - lNextHead, lCount );

			/* Write as many bytes as can be written in the first write. */
			memcpy( ( void* ) ( pxBuffer->ucArray + lNextHead ), pucData, ( size_t ) lFirst );

			/* If the number of bytes written was less than the number that
			could be written in the first write... */
			if( lCount > lFirst )
			{
				/* ...then write the remaining bytes to the start of the
				buffer. */
				memcpy( ( void * )pxBuffer->ucArray, pucData + lFirst, ( size_t )( lCount - lFirst ) );
			}
		}

		if( lOffset == 0 )
		{
			/* ( lOffset == 0 ) means: write at lHead position */
			lNextHead += lCount;
			if( lNextHead >= pxBuffer->LENGTH )
			{
				lNextHead -= pxBuffer->LENGTH;
			}
			pxBuffer->lHead = lNextHead;
		}

		if( xStreamBufferLessThenEqual( pxBuffer, pxBuffer->lFront, lNextHead ) != pdFALSE )
		{
			/* Advance the front pointer */
			pxBuffer->lFront = lNextHead;
		}
	}

	return lCount;
}
/*-----------------------------------------------------------*/

/*
 * lStreamBufferGet( )
 * 'lOffset' can be used to read data located at a certain offset from 'lTail'.
 * If 'pucData' equals NULL, the function is called to advance 'lTail' only.
 * if 'xPeek' is pdTRUE, or if 'lOffset' is non-zero, the 'lTail' pointer will
 * not be advanced.
 */
int32_t lStreamBufferGet( xStreamBuffer *pxBuffer, int32_t lOffset, uint8_t *pucData, int32_t lMaxCount, BaseType_t xPeek )
{
int32_t lSize, lCount, lFirst, lNextTail;

	/* How much data is available? */
	lSize = lStreamBufferGetSize( pxBuffer );

	if( lSize > lOffset )
	{
		lSize -= lOffset;
	}
	else
	{
		lSize = 0;
	}

	/* Use the minimum of the wanted bytes and the available bytes. */
	lCount = FreeRTOS_min_int32( lSize, lMaxCount );

	if( lCount > 0 )
	{
		lNextTail = pxBuffer->lTail;

		if( lOffset != 0 )
		{
			lNextTail += lOffset;
			if( lNextTail >= pxBuffer->LENGTH )
			{
				lNextTail -= pxBuffer->LENGTH;
			}
		}

		if( pucData != NULL )
		{
			/* Calculate the number of bytes that can be read - which may be
			less than the number wanted if the data wraps around to the start of
			the buffer. */
			lFirst = FreeRTOS_min_int32( pxBuffer->LENGTH - lNextTail, lCount );

			/* Obtain the number of bytes it is possible to obtain in the first
			read. */
			memcpy( pucData, pxBuffer->ucArray + lNextTail, ( size_t ) lFirst );

			/* If the total number of wanted bytes is greater than the number
			that could be read in the first read... */
			if( lCount > lFirst )
			{
				/*...then read the remaining bytes from the start of the buffer. */
				memcpy( pucData + lFirst, pxBuffer->ucArray, ( size_t )( lCount - lFirst ) );
			}
		}

		if( ( xPeek == pdFALSE ) && ( lOffset == 0L ) )
		{
			/* Move the tail pointer to effecively remove the data read from
			the buffer. */
			lNextTail += lCount;

			if( lNextTail >= pxBuffer->LENGTH )
			{
				lNextTail -= pxBuffer->LENGTH;
			}

			pxBuffer->lTail = lNextTail;
		}
	}

	return lCount;
}

