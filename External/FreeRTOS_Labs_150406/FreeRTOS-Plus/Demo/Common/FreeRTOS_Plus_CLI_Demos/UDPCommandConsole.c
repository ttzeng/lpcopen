/*
    FreeRTOS V8.2.1 - Copyright (C) 2015 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>!AND MODIFIED BY!<< the FreeRTOS exception.

    ***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<
    ***************************************************************************

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available on the following
    link: http://www.freertos.org/a00114.html

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that is more than just the market leader, it     *
     *    is the industry's de facto standard.                               *
     *                                                                       *
     *    Help yourself get started quickly while simultaneously helping     *
     *    to support the FreeRTOS project by purchasing a FreeRTOS           *
     *    tutorial book, reference manual, or both:                          *
     *    http://www.FreeRTOS.org/Documentation                              *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading
    the FAQ page "My application does not run, what could be wrong?".  Have you
    defined configASSERT()?

    http://www.FreeRTOS.org/support - In return for receiving this top quality
    embedded software for free we request you assist our global community by
    participating in the support forum.

    http://www.FreeRTOS.org/training - Investing in training allows your team to
    be as productive as possible as early as possible.  Now you can receive
    FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers
    Ltd, and the world's leading authority on the world's leading RTOS.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.
    Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

    http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High
    Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and commercial middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

    1 tab == 4 spaces!
*/

/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* FreeRTOS+CLI includes. */
#include "FreeRTOS_CLI.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

/* Dimensions the buffer into which input characters are placed. */
#define cmdMAX_INPUT_SIZE	60

/* Dimensions the buffer into which string outputs can be placed. */
#define cmdMAX_OUTPUT_SIZE	1024

/* Dimensions the buffer passed to the recvfrom() call. */
#define cmdSOCKET_INPUT_BUFFER_SIZE 60

/* DEL acts as a backspace. */
#define cmdASCII_DEL		( 0x7F )

/* The socket used by the CLI and debug print messages. */
static xSocket_t xSocket = FREERTOS_INVALID_SOCKET;

/*
 * The task that runs FreeRTOS+CLI.
 */
static void prvUDPCommandInterpreterTask( void *pvParameters );

/*
 * Function called to start the command interpreter.
 */
void vStartUDPCommandInterpreterTask( uint16_t usStackSize, uint32_t ulPort, UBaseType_t uxPriority );

/*
 * Open and configure the UDP socket.
 */
static xSocket_t prvOpenUDPServerSocket( uint16_t usPort );

/*
 * Repeatedly call the FreeRTOS+CLI command interpreter until all output has
 * been generated and sent.
 */
static void prvProcessCommandString( char *cInputString, struct freertos_sockaddr *pxClient );

/* This is required as a parameter to maintain the sendto() Berkeley sockets
API - but it is not actually used so can take any value. */
static socklen_t xClientAddressLength = sizeof( struct freertos_sockaddr );

/* The UDP socket is used for both the command console and logging printfs, so
the buffer used for output is guarded by a mutex. */
static SemaphoreHandle_t xLoggingMutex = NULL;

/* The buffer into which formatted text is written for output. */
static char cOutputString[ cmdMAX_OUTPUT_SIZE ];

/* Destination address used by debug printfs. */
struct freertos_sockaddr xPrintUDPAddress;

/*-----------------------------------------------------------*/

void vStartUDPCommandInterpreterTask( uint16_t usStackSize, uint32_t ulPort, UBaseType_t uxPriority )
{
	/* Create the mutex used to guard the socket.  A mutex is required as the
	same socket can be used for both CLI output and logging message. */
	xLoggingMutex = xSemaphoreCreateMutex();
	configASSERT( xLoggingMutex );

	/* The network address to which debug printfs are sent.  Receives on a
	port number one higher than sends. */
	xPrintUDPAddress.sin_port = FreeRTOS_htons( ( ( uint16_t ) ulPort + 1 ) );
	xPrintUDPAddress.sin_addr = FreeRTOS_inet_addr_quick( configECHO_SERVER_ADDR0, configECHO_SERVER_ADDR1, configECHO_SERVER_ADDR2, configECHO_SERVER_ADDR3 );

	xTaskCreate( prvUDPCommandInterpreterTask, "UDP CLI", usStackSize, ( void * ) ulPort, uxPriority, NULL );
}
/*-----------------------------------------------------------*/

