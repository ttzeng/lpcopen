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
	Some code which is common to TCP servers like HTTP and FTP
*/

#ifndef FREERTOS_SERVER_PRIVATE_H
#define	FREERTOS_SERVER_PRIVATE_H

#define FREERTOS_NO_SOCKET		NULL

/* FreeRTOS+FAT */
#include "ff_stdio.h"

/* Each HTTP server has 1, at most 2 sockets */
#define	HTTP_SOCKET_COUNT	2

#ifndef HTTP_COMMAND_BUFFER_SIZE
	#define HTTP_COMMAND_BUFFER_SIZE	( 2048 )
#endif

struct xTCP_CLIENT;

typedef BaseType_t ( * FTCPWorkFunction ) ( struct xTCP_CLIENT * /* pxClient */ );
typedef void ( * FTCPDeleteFunction ) ( struct xTCP_CLIENT * /* pxClient */ );

#define	TCP_CLIENT_FIELDS \
	enum eSERVER_TYPE eType; \
	struct xTCP_SERVER *pxParent; \
	xSocket_t xSocket; \
	const char *pcRootDir; \
	FTCPWorkFunction fWorkFunction; \
	FTCPDeleteFunction fDeleteFunction; \
	struct xTCP_CLIENT *pxNextClient

typedef struct xTCP_CLIENT
{
	/* This define contains fields which must come first within each of the client structs */
	TCP_CLIENT_FIELDS;
	/* --- Keep at the top  --- */

} xTCPClient;

struct xHTTP_CLIENT
{
	/* This define contains fields which must come first within each of the client structs */
	TCP_CLIENT_FIELDS;
	/* --- Keep at the top  --- */

	const char *pcUrlData;
	const char *pcRestData;
	char pcCurrentFilename[ ffconfigMAX_FILENAME ];
	size_t xBytesLeft;
	FF_FILE *pxFileHandle;
	union {
		struct {
			uint32_t
				bReplySent : 1;
		};
		uint32_t ulFlags;
	} bits;
};

typedef struct xHTTP_CLIENT xHTTPClient;

struct xFTP_CLIENT
{
	/* This define contains fields which must come first within each of the client structs */
	TCP_CLIENT_FIELDS;
	/* --- Keep at the top  --- */

	uint32_t ulRestartOffset;
	uint32_t ulRecvBytes;
	uint32_t xBytesLeft;	/* Bytes left to send */
	uint32_t ulClientIP;
	TickType_t xStartTime;
	uint16_t usClientPort;
	xSocket_t xTransferSocket;
	BaseType_t xTransType;
	BaseType_t xDirCount;
	FF_FindData_t xFindData;
	FF_FILE *pxReadHandle;
	FF_FILE *pxWriteHandle;
	char pcCurrentDir[ ffconfigMAX_FILENAME ];
	char pcFileName[ ffconfigMAX_FILENAME ];
	char pcConnectionAck[ 64 ];
	char pcClientAck[ 128 ];
	union {
		struct {
			uint32_t
				bHelloSent : 1,
				bLoggedIn : 1,
				bStatusUser : 1,
				bInRename : 1;
		};
		uint32_t ulFTPFlags;
	} bits;
	union {
		struct {
			uint32_t
				bIsListen : 1,		/* Data socket was opened with listen() */
				bDirHasEntry : 1,
				bClientConnected : 1,
				bClientPrepared : 1,
				bClientClosing : 1;
		};
		uint32_t ulConnFlags;
	} bits1;
};

typedef struct xFTP_CLIENT xFTPClient;

BaseType_t xHTTPClientWork( xTCPClient *pxClient );
BaseType_t xFTPClientWork( xTCPClient *pxClient );

void vHTTPClientDelete( xTCPClient *pxClient );
void vFTPClientDelete( xTCPClient *pxClient );

BaseType_t xMakeAbsolute( struct xFTP_CLIENT *pxClient, char *pcBuffer, BaseType_t xBufferLength, const char *pcFileName );

struct xTCP_SERVER
{
	xSocketSet_t xSocketSet;
	char pcCommandBuffer[ HTTP_COMMAND_BUFFER_SIZE ];
	char pcFileBuffer[ HTTP_COMMAND_BUFFER_SIZE ];
	#if( ipconfigUSE_FTP != 0 )
		char pcNewDir[ ffconfigMAX_FILENAME ];
	#endif
	#if( ipconfigUSE_HTTP != 0 )
		char pcContentsType[40];	/* Space for the msg: "text/javascript" */
		char pcExtraContents[40];	/* Space for the msg: "Content-Length: 346500" */
	#endif
	BaseType_t xServerCount;
	xTCPClient *pxClients;
	struct xSERVER
	{
		enum eSERVER_TYPE eType;		/* eSERVER_HTTP | eSERVER_FTP */
		const char *pcRootDir;
		xSocket_t xSocket;
	} xServers[ 1 ];
};

#endif /* FREERTOS_SERVER_PRIVATE_H */
