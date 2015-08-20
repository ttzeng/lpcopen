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
 *	FreeRTOS_Stream_Buffer.h
 *
 *	A cicular character buffer
 *	An implementation of a circular buffer without a length field
 *	If LENGTH defines the size of the buffer, a maximum of (LENGT-1) bytes can be stored
 *	In order to add or read data from the buffer, memcpy() will be called at most 2 times
 */

#ifndef FREERTOS_STREAM_BUFFER_H
#define	FREERTOS_STREAM_BUFFER_H

#if	defined( __cplusplus )
extern "C" {
#endif

typedef struct xSTREAM_BUFFER {
	volatile int32_t lTail;		/* next item to read */
	volatile int32_t lMid;		/* iterator within the valid items */
	volatile int32_t lHead;		/* next position store a new item */
	volatile int32_t lFront;	/* iterator within the free space */
	int32_t LENGTH;				/* const value: number of reserved elements */
	uint8_t ucArray[ sizeof( int32_t ) ];
} xStreamBuffer;

static portINLINE void vStreamBufferClear( xStreamBuffer *pxBuffer )
{
	/* Make the circular buffer empty */
	pxBuffer->lHead = 0;
	pxBuffer->lTail = 0;
	pxBuffer->lFront = 0;
	pxBuffer->lMid = 0;
}
/*-----------------------------------------------------------*/

static portINLINE int32_t lStreamBufferSpace( const xStreamBuffer *pxBuffer, int32_t lLower, int32_t lUpper )
{
/* Returns the space between lLower and lUpper, which equals to the distance minus 1 */
int32_t lCount;

	lCount = pxBuffer->LENGTH + lUpper - lLower - 1;
	if( lCount >= pxBuffer->LENGTH )
	{
		lCount -= pxBuffer->LENGTH;
	}

	return lCount;
}
/*-----------------------------------------------------------*/

static portINLINE int32_t lStreamBufferDistance( const xStreamBuffer *pxBuffer, int32_t lLower, int32_t lUpper )
{
/* Returns the distance between lLower and lUpper */
int32_t lCount;

	lCount = pxBuffer->LENGTH + lUpper - lLower;
	if ( lCount >= pxBuffer->LENGTH )
	{
		lCount -= pxBuffer->LENGTH;
	}

	return lCount;
}
/*-----------------------------------------------------------*/

static portINLINE int32_t lStreamBufferGetSpace( const xStreamBuffer *pxBuffer )
{
	/* Returns the number of items which can still be added to lHead
	before hitting on lTail */
	return lStreamBufferSpace( pxBuffer, pxBuffer->lHead, pxBuffer->lTail );
}
/*-----------------------------------------------------------*/

static portINLINE int32_t lStreamBufferFrontSpace( const xStreamBuffer *pxBuffer )
{
	/* Distance between lFront and lTail
	or the number of items which can still be added to lFront,
	before hitting on lTail */
	return lStreamBufferSpace( pxBuffer, pxBuffer->lFront, pxBuffer->lTail );
}
/*-----------------------------------------------------------*/

static portINLINE int32_t lStreamBufferGetSize( const xStreamBuffer *pxBuffer )
{
	/* Returns the number of items which can be read from lTail
	before reaching lHead */
	return lStreamBufferDistance( pxBuffer, pxBuffer->lTail, pxBuffer->lHead );
}
/*-----------------------------------------------------------*/

static portINLINE int32_t lStreamBufferMidSpace( const xStreamBuffer *pxBuffer )
{
	/* Returns the distance between lHead and lMid */
	return lStreamBufferDistance( pxBuffer, pxBuffer->lMid, pxBuffer->lHead );
}
/*-----------------------------------------------------------*/

static portINLINE void vStreamBufferMoveMid( xStreamBuffer *pxBuffer, int32_t lCount )
{
	/* Increment lMid, but no further than lHead */
	int32_t lSize = lStreamBufferMidSpace( pxBuffer );
	if( lCount > lSize )
	{
		lCount = lSize;
	}
	pxBuffer->lMid += lCount;
	if( pxBuffer->lMid >= pxBuffer->LENGTH )
	{
		pxBuffer->lMid -= pxBuffer->LENGTH;
	}
}
/*-----------------------------------------------------------*/

static portINLINE BaseType_t xStreamBufferIsEmpty( const xStreamBuffer *pxBuffer )
{
BaseType_t xReturn;

	/* True if no item is available */
	if( pxBuffer->lHead == pxBuffer->lTail )
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

static portINLINE BaseType_t xStreamBufferIsFull( const xStreamBuffer *pxBuffer )
{
	/* True if the available space equals zero. */
	return lStreamBufferGetSpace( pxBuffer ) == 0;
}
/*-----------------------------------------------------------*/

static portINLINE BaseType_t xStreamBufferLessThenEqual( const xStreamBuffer *pxBuffer, int32_t ulLeft, int32_t ulRight )
{
BaseType_t xReturn;
int32_t lTail = pxBuffer->lTail;

	/* Returns true if ( ulLeft < ulRight ) */
	if( ( ulLeft < lTail ) ^ ( ulRight < lTail ) )
	{
		if( ulRight < lTail )
		{
			xReturn = pdTRUE;
		}
		else
		{
			xReturn = pdFALSE;
		}
	}
	else
	{
		if( ulLeft <= ulRight )
		{
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

static portINLINE int32_t lStreamBufferGetPtr( xStreamBuffer *pxBuffer, uint8_t **ppucData )
{
int32_t lNextTail = pxBuffer->lTail;
int32_t lSize = lStreamBufferGetSize( pxBuffer );

	*ppucData = pxBuffer->ucArray + lNextTail;

	return FreeRTOS_min_int32( lSize, pxBuffer->LENGTH - lNextTail );
}

/*
 * Add bytes to a stream buffer.
 *
 * pxBuffer -	The buffer to which the bytes will be added.
 * lOffset -	If lOffset > 0, data will be written at an offset from lHead
 *				while lHead will not be moved yet.
 * pucData -	A pointer to the data to be added.
 * lCount -		The number of bytes to add.
 */
int32_t lStreamBufferAdd( xStreamBuffer *pxBuffer, int32_t lOffset, const uint8_t *pucData, int32_t lCount );

/*
 * Read bytes from a stream buffer.
 *
 * pxBuffer -	The buffer from which the bytes will be read.
 * lOffset -	Can be used to read data located at a certain offset from 'lTail'.
 * pucData -	A pointer to the buffer into which data will be read.
 * lMaxCount -	The number of bytes to read.
 * xPeek -		If set to pdTRUE the data will remain in the buffer.
 */
int32_t lStreamBufferGet( xStreamBuffer *pxBuffer, int32_t lOffset, uint8_t *pucData, int32_t lMaxCount, BaseType_t xPeek );

#if	defined( __cplusplus )
} /* extern "C" */
#endif

#endif	/* !defined( FREERTOS_STREAM_BUFFER_H ) */