void prvUDPCommandInterpreterTask( void *pvParameters )
{
int32_t lBytes, lByte;
char cRxedChar, cInputIndex = 0;
static char cInputString[ cmdMAX_INPUT_SIZE ], cLocalBuffer[ cmdSOCKET_INPUT_BUFFER_SIZE ];
BaseType_t xMoreDataToFollow;
struct freertos_sockaddr xClient;
static const TickType_t xReceiveTimeOut = 0UL;

	/* Attempt to open the socket.  The port number is passed in the task
	parameter.  The strange casting is to remove compiler warnings on 32-bit
	machines. */
	xSocket = prvOpenUDPServerSocket( ( uint16_t ) ( ( uint32_t ) pvParameters ) & 0xffffUL );

	/* In case the same socket is used for user output it must be set to non
	blocking sends.  Otherwise being called from something like a ping reply
	hook would result in the socket being used from inside the IP task, and
	the IP task cannot block to wait for itself. */
	FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_SNDTIMEO, &xReceiveTimeOut, sizeof( xReceiveTimeOut ) );

	if( xSocket != FREERTOS_INVALID_SOCKET )
	{
		for( ;; )
		{
			/* Wait for incoming data on the opened socket. */
			lBytes = FreeRTOS_recvfrom( xSocket, ( void * ) cLocalBuffer, sizeof( cLocalBuffer ), 0, &xClient, &xClientAddressLength );

			if( lBytes != FREERTOS_SOCKET_ERROR )
			{
				/* Process each received byte in turn. */
				lByte = 0;
				while( lByte < lBytes )
				{
					/* The next character in the input buffer. */
					cRxedChar = cLocalBuffer[ lByte ];
					lByte++;

					/* Newline characters are taken as the end of the command
					string. */
					if( cRxedChar == '\n' )
					{
						prvProcessCommandString( cInputString, &xClient );

						/* All the strings generated by the command processing
						have been sent.  Clear the input string ready to receive
						the next command. */
						cInputIndex = 0;
						memset( cInputString, 0x00, cmdMAX_INPUT_SIZE );

						/* Transmit a spacer, just to make the command console
						easier to read. */
						FreeRTOS_sendto( xSocket, "\r\n",  strlen( "\r\n" ), 0, &xClient, xClientAddressLength );
					}
					else
					{
						if( cRxedChar == '\r' )
						{
							/* Ignore the character.  Newlines are used to
							detect the end of the input string. */
						}
						else if( ( cRxedChar == '\b' ) || ( cRxedChar == cmdASCII_DEL ) )
						{
							/* Backspace was pressed.  Erase the last character
							in the string - if any. */
							if( cInputIndex > 0 )
							{
								cInputIndex--;
								cInputString[ ( int ) cInputIndex ] = '\0';
							}
						}
						else
						{
							/* A character was entered.  Add it to the string
							entered so far.  When a \n is entered the complete
							string will be passed to the command interpreter. */
							if( cInputIndex < cmdMAX_INPUT_SIZE )
							{
								cInputString[ ( int ) cInputIndex ] = cRxedChar;
								cInputIndex++;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		/* The socket could not be opened. */
		vTaskDelete( NULL );
	}
}
/*-----------------------------------------------------------*/

static void prvProcessCommandString( char *pcInputString, struct freertos_sockaddr *pxClient )
{
BaseType_t xMoreDataToFollow;

	/* Process the input string received prior to the
	newline. */
	xSemaphoreTake( xLoggingMutex, portMAX_DELAY );
	{
		do
		{
			/* Pass the string to FreeRTOS+CLI. */
			cOutputString[ 0 ] = 0x00;
			xMoreDataToFollow = FreeRTOS_CLIProcessCommand( pcInputString, cOutputString, cmdMAX_OUTPUT_SIZE );

			/* Send the output generated by the command's
			implementation. */
			FreeRTOS_sendto( xSocket, cOutputString,  strlen( ( const char * ) cOutputString ), 0, pxClient, xClientAddressLength );

		  /* Until the command does not generate any more output. */
		} while( xMoreDataToFollow != pdFALSE );

		xSemaphoreGive( xLoggingMutex );
	}
}
/*-----------------------------------------------------------*/

static xSocket_t prvOpenUDPServerSocket( uint16_t usPort )
{
struct freertos_sockaddr xServer;
xSocket_t xSocket = FREERTOS_INVALID_SOCKET;

	xSocket = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
	if( xSocket != FREERTOS_INVALID_SOCKET)
	{
		/* Zero out the server structure. */
		memset( ( void * ) &xServer, 0x00, sizeof( xServer ) );

		/* Set family and port. */
		xServer.sin_port = FreeRTOS_htons( usPort );

		/* Bind the address to the socket. */
		if( FreeRTOS_bind( xSocket, &xServer, sizeof( xServer ) ) == -1 )
		{
			FreeRTOS_closesocket( xSocket );
			xSocket = FREERTOS_INVALID_SOCKET;
		}
	}

	return xSocket;
}
/*-----------------------------------------------------------*/

void vLoggingPrintf( const char *pcFormat, ... )
{
static char cPrintString[ cmdMAX_OUTPUT_SIZE ];
char *pcSource, *pcTarget, *pcBegin;
size_t xLength, xLength2, rc;
static BaseType_t xMessageNumber = 0;
va_list args;
uint32_t ulIPAddress;
const char *pcTaskName;
const char *pcNoTask = "None";
const TickType_t xDontBlock = 0;

	/* Cannot be called until the mutex has been created. */
	if( xLoggingMutex != NULL )
	{
		if( xSemaphoreTake( xLoggingMutex, xDontBlock ) == pdPASS )
		{
			/* There are a variable number of parameters. */
			va_start( args, pcFormat );

			/* Additional info to place at the start of the log. */
			if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
			{
				pcTaskName = pcTaskGetTaskName( NULL );
			}
			else
			{
				pcTaskName = pcNoTask;
			}

			xLength = snprintf( cPrintString, cmdMAX_OUTPUT_SIZE, "%lu %lu [%s] ",
				xMessageNumber++,
				xTaskGetTickCount(),
				pcTaskName );

			xLength2 = vsnprintf( cPrintString + xLength, cmdMAX_OUTPUT_SIZE - xLength, pcFormat, args );

#warning xLength2 cannot be less than 0.
			if( xLength2 <  0 )
			{
				/* Clean up. */
				xLength2 = sizeof( cPrintString ) - 1 - xLength;
				cPrintString[ sizeof( cPrintString ) - 1 ] = '\0';
			}

			xLength += xLength2;
			va_end( args );

			/* For ease of viewing, copy the string into another buffer, converting
			IP addresses to dot notation on the way. */
			pcSource = cPrintString;
			pcTarget = cOutputString;

			while( ( *pcSource ) != '\0' )
			{
				*pcTarget = *pcSource;
				pcTarget++;
				pcSource++;

				/* Look forward for an IP address denoted by 'ip'. */
				if( ( isxdigit( ( int ) pcSource[ 0 ] ) != pdFALSE ) && ( ( int ) pcSource[ 1 ] == 'i' ) && ( ( int ) pcSource[ 2 ] == 'p' ) )
				{
					*pcTarget = *pcSource;
					pcTarget++;
					*pcTarget = '\0';
					pcBegin = pcTarget - 8;

					while( ( pcTarget > pcBegin ) && ( isxdigit( ( int ) pcTarget[ -1 ] ) != pdFALSE ) )
					{
						pcTarget--;
					}

					sscanf( pcTarget, "%8X", (unsigned int*) &ulIPAddress );
					rc = sprintf( pcTarget, "%lu.%lu.%lu.%lu",
						ulIPAddress >> 24,
						(ulIPAddress >> 16) & 0xff,
						(ulIPAddress >> 8) & 0xff,
						ulIPAddress & 0xff );
					pcTarget += rc;
					pcSource += 3; /* skip "<n>ip" */
				}
			}

			/* How far through the buffer was written? */
			xLength = ( BaseType_t ) ( pcTarget - cOutputString );

			/* If the message is to be logged to a UDP port then it can be sent
			directly because it only uses FreeRTOS function. */
			if( ( xSocket != FREERTOS_INVALID_SOCKET ) && ( FreeRTOS_IsNetworkUp() != pdFALSE ) )
			{
				FreeRTOS_sendto( xSocket, cOutputString, xLength, 0, &xPrintUDPAddress, sizeof( xPrintUDPAddress ) );
			}

			xSemaphoreGive( xLoggingMutex );
		}
	}
}



