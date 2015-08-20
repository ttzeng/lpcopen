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
#include <stdlib.h>
#include <time.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

/* FreeRTOS Protocol includes. */
#include "FreeRTOS_FTP_commands.h"
#include "FreeRTOS_TCP_server.h"
#include "FreeRTOS_server_private.h"

#ifndef HTTP_SERVER_BACKLOG
	#define HTTP_SERVER_BACKLOG			( 12 )
#endif

#if !defined( ARRAY_SIZE )
	#define ARRAY_SIZE( x ) ( BaseType_t ) (sizeof ( x ) / sizeof ( x )[ 0 ] )
#endif

#if( _MSC_VER != 0 )
	#ifdef snprintf
		#undef snprintf
	#endif
	#define snprintf	_snprintf
	#define vsnprintf	_vsnprintf
#endif

#if defined(__WIN32__) && !defined(ipconfigFTP_FS_USES_BACKSLAH)
	#define ipconfigFTP_FS_USES_BACKSLAH	1
#endif

/* Some defines to make the code more readbale */
#define pcCOMMAND_BUFFER	pxClient->pxParent->pcCommandBuffer
#define pcNEW_DIR			pxClient->pxParent->pcNewDir
#define pcFILE_BUFFER		pxClient->pxParent->pcFileBuffer

/* This FTP server will only do binary transfers */
#define TMODE_BINARY	1
#define TMODE_ASCII		2
#define TMODE_7BITS		3
#define TMODE_8BITS		4

/*
 * This module only has 2 public functions:
 */
BaseType_t xFTPClientWork( xTCPClient *pxClient );
void vFTPClientDelete( xTCPClient *pxClient );

/* Process a single command */
static BaseType_t prvProcessCommand( xFTPClient *pxClient, BaseType_t index, char *pcRestCommand );

/* Create a socket for a data connection to the FTP client. */
static BaseType_t prvTransferConnect( xFTPClient *pxClient, BaseType_t xDoListen );

/* Either call listen() or connect to start the transfer connection. */
static BaseType_t prvTransferStart( xFTPClient *pxClient );

/* See if the socket has got connected or disconnected. Close the socket if necessary. */
static void prvTransferCheck( xFTPClient *pxClient );

/* Close the data socket and issue some informative logging. */
static void prvTransferCloseSocket( xFTPClient *pxClient );

/* Close the file handle (pxReadHandle or pxWriteHandle) */
static void prvTransferCloseFile( xFTPClient *pxClient );

/* Close a directory (-handle) */
static void prvTransferCloseDir( xFTPClient *pxClient );

/* Translate a string (indicating a transfer type) to a number */
static BaseType_t prvGetTransferType( const char *pcType );

#if( ipconfigHAS_PRINTF != 0 )
	/* For nice logging: write an amount (number of bytes), e.g. 3512200 as "3.45 MB" */
	static const char *pcMkSize( uint32_t ulAmount, char *pcBuffer, BaseType_t xBufferSize );
#endif

#if( ipconfigHAS_PRINTF != 0 )
	/* Calculate the average as bytes-per-second, when amount and milliseconds are known. */
	static uint32_t ulGetAverage( uint32_t ulAmount, TickType_t xDeltaMs );
#endif

/*
 * A port command looks like: PORT h1,h2,h3,h4,p1,p2. Parse it and translate it to an
 * IP-address and a port number
*/
static BaseType_t prvParsePortData( const char *pcCommand, uint32_t *pulIPAddress );

/* CWD: Change current working directory. */
static BaseType_t prvChangeDir( xFTPClient *pxClient, char *pcDirectory );

/* RNFR: Rename from ... */
static BaseType_t prvRenameFrom( xFTPClient *pxClient, const char *pcFileName );

/* RNTO: Rename to ... */
static BaseType_t prvRenameTo( xFTPClient *pxClient, const char *pcFileName );

/* SITE: Change file permissions. */
static BaseType_t prvSiteCmd( xFTPClient *pxClient, char *pcRestCommand );

/* DELE: Delete a file. */
static BaseType_t prvDeleteFile( xFTPClient *pxClient, char *pcFileName );

/* SIZE: get the size of a file (xSendDate = 0)
 * MDTM: get data and time properties (xSendDate = 1) */
static BaseType_t prvSizeDateFile( xFTPClient *pxClient, char *pcFileName, BaseType_t xSendDate );

/* MKD: Make / create a directory (xDoRemove = 0)
 * RMD: Remove a directory (xDoRemove = 1) */
static BaseType_t prvMakeRemoveDir( xFTPClient *pxClient, const char *pcDirectory, BaseType_t xDoRemove );

/* The next three commands: LIST, RETR and STOR all require a data socket.
 * The data connection is either started with a 'PORT' or a 'PASV' command.
 * Each of the commands has a prepare- (Prep) and a working- (Work) function.
 * The Work function should be called as long as the data socket is open, and
 * there is data to be transmitted.
 */

/* LIST: Send a directory listing in Unix style. */
static BaseType_t prvListSendPrep( xFTPClient *pxClient );
static BaseType_t prvListSendWork( xFTPClient *pxClient );

/* RETR: Send a file to the FTP client. */
static BaseType_t prvRetrieveFilePrep( xFTPClient *pxClient, char *pcFileName );
static BaseType_t prvRetrieveFileWork( xFTPClient *pxClient );

/* STOR: Receive a file from the FTP client and store it. */
static BaseType_t prvStoreFilePrep( xFTPClient *pxClient, char *pcFileName );
static BaseType_t prvStoreFileWork( xFTPClient *pxClient );

/* Print/format a single directory entry in Unix style */
static BaseType_t prvGetFileInfoStat( FF_DirEnt_t *pxEntry, char *pcLine, BaseType_t xMaxLength );

/* Send a reply to a socket, either the command- or the data-socket */
static BaseType_t prvSendReply( xSocket_t xSocket, const char *pcBuffer, BaseType_t xLength );

/* Prepend the root directory (if any), plus the current working directory (always),
 * to get an absolute path
 */
BaseType_t xMakeAbsolute( xFTPClient *pxClient, char *pcBuffer, BaseType_t xBufferLength, const char *pcPath );

/*

####### ##### ######        #     #                ##
 #   ## # # #  #    #       #     #                 #
 #        #    #    #       #     #                 #
 #        #    #    #       #  #  #  ####  ### ##   #    #
 #####    #    #####        #  #  # #    #  # #  #  #   #
 #   #    #    #            #  #  # #    #  ##   #  ####
 #        #    #             ## ##  #    #  #       #   #
 #        #    #             ## ##  #    #  #       #    #
####     #### ####           ## ##   ####  ####    ##   ##

 *	xFTPClientWork( )
 *	will be called by FreeRTOS_TCPServerWork( ), after select has expired().
 *	FD_ISSET will not be used.  This work function will always be called at
 *	regular intervals, and also after a select() event has occurred.
 */
BaseType_t xFTPClientWork( xTCPClient *pxTCPClient )
{
xFTPClient *pxClient = ( xFTPClient * ) pxTCPClient;
BaseType_t xRc;

	if( pxClient->bits.bHelloSent == pdFALSE )
	{
	BaseType_t xLength;

		pxClient->bits.bHelloSent = pdTRUE;

		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
			"220 Welcome to the FreeRTOS+TCP FTP server\r\n" );
		prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
	}

	/* Call recv() in a non-blocking way, to see if there is an FTP command
	sent to this server */
	xRc = FreeRTOS_recv( pxClient->xSocket, ( void * )pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), 0 );

	if( xRc > 0 )
	{
	BaseType_t xIndex;
	const struct xFTP_COMMAND *pxCommand;
	char *pcRestCommand;

		if( xRc < ( BaseType_t )HTTP_COMMAND_BUFFER_SIZE )
		{
			pcCOMMAND_BUFFER[ xRc ] = '\0';
		}
		while( xRc && ( pcCOMMAND_BUFFER[ xRc - 1 ] == 13 || pcCOMMAND_BUFFER[ xRc - 1 ] == 10 ) )
		{
			pcCOMMAND_BUFFER[ --xRc ] = '\0';
		}

		/* Now iterate through a list of FTP commands, and look for a match. */
		pxCommand = xFTPCommands;
		pcRestCommand = pcCOMMAND_BUFFER;
		for( xIndex = 0; xIndex < FTP_CMD_COUNT - 1; xIndex++, pxCommand++ )
		{
		BaseType_t xLength;

			/* The length of each command is stored as well, just to be a bit quicker here */
			xLength = pxCommand->xCommandLength;
			if( ( xRc >= xLength ) && ( memcmp( ( const void * ) pxCommand->pcCommandName, ( const void * ) pcCOMMAND_BUFFER, xLength ) == 0 ) )
			{
				/* A match with an existing command is found.  Skip any whitespace to get the
				first parameter */
				pcRestCommand += xLength;
				while( ( *pcRestCommand == ' ' ) || ( *pcRestCommand == '\t' ) )
				{
					pcRestCommand++;
				}
				break;
			}
		}
		/* If the command received was not recognised, xIndex will point to a fake entry
		called 'ECMD_UNKNOWN' */
		prvProcessCommand( pxClient, xIndex, pcRestCommand );
	}
	else if( xRc < 0 )
	{
		/* The connection will be closed and the client will be deleted */
		FreeRTOS_printf( ( "xFTPClientWork: xRc = %ld\n", xRc ) );
	}

	/* Does is have an open data connection? */
	if( pxClient->xTransferSocket != FREERTOS_NO_SOCKET )
	{
		/* See if the connection has changed. */
		prvTransferCheck( pxClient );

		/* "pcConnectionAck" contains a string like:
		"Response:	150 Accepted data connection from 192.168.2.3:6789"
		The socket can only be used once this acknowledgement has been sent. */
		if( ( pxClient->xTransferSocket != FREERTOS_NO_SOCKET ) && ( pxClient->pcConnectionAck[ 0 ] == '\0' ) )
		{
		BaseType_t xClientRc = 0;

			if( pxClient->bits1.bDirHasEntry )
			{
				/* Still listing a directory */
				xClientRc = prvListSendWork( pxClient );
			}
			else if( pxClient->pxReadHandle != NULL )
			{
				/* Sending a file */
				xClientRc = prvRetrieveFileWork( pxClient );
			}
			else if( pxClient->pxWriteHandle != NULL )
			{
				/* Receiving a file */
				xClientRc = prvStoreFileWork( pxClient );
			}

			if( xClientRc < 0 )
			{
				prvTransferCloseSocket( pxClient );
				prvTransferCloseFile( pxClient );
			}
		}
	}

	return xRc;
}
/*-----------------------------------------------------------*/

static void prvTransferCloseDir( xFTPClient *pxClient )
{
	/* Nothing to close for +FAT */
	( void ) pxClient;
}
/*-----------------------------------------------------------*/

void vFTPClientDelete( xTCPClient *pxTCPClient )
{
xFTPClient *pxClient = ( xFTPClient * ) pxTCPClient;

	prvTransferCloseDir( pxClient );
	prvTransferCloseSocket( pxClient );
	prvTransferCloseFile( pxClient );
	/* Close the FTP command socket */
	if( pxClient->xSocket != FREERTOS_NO_SOCKET )
	{
		FreeRTOS_FD_CLR( pxClient->xSocket, pxClient->pxParent->xSocketSet, eSELECT_ALL );
		FreeRTOS_closesocket( pxClient->xSocket );
		pxClient->xSocket = FREERTOS_NO_SOCKET;
	}
}
/*-----------------------------------------------------------*/

static BaseType_t prvProcessCommand( xFTPClient *pxClient, BaseType_t xIndex, char *pcRestCommand )
{
const struct xFTP_COMMAND *pxFTPCommand = &( xFTPCommands[ xIndex ] );
const char *pcMyReply = NULL;
BaseType_t xResult = 0;

	if( ( pxFTPCommand->ucCommandType != ECMD_PASS ) && ( pxFTPCommand->ucCommandType != ECMD_PORT ) )
	{
		FreeRTOS_printf( ( "       %s %s\n", pxFTPCommand->pcCommandName, pcRestCommand ) );
	}

	if( ( pxFTPCommand->checkLogin != pdFALSE ) && ( pxClient->bits.bLoggedIn == pdFALSE ) )
	{
		pcMyReply = REPL_530; // Please first log in
	}
	else if( ( pxFTPCommand->checkNullArg != pdFALSE ) && ( ( pcRestCommand == NULL ) || ( pcRestCommand[0] == '\0' ) ) )
	{
		pcMyReply = REPL_501; // Command needs a parameter
	}

	if( pcMyReply == NULL )
	{
		switch( pxFTPCommand->ucCommandType )
		{
		case ECMD_USER:	/* User */
			/* User name has been entered, expect password. */
			pxClient->bits.bStatusUser = pdTRUE;

			#if( ipconfigFTP_HAS_USER_PASSWORD_HOOK != 0 )
			{
				/* Save the user name in 'pcFileName' */
				snprintf( pxClient->pcFileName, sizeof pxClient->pcFileName, "%s", pcRestCommand );
				/* The USER name is presented to the application. The function may return
				a const string like "331 Please enter your password\r\n". */
				pcMyReply = pcApplicationFTPUserHook( pxClient->pcFileName );
				if( pcMyReply == NULL )
				{
					pcMyReply = REPL_331_ANON;
				}
			}
			#else
			{
				/* No password checks, any password will be accepted */
				pcMyReply = REPL_331_ANON;
			}
			#endif /* ipconfigFTP_HAS_USER_PASSWORD_HOOK != 0 */
			break;

		case ECMD_PASS:	/* Password */
			pxClient->ulRestartOffset = 0;
			if( pxClient->bits.bStatusUser == pdFALSE )
			{
				pcMyReply = REPL_503;	// "503 Bad sequence of commands.\r\n"
			}
			else
			{
			BaseType_t xAllow;

				pxClient->bits.bStatusUser = pdFALSE;
				#if( ipconfigFTP_HAS_USER_PASSWORD_HOOK != 0 )
				{
					xAllow = xApplicationFTPPasswordHook( pxClient->pcFileName, pcRestCommand );
				}
				#else
				{
					xAllow = 1;
				}
				#endif /* ipconfigFTP_HAS_USER_PASSWORD_HOOK */
				if( xAllow > 0 )
				{
					pxClient->bits.bLoggedIn = pdTRUE;  // Client has now logged in
					pcMyReply = "230 OK.  Current directory is /\r\n";
				}
				else
				{
					pcMyReply = "530 Login incorrect\r\n"; // 530 Login incorrect.
				}
				strcpy( pxClient->pcCurrentDir, ( const char * ) "/" );
			}
			break;

		case ECMD_SYST:	/* System */
			snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "215 UNIX Type: L8\r\n" );
			pcMyReply = pcCOMMAND_BUFFER;
			break;

		case ECMD_PWD:	/* Get working directory */
			snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), REPL_257_PWD, pxClient->pcCurrentDir );
			pcMyReply = pcCOMMAND_BUFFER;
			break;

		case ECMD_REST:
			{
				const char *pcPtr = pcRestCommand;
				while( *pcPtr == ' ' )
				{
					pcPtr++;
				}
				if( ( *pcPtr >= '0' ) && ( *pcPtr <= '9' ) )
				{
					sscanf( pcPtr, "%lu", &pxClient->ulRestartOffset );
					snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
						"350 Restarting at %lu. Send STORE or RETRIEVE\r\n", pxClient->ulRestartOffset );
					pcMyReply = pcCOMMAND_BUFFER;
				}
				else
				{
					pcMyReply = REPL_500; // "500 Syntax error, command unrecognized.\r\n"
				}
			}
			break;
		case ECMD_NOOP:	/* Nop operation */
			if( pxClient->xTransferSocket != FREERTOS_NO_SOCKET )
			{
				pcMyReply = REPL_200_PROGRESS;
			}
			else
			{
				pcMyReply = REPL_200;
			}
			break;

		case ECMD_TYPE:	/* Ask or set ransfer type */
			{
				// e.g. "TYPE I" for Images (binary)
				BaseType_t xType = prvGetTransferType( pcRestCommand );

				if( xType < 0 )
				{
					// TYPE not recognized
					pcMyReply = REPL_500;
				}
				else
				{
					pxClient->xTransType = xType;
					pcMyReply = REPL_200;
				}
			}
			break;

		case ECMD_PASV: /* Connect passive: server will listen() and wait for a connection */
			/* Start up a new data connection. Set parameter 'xDoListen' to true*/
			if( prvTransferConnect( pxClient, pdTRUE ) == pdFALSE )
			{
				pcMyReply = REPL_502;
			}
			else
			{
			uint32_t myIp;
			uint16_t myPort;
			struct freertos_sockaddr xLocalAddress;
			struct freertos_sockaddr xRemoteAddress;

				FreeRTOS_GetLocalAddress( pxClient->xTransferSocket, &xLocalAddress );
				FreeRTOS_GetRemoteAddress( pxClient->xSocket, &xRemoteAddress );

				myIp = FreeRTOS_ntohl( xLocalAddress.sin_addr );
				pxClient->ulClientIP = FreeRTOS_ntohl( xRemoteAddress.sin_addr );
				myPort = FreeRTOS_ntohs( xLocalAddress.sin_port );

				pxClient->usClientPort = FreeRTOS_ntohs( xRemoteAddress.sin_port );

				// REPL_227_D "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n"
				snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), REPL_227_D,
					( unsigned )myIp >> 24,
					( unsigned )( myIp >> 16 ) & 0xFF,
					( unsigned )( myIp >> 8 ) & 0xFF,
					( unsigned )myIp & 0xFF,
					( unsigned )myPort >> 8,
					( unsigned )myPort & 0xFF );

//				FreeRTOS_printf( ("Reply %lxip port %d remote %lxip port %d\n",
//					myIp, myPort, pxClient->ulClientIP, pxClient->usClientPort ) );
				pcMyReply = pcCOMMAND_BUFFER;
			}
			break;

		case ECMD_PORT:
			// The client uses this command to tell the server to what client-side port the
			// server should contact; use of this command indicates an active data transfer.
			// e.g. PORT 192,168,1,2,4,19
			{
				uint32_t ulIPAddress = 0;
				BaseType_t xPort = prvParsePortData( pcRestCommand, &ulIPAddress );
				FreeRTOS_printf( ("       PORT %lxip:%ld\n", ulIPAddress, xPort ) );
				if( xPort < 0 )
				{
					pcMyReply = REPL_501;
				}
				/* Call prvTransferConnect() with 'xDoListen' = false for an active connect(). */
				else if( prvTransferConnect( pxClient, pdFALSE ) == pdFALSE )
				{
					pcMyReply = REPL_501;
				}
				else
				{
					pxClient->usClientPort = xPort;
					pxClient->ulClientIP = ulIPAddress;
					FreeRTOS_printf( ("Client address %lxip:%lu\n", ulIPAddress, xPort ) );
					pcMyReply = REPL_200;
				}
			}
			break;
		case ECMD_CWD:	/* Change current working directory */
			prvChangeDir( pxClient, pcRestCommand );
			break;

		case ECMD_RNFR:
			prvRenameFrom( pxClient, pcRestCommand );
			break;

		case ECMD_RNTO:
			if( pxClient->bits.bInRename == pdFALSE )
			{
				pcMyReply = REPL_503;	// "503 Bad sequence of commands.\r\n"
			}
			else
			{
				prvRenameTo( pxClient, pcRestCommand );
			}
			break;

		case ECMD_SITE:	/* Set file permissions */
			if( prvSiteCmd( pxClient, pcRestCommand ) == pdFALSE )
			{
				pcMyReply = REPL_202;
			}
			break;

		case ECMD_DELE:
			prvDeleteFile( pxClient, pcRestCommand );
			break;

		case ECMD_MDTM:
			prvSizeDateFile( pxClient, pcRestCommand, pdTRUE );
			break;

		case ECMD_SIZE:
			if( pxClient->pxWriteHandle != NULL )
			{
				/* This SIZE query is problably about a file which is now being received.
				It so, return the value of pxClient->ulRecvBytes,
				pcRestCommand points to 'pcCommandBuffer', make it free by copying it to pcNewDir. */

				xMakeAbsolute( pxClient, pcNEW_DIR, sizeof pcNEW_DIR, pcRestCommand );

				if( strcmp( pcNEW_DIR, pcRestCommand ) == 0 )
				{
				BaseType_t xCount;
					for( xCount = 0; xCount < 3 && pxClient->pxWriteHandle; xCount++ )
					{
						prvStoreFileWork( pxClient );
					}
					if( pxClient->pxWriteHandle != NULL )
					{
						// File being queried is still open, return number of bytes received until now
						snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "213 %lu\r\n", pxClient->ulRecvBytes );
						pcMyReply = pcCOMMAND_BUFFER;
					} // otherwise, do a normal stat()
				}
				strcpy( pcRestCommand, pcNEW_DIR );
			}
			if( pcMyReply == NULL )
			{
				prvSizeDateFile( pxClient, pcRestCommand, pdFALSE );
			}
			break;
		case ECMD_MKD:
		case ECMD_RMD:
			prvMakeRemoveDir( pxClient, pcRestCommand, pxFTPCommand->ucCommandType == ECMD_RMD );
			break;
		case ECMD_CDUP:
			prvChangeDir( pxClient, ".." );
			break;

		case ECMD_QUIT:
			prvSendReply( pxClient->xSocket, REPL_221, 0 );
			pxClient->bits.bLoggedIn = pdFALSE;
			if( pxClient->xTransferSocket != FREERTOS_NO_SOCKET )
			{
				pxClient->bits1.bClientClosing = pdTRUE;
			}
			break;
		case ECMD_LIST:
		case ECMD_RETR:
		case ECMD_STOR:
			if( pxClient->xTransferSocket == FREERTOS_NO_SOCKET )
			{
				/* Sending "425 Can't open data connection." :
				Before receiving any of these commands, there must have been a
				PORT or PASV command, which causes the creation of a data socket. */
				pcMyReply = REPL_425;
			}
			else
			{
				prvTransferCheck( pxClient );
				switch( pxFTPCommand->ucCommandType )
				{
				case ECMD_LIST:
					prvListSendPrep( pxClient );
					break;
				case ECMD_RETR:
					prvRetrieveFilePrep( pxClient, pcRestCommand );
					break;
				case ECMD_STOR:
					prvStoreFilePrep( pxClient, pcRestCommand );
					break;
				}
			}
			break;

		case ECMD_FEAT:
			{
				static const char pcFeatAnswer[] =
					"211-Features:\x0a"
					" MDTM\x0a"
					" REST STREAM\x0a"
					" SIZE\x0d\x0a"
					"211 End\x0d\x0a";
				pcMyReply = pcFeatAnswer;
			}
			break;

		case ECMD_UNKNOWN:
			FreeRTOS_printf( ("ftp::processCmd: Cmd %s unknown\n", pcRestCommand ) );
			pcMyReply = REPL_500;
			break;
		}
	}
	if( pxFTPCommand->ucCommandType != ECMD_RNFR )
	{
		pxClient->bits.bInRename = pdFALSE;
	}

	if( pcMyReply != NULL )
	{
		xResult = prvSendReply( pxClient->xSocket, pcMyReply, strlen( pcMyReply ) );
	}

	return xResult;
}
/*-----------------------------------------------------------*/

static BaseType_t prvTransferConnect( xFTPClient *pxClient, BaseType_t xDoListen )
{
xSocket_t xSocket;
BaseType_t xResult;

	/* Open a socket for a data connection with the FTP client.
	Happens after a PORT or a PASV command. */

	/* Make sure the previous socket is deleted and flags reset */
	prvTransferCloseSocket( pxClient );

	xSocket = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP );

	if( ( xSocket != FREERTOS_NO_SOCKET ) && ( xSocket != FREERTOS_INVALID_SOCKET ) )
	{
	BaseType_t xSmallTimeout = pdMS_TO_TICKS( 100 );
	struct freertos_sockaddr xAddress;

	#if( ipconfigFTP_TX_BUFSIZE > 0 )
		xWinProperties_t xWinProps;
	#endif
		xAddress.sin_addr = FreeRTOS_GetIPAddress( );	/* Single NIC, currently not used */
		xAddress.sin_port = FreeRTOS_htons( 0 );		/* Bind to aynt available port number */

		FreeRTOS_bind( xSocket, &xAddress, sizeof( xAddress ) );

		#if( ipconfigFTP_TX_BUFSIZE > 0 )
		{
			/* Fill in the buffer and window sizes that will be used by the socket. */
			xWinProps.lTxBufSize = ipconfigFTP_TX_BUFSIZE;
			xWinProps.lTxWinSize = ipconfigFTP_TX_WINSIZE;
			xWinProps.lRxBufSize = ipconfigFTP_RX_BUFSIZE;
			xWinProps.lRxWinSize = ipconfigFTP_RX_WINSIZE;

			/* Set the window and buffer sizes. */
			FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_WIN_PROPERTIES, ( void * ) &xWinProps,	sizeof( xWinProps ) );
		}
		#endif

		FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_RCVTIMEO, ( void * ) &xSmallTimeout, sizeof( BaseType_t ) );
		FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_SNDTIMEO, ( void * ) &xSmallTimeout, sizeof( BaseType_t ) );

		/* The same instance of the socket will be used for the connection and data transport */
		if( xDoListen != pdFALSE )
		{
		BaseType_t xTrueValue = pdTRUE;
			FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_REUSE_LISTEN_SOCKET, ( void * ) &xTrueValue, sizeof( xTrueValue ) );
		}
		pxClient->bits1.bIsListen = xDoListen;
		pxClient->xTransferSocket = xSocket;

		if( xDoListen != pdFALSE )
		{
			FreeRTOS_FD_SET( xSocket, pxClient->pxParent->xSocketSet, eSELECT_EXCEPT | eSELECT_READ );
			/* Calling FreeRTOS_listen( ) */
			xResult = prvTransferStart( pxClient );
			if( xResult >= 0 )
			{
				xResult = pdTRUE;
			}
		}
		else
		{
			FreeRTOS_FD_SET( xSocket, pxClient->pxParent->xSocketSet, eSELECT_EXCEPT | eSELECT_READ | eSELECT_WRITE );
			xResult = pdTRUE;
		}
	}
	else
	{
		FreeRTOS_printf( ( "FreeRTOS_socket() failed\n" ) );
		xResult = -pdFREERTOS_ERRNO_ENOMEM;
	}

	// An active socket (PORT) should connect() later
	return xResult;
}
/*-----------------------------------------------------------*/

static BaseType_t prvTransferStart( xFTPClient *pxClient )
{
BaseType_t xResult;

	/* A transfer socket has been opened, now either call listen() for 'PASV'
	or connect() for the 'PORT' command. */
	if( pxClient->bits1.bIsListen != pdFALSE )
	{
		xResult = FreeRTOS_listen( pxClient->xTransferSocket, 1 );
	}
	else
	{
	struct freertos_sockaddr xAddress;

		xAddress.sin_addr = FreeRTOS_htonl( pxClient->ulClientIP );
		xAddress.sin_port = FreeRTOS_htons( pxClient->usClientPort );
		/* Start an active connection for this data socket */
		xResult = FreeRTOS_connect( pxClient->xTransferSocket, &xAddress, sizeof xAddress );
	}

	return xResult;
}
/*-----------------------------------------------------------*/

static void prvTransferCheck( xFTPClient *pxClient )
{
BaseType_t xRxSize;

	/* A data transfer is busy. Check if there are changes in connectedness. */
	xRxSize = FreeRTOS_rx_size( pxClient->xTransferSocket );

	if( pxClient->bits1.bClientConnected == pdFALSE )
	{
		/* The time to receive a small file can be so short, that we don't even see that
		the socket gets connected and disconnected. Therefore, check the sizeo of the RX buffer. */
		{
		struct freertos_sockaddr xAddress;
		xSocket_t xNexSocket;
		socklen_t xSocketLength = sizeof( xAddress );

			if( pxClient->bits1.bIsListen != pdFALSE )
			{
				xNexSocket = FreeRTOS_accept( pxClient->xTransferSocket, &xAddress, &xSocketLength);
				if( ( ( xNexSocket != FREERTOS_NO_SOCKET ) && ( xNexSocket != FREERTOS_INVALID_SOCKET ) ) ||
					xRxSize > 0 )
				{
					pxClient->bits1.bClientConnected = pdTRUE;
				}
			}
			else
			{
				if( FreeRTOS_issocketconnected( pxClient->xTransferSocket ) > 0 ||
					xRxSize > 0 )
				{
					pxClient->bits1.bClientConnected = pdTRUE;
				}
			}
			if(	pxClient->bits1.bClientConnected != pdFALSE )
			{
				#if( ipconfigHAS_PRINTF != 0 )
				{
					struct freertos_sockaddr xRemoteAddress, xLocalAddress;
					FreeRTOS_GetRemoteAddress( pxClient->xTransferSocket, &xRemoteAddress );
					FreeRTOS_GetLocalAddress( pxClient->xTransferSocket, &xLocalAddress );
					FreeRTOS_printf( ( "%s Connected from %u to %u xRxSize %u\n",
						pxClient->bits1.bIsListen != pdFALSE ? "PASV" : "PORT",
						( unsigned ) FreeRTOS_ntohs( xLocalAddress.sin_port ),
						( unsigned ) FreeRTOS_ntohs( xRemoteAddress.sin_port ),
						( unsigned ) xRxSize ) );
				}
				#endif /* ipconfigHAS_PRINTF */
				FreeRTOS_FD_CLR( pxClient->xTransferSocket, pxClient->pxParent->xSocketSet, eSELECT_WRITE );
				FreeRTOS_FD_SET( pxClient->xTransferSocket, pxClient->pxParent->xSocketSet, eSELECT_READ|eSELECT_EXCEPT );
			}
		}
	}

	if ( pxClient->bits1.bClientConnected != pdFALSE )
	{
		if( pxClient->pcConnectionAck[ 0 ] != '\0' )
		{
		BaseType_t xLength;
		BaseType_t xRemotePort;
		struct freertos_sockaddr xRemoteAddress;

			FreeRTOS_GetRemoteAddress( pxClient->xTransferSocket, &xRemoteAddress );
			xRemotePort = FreeRTOS_ntohs( xRemoteAddress.sin_port );

			/* Tell on the command port 21 we have a data connection */
			xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
				pxClient->pcConnectionAck, pxClient->ulClientIP, xRemotePort );

			prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
			pxClient->pcConnectionAck[ 0 ] = '\0';
		}

		if( ( FreeRTOS_issocketconnected( pxClient->xTransferSocket ) == pdFALSE ) && FreeRTOS_rx_size( pxClient->xTransferSocket ) == 0 )
		{
			#if( ipconfigHAS_PRINTF != 0 )
			{
			struct freertos_sockaddr xRemoteAddress, xLocalAddress;
				FreeRTOS_GetRemoteAddress( pxClient->xTransferSocket, &xRemoteAddress );
				FreeRTOS_GetLocalAddress( pxClient->xTransferSocket, &xLocalAddress );
				FreeRTOS_printf( ( "prvTransferCheck: closing port %u to %u\n",
					FreeRTOS_ntohs( xLocalAddress.sin_port ),
					FreeRTOS_ntohs( xRemoteAddress.sin_port ) ) );
			}
			#endif /* ipconfigHAS_PRINTF != 0 */
			prvTransferCloseSocket( pxClient );
			prvTransferCloseFile( pxClient );
		}
	}
}
/*-----------------------------------------------------------*/

static void prvTransferCloseSocket( xFTPClient *pxClient )
{
	if( pxClient->xTransferSocket != FREERTOS_NO_SOCKET )
	{
		/* DEBUGGING ONLY */
		BaseType_t xRxSize = FreeRTOS_rx_size( pxClient->xTransferSocket );
		if( xRxSize > 0 )
		{
		BaseType_t xRxSize2;
		UBaseType_t xStatus;
			prvStoreFileWork( pxClient );
			xStatus = FreeRTOS_connstatus( pxClient->xTransferSocket );
			xRxSize2 = FreeRTOS_rx_size( pxClient->xTransferSocket );
			FreeRTOS_printf( ( "FTP: WARNING: %s: RX size = %ld -> %ld (%s)\n",
				FreeRTOS_GetTCPStateName( xStatus ),
				xRxSize, xRxSize2, pxClient->pcFileName ) );
			if( xRxSize2 > 1 )
				return;
		}
	}

	if( ( pxClient->pxWriteHandle != NULL ) || ( pxClient->pxReadHandle != NULL ) )
	{
	BaseType_t xLength;
	char pcStrBuf[32];
		xLength = snprintf( pxClient->pcClientAck, sizeof pxClient->pcClientAck,
			"226 Closing connection %d bytes transmitted\r\n", ( int ) pxClient->ulRecvBytes );

		/* Tell on the command socket the data connection is now closed. */
		prvSendReply( pxClient->xSocket, pxClient->pcClientAck, xLength );

		#if( ipconfigHAS_PRINTF != 0 )
		{
			TickType_t xDelta;
			uint32_t ulAverage;
				xDelta = xTaskGetTickCount( ) - pxClient->xStartTime;
				ulAverage = ulGetAverage( pxClient->ulRecvBytes, xDelta );

				FreeRTOS_printf( ("FTP: %s: '%s' %lu Bytes (%s/sec)\n",
					pxClient->pxReadHandle ? "sent" : "recv",
					pxClient->pcFileName,
					pxClient->ulRecvBytes,
					pcMkSize( ulAverage, pcStrBuf, sizeof pcStrBuf ) ) );
		}
		#endif
	}
	if( pxClient->xTransferSocket != FREERTOS_NO_SOCKET )
	{
		FreeRTOS_FD_CLR( pxClient->xTransferSocket, pxClient->pxParent->xSocketSet, eSELECT_ALL );
		FreeRTOS_closesocket( pxClient->xTransferSocket );
		pxClient->xTransferSocket = FREERTOS_NO_SOCKET;
	}
	pxClient->bits1.ulConnFlags = 0ul;
}
/*-----------------------------------------------------------*/

static void prvTransferCloseFile( xFTPClient *pxClient )
{
	if( pxClient->pxWriteHandle != NULL )
	{
		ff_fclose( pxClient->pxWriteHandle );
		pxClient->pxWriteHandle = NULL;
		#if( ipconfigFTP_HAS_RECEIVED_HOOK != 0 )
		{
			vApplicationFTPReceivedHook( pxClient->pcFileName, pxClient->ulRecvBytes, pxClient );
		}
		#endif

	}
	if( pxClient->pxReadHandle != NULL )
	{
		ff_fclose( pxClient->pxReadHandle );
		pxClient->pxReadHandle = NULL;
	}
	/* These two field are only used for logging / file-statistics */
	pxClient->ulRecvBytes = 0ul;
	pxClient->xStartTime = 0ul;
}
/*-----------------------------------------------------------*/

/**
 * Guess the transfer type, given the client requested type.
 * Actually in unix there is no difference between binary and
 * ascii mode when we work with file descriptors.
 * If #type is not recognized as a valid client request, -1 is returned.
 */
static BaseType_t prvGetTransferType( const char *pcType )
{
BaseType_t xResult = -1;

	if( pcType != NULL )
	{
		BaseType_t xLength = strlen( pcType );
		if( xLength == 0 )
		{
			return -1;
		}
		switch( pcType[ 0 ] ) {
		case 'I':
			xResult = TMODE_BINARY;
			break;
		case 'A':
			xResult = TMODE_ASCII;
			break;
		case 'L':
			if( xLength >= 3 )
			{
				if( pcType[ 2 ] == '7' )
				{
					xResult = TMODE_7BITS;
				}
				else if( pcType[ 2 ] == '8' )
				{
					xResult = TMODE_7BITS;
				}
			}
			break;
		}
	}
	return xResult;
}
/*-----------------------------------------------------------*/

#if( ipconfigHAS_PRINTF != 0 )
	#define SIZE_1_GB	( 1024ul * 1024ul * 1024ul )
	#define SIZE_1_MB	( 1024ul * 1024ul )
	#define SIZE_1_KB	( 1024ul )

	static const char *pcMkSize( uint32_t ulAmount, char *pcBuffer, BaseType_t xBufferSize )
	{
	uint32_t ulGB, ulMB, ulKB, ulByte;

		ulGB = ( ulAmount / SIZE_1_GB );
		ulAmount -= ( ulGB * SIZE_1_GB );
		ulMB = ( ulAmount / SIZE_1_MB );
		ulAmount -= ( ulMB * SIZE_1_MB );
		ulKB = ( ulAmount / SIZE_1_KB );
		ulAmount -= ( ulKB * SIZE_1_KB );
		ulByte = ( ulAmount );

		if (ulGB != 0ul )
		{
			snprintf( pcBuffer, xBufferSize, "%lu.%02lu GB", ulGB, (100 * ulMB) / SIZE_1_KB );
		}
		else if( ulMB != 0ul )
		{
			snprintf( pcBuffer, xBufferSize, "%lu.%02lu MB", ulMB, (100 * ulKB) / SIZE_1_KB );
		}
		else if( ulKB != 0ul )
		{
			snprintf(pcBuffer, xBufferSize, "%lu.%02lu KB", ulKB, (100 * ulByte) / SIZE_1_KB );
		}
		else
		{
			snprintf( pcBuffer, xBufferSize, "%lu bytes", ulByte );
		}

		return pcBuffer;
	}
	/*-----------------------------------------------------------*/
#endif /* ipconfigHAS_PRINTF != 0 */

#if( ipconfigHAS_PRINTF != 0 )
	static uint32_t ulGetAverage( uint32_t ulAmount, TickType_t xDeltaMs )
	{
	uint32_t ulAverage;

		/* Get the average amount of bytes per seconds. Ideally this is calculated by
		Multiplying with 1000 and dividing by milliseconds:
			ulAverage = ( 1000ul * ulAmount ) / xDeltaMs;
		Now get a maximum precision, while avoiding an arithmetic overflow:
		*/
		if( xDeltaMs == 0ul )
		{
			/* Time is zero, there is no average  */
			ulAverage = 0ul;
		}
		else if( ulAmount >= ( ~0ul / 10ul ) )
		{
			/* More than 409 MB has been transferred, do not multiply */
			ulAverage = ( ulAmount / ( xDeltaMs / 1000ul ) );
		}
		else if( ulAmount >= ( ~0ul / 100ul ) )
		{
			/* Between 409 and 41 MB has been transferred, can multiply by 10 */
			ulAverage = ( ( ulAmount * 10ul ) / ( xDeltaMs / 100ul ) );
		}
		else if( ulAmount >= ( ~0ul / 1000ul ) )
		{
			/* Between 4.1 MB and 41 has been transferred, can multiply by 100 */
			ulAverage = ( ( ulAmount * 100ul ) / ( xDeltaMs / 10ul ) );
		}
		else
		{
			/* Less than 4.1 MB: can multiply by 1000 */
			ulAverage = ( ( ulAmount * 1000ul ) / xDeltaMs );
		}

		return ulAverage;
	}
	/*-----------------------------------------------------------*/
#endif /* ipconfigHAS_PRINTF != 0 */

static BaseType_t prvParsePortData( const char *pcCommand, uint32_t *pulIPAddress )
{
unsigned h1, h2, h3, h4, p1, p2;
char sep;

	if (sscanf (pcCommand, "%u%c%u%c%u%c%u%c%u%c%u", &h1, &sep, &h2, &sep, &h3, &sep, &h4, &sep, &p1, &sep, &p2) != 11)
		return -1;

	// Very big endian
	*pulIPAddress = (h1 << 24) | (h2 << 16)  | (h3 << 8)  | h4;
	return (p1 << 8) | p2;
}
/*-----------------------------------------------------------*/

/*

 ####                                  #######   #   ###
#    #   #                              #   ##   #     #
#    #   #                              #    #         #
#      ######  ####  ### ##   ####      #   #  ###     #    ####
 ##      #    #    #  # #  # #    #     #####    #     #   #    #
   ##    #    #    #  ##   # ######     #   #    #     #   ######
#    #   #    #    #  #      #          #        #     #   #
#    #   # ## #    #  #      #   ##     #        #     #   #   ##
 ####     ##   ####  ####     ####     ####    ##### #####  ####

*/

static BaseType_t prvStoreFilePrep( xFTPClient *pxClient, char *pcFileName )
{
BaseType_t xResult;
FF_FILE *pxNewHandle;
size_t ulFileSize = 0ul;
int iErrorNo;

	prvTransferCloseFile( pxClient );	// Close previous handle (if any) and reset file transfer parameters

	xMakeAbsolute( pxClient, pxClient->pcFileName, sizeof pxClient->pcFileName, pcFileName );

	pxNewHandle = NULL;

	if( pxClient->ulRestartOffset != 0 )
	{
	size_t ulOffset = pxClient->ulRestartOffset;
	int32_t lRc;

		pxClient->ulRestartOffset = 0ul; // Only use 1 time
		pxNewHandle = ff_fopen( pxClient->pcFileName, "ab" );

		if( pxNewHandle != NULL )
		{
			ulFileSize = pxNewHandle->ulFileSize;

			if( ulOffset <= ulFileSize )
			{
				lRc = ff_fseek( pxNewHandle, ulOffset, FF_SEEK_SET );
			}
			else
			{
				/* Won't even try to seek after EOF */
				lRc = -pdFREERTOS_ERRNO_EINVAL;
			}
			if( lRc != 0 )
			{
			BaseType_t xLength;

				xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
					"450 Seek invalid %d length %d\r\n",
					( int ) ulOffset, ( int ) ulFileSize );
				prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );	// "Requested file action not taken"

				FreeRTOS_printf( ( "ftp::storeFile: create %s: Seek %d length %d\n",
					pxClient->pcFileName, ( int ) ulOffset, ( int ) ulFileSize ) );

				ff_fclose( pxNewHandle );
				pxNewHandle = NULL;
			}
		}
	}
	else
	{
		pxNewHandle = ff_fopen( pxClient->pcFileName, "wb" );
	}

	if( pxNewHandle == NULL )
	{
		iErrorNo = stdioGET_ERRNO();
		if( iErrorNo == pdFREERTOS_ERRNO_ENOSPC )
		{
			prvSendReply( pxClient->xSocket, REPL_552, 0 );
		}
		else
		{
			prvSendReply( pxClient->xSocket, REPL_450, 0 );	// "Requested file action not taken"
		}
		FreeRTOS_printf( ( "ftp::storeFile: create %s: %s (errno %d)\n",
			pxClient->pcFileName,
			( const char* ) strerror( iErrorNo ), iErrorNo ) );

		xResult = pdFALSE;
	}
	else
	{
		if( pxClient->bits1.bIsListen )
		{ // True if PASV is used
			snprintf( pxClient->pcConnectionAck, sizeof pxClient->pcConnectionAck,
				"150 Accepted data connection from %%xip:%%u\r\n" );
			prvTransferCheck( pxClient );
		}
		else
		{
		BaseType_t xLength;

			xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "150 Opening BIN connection to store file\r\n" );
			prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
			pxClient->pcConnectionAck[ 0 ] = '\0';
			prvTransferStart( pxClient ); // Now active connect
		}

		pxClient->pxWriteHandle = pxNewHandle;

		/* To get some statistics about the performance. */
		pxClient->xStartTime = xTaskGetTickCount( );

		xResult = pdTRUE;
	}

	return xResult;
}
/*-----------------------------------------------------------*/

extern const char * const tcp_state_str[11];

static BaseType_t prvStoreFileWork( xFTPClient *pxClient )
{
BaseType_t xRc;

	/* Read from the data socket until all has been read or until a negative value
	is returned. */
	for( ; ; )
	{
	char *pcBuffer;

		/* The "zero-copy" method: */
		xRc = FreeRTOS_recv( pxClient->xTransferSocket, ( void * ) &pcBuffer,
			0x20000u, FREERTOS_ZERO_COPY | FREERTOS_MSG_DONTWAIT );
		if( xRc <= 0 )
		{
			break;
		}
		pxClient->ulRecvBytes += xRc;
		ff_fwrite( pcBuffer, 1, xRc, pxClient->pxWriteHandle );
		FreeRTOS_recv( pxClient->xTransferSocket, ( void * ) NULL, xRc, 0 );
	}
	return xRc;
}
/*-----------------------------------------------------------*/

/*
######                          #                           #######   #   ###
 #    #          #              #                            #   ##   #     #
 #    #          #                                           #    #         #
 #    #  ####  ###### ### ##  ###    ####  #    #  ####      #   #  ###     #    ####
 ###### #    #   #     # #  #   #   #    # #    # #    #     #####    #     #   #    #
 #  ##  ######   #     ##   #   #   ###### #    # ######     #   #    #     #   ######
 #   #  #        #     #        #   #      #    # #          #        #     #   #
 #    # #   ##   # ##  #        #   #   ##  #  #  #   ##     #        #     #   #   ##
###  ##  ####     ##  ####    #####  ####    ##    ####     ####    ##### #####  ####
*/
static BaseType_t prvRetrieveFilePrep( xFTPClient *pxClient, char *pcFileName )
{
BaseType_t xResult = pdTRUE;

	prvTransferCloseFile( pxClient );	/* Close previous handle (if any) and reset file transfer parameters */

	xMakeAbsolute( pxClient, pxClient->pcFileName, sizeof pxClient->pcFileName, pcFileName );

	pxClient->pxReadHandle = ff_fopen( pxClient->pcFileName, "rb" );
	if( pxClient->pxReadHandle == NULL )
	{
		prvSendReply( pxClient->xSocket, REPL_450, 0 );	// "Requested file action not taken"
		FreeRTOS_printf( ("prvRetrieveFilePrep: open %s: %s\n", pxClient->pcFileName, ( const char * ) strerror( stdioGET_ERRNO() ) ) );
		xResult = pdFALSE;
	}
	else
	{
	size_t ulFileSize;

		ulFileSize = pxClient->pxReadHandle->ulFileSize;
		pxClient->xBytesLeft = ulFileSize;
		if( pxClient->ulRestartOffset != 0ul )
		{
		size_t ulOffset = pxClient->ulRestartOffset;
		int32_t iRc;

			pxClient->ulRestartOffset = 0; // Only use 1 time

			if( ulOffset < ulFileSize )
			{
				iRc = ff_fseek( pxClient->pxReadHandle, ulOffset, FF_SEEK_SET );
			}
			else
			{
				iRc = -pdFREERTOS_ERRNO_EINVAL;
			}
			if( iRc != 0 )
			{
			BaseType_t xLength;

				xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
					"450 Seek invalid %d length %d\r\n", ( int ) ulOffset, ( int ) ulFileSize );
				prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );	/* "Requested file action not taken". */

				FreeRTOS_printf( ( "prvRetrieveFilePrep: create %s: Seek %d length %d\n",
					pxClient->pcFileName, ( int ) ulOffset, ( int ) ulFileSize ) );

				ff_fclose( pxClient->pxReadHandle );
				pxClient->pxReadHandle = NULL;
				xResult = pdFALSE;
			}
			else
			{
				pxClient->xBytesLeft = ulFileSize - pxClient->ulRestartOffset;
			}
		}
	}
	if( xResult != pdFALSE )
	{
		if( pxClient->bits1.bIsListen != pdFALSE )
		{ // True if PASV is used
			snprintf( pxClient->pcConnectionAck, sizeof pxClient->pcConnectionAck,
				"150%cAccepted data connection from %%xip:%%u\r\n%s",
				pxClient->xTransType == TMODE_ASCII ? '-' : ' ',
				pxClient->xTransType == TMODE_ASCII ? "150 NOTE: ASCII mode requested, but binary mode used\r\n" : "" );
		} else {
		BaseType_t xLength;

			xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "150%cOpening data connection to %lxip:%u\r\n%s",
				pxClient->xTransType == TMODE_ASCII ? '-' : ' ',
				pxClient->ulClientIP,
				pxClient->usClientPort,
				pxClient->xTransType == TMODE_ASCII ? "150 NOTE: ASCII mode requested, but binary mode used\r\n" : "" );
			prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
			pxClient->pcConnectionAck[ 0 ] = '\0';
			prvTransferStart( pxClient );
		}

		// Prepare the ACK which will be sent when all data has been sent
		snprintf( pxClient->pcClientAck, sizeof pxClient->pcClientAck, "%s", REPL_226 );

		/* To get some statistics about the performance. */
		pxClient->xStartTime = xTaskGetTickCount( );
	}

	return xResult;
}
/*-----------------------------------------------------------*/

static BaseType_t prvRetrieveFileWork( xFTPClient *pxClient )
{
size_t xSpace;
size_t xCount, xItemsRead;
BaseType_t xRc = 0;
BaseType_t xSetEvent = pdFALSE;

	do
	{
		/* Take the lesser of the two: tx_space (number of bytes that can be queued for
		transmission) and xBytesLeft (the number of bytes left to read from the file) */
		xSpace = FreeRTOS_tx_space( pxClient->xTransferSocket );

		if( xSpace == 0 )
		{
			FreeRTOS_FD_SET( pxClient->xTransferSocket, pxClient->pxParent->xSocketSet, eSELECT_WRITE | eSELECT_EXCEPT );
			xRc = FreeRTOS_select( pxClient->pxParent->xSocketSet, 200 );
			xSpace = FreeRTOS_tx_space( pxClient->xTransferSocket );
		}

		if( pxClient->xBytesLeft < xSpace )
		{
			xCount = pxClient->xBytesLeft;
		}
		else
		{
			xCount = xSpace;
		}

		if( xCount <= 0 )
		{
			break;
		}

		if( xCount > sizeof pcFILE_BUFFER )
		{
			xCount = sizeof pcFILE_BUFFER;
		}
		/*
		size_t fread( void *buffer, size_t size, size_t count, FILE *stream );
		*/
		xItemsRead = ff_fread( pcFILE_BUFFER, 1, xCount, pxClient->pxReadHandle );
		if( xItemsRead != xCount )
		{
			FreeRTOS_printf( ( "prvRetrieveFileWork: Got %d Expected %d\n", ( int )xItemsRead, ( int ) xCount ) );
			xRc = FreeRTOS_shutdown( pxClient->xTransferSocket, FREERTOS_SHUT_RDWR );
			pxClient->xBytesLeft = 0;
			break;
		}
		pxClient->xBytesLeft -= xCount;

		if( pxClient->xBytesLeft == 0 )
		{
		BaseType_t xTrueValue = 1;

			FreeRTOS_setsockopt( pxClient->xTransferSocket, 0, FREERTOS_SO_CLOSE_AFTER_SEND, ( void * ) &xTrueValue, sizeof( xTrueValue ) );
		}

		xRc = FreeRTOS_send( pxClient->xTransferSocket, pcFILE_BUFFER, xCount, 0 );
		if( xRc < 0 )
		{
			break;
		}

		pxClient->ulRecvBytes += xRc;
		if( pxClient->xBytesLeft == 0 )
		{
			break;
		}
	} while( xCount > 0 );

	if( xRc < 0 )
	{
		FreeRTOS_printf( ( "prvRetrieveFileWork: already disconnected\n" ) );
	}
	else if( pxClient->xBytesLeft <= 0 )
	{
	BaseType_t x;

		for( x = 0; x < 5; x++ )
		{
			xRc = FreeRTOS_recv( pxClient->xTransferSocket, pxClient->pxParent->pcFileBuffer, sizeof pxClient->pxParent->pcFileBuffer, 0 );
			if( xRc < 0 )
			{
				break;
			}
		}
//		FreeRTOS_printf( ( "prvRetrieveFileWork: %s all sent: xRc %ld\n", pxClient->pcFileName, xRc ) );
	}
	else
	{
		FreeRTOS_FD_SET( pxClient->xTransferSocket, pxClient->pxParent->xSocketSet, eSELECT_WRITE );
		xSetEvent = pdTRUE;
	}
	if( xSetEvent == pdFALSE )
	{
		FreeRTOS_FD_CLR( pxClient->xTransferSocket, pxClient->pxParent->xSocketSet, eSELECT_WRITE );
	}
	return xRc;
}
/*-----------------------------------------------------------*/

/*
###     #####  ####  #####
 #        #   #    # # # #
 #        #   #    #   #
 #        #   #        #
 #        #    ##      #
 #    #   #      ##    #
 #    #   #   #    #   #
 #    #   #   #    #   #
####### #####  ####   ####
*/
static BaseType_t prvListSendPrep( xFTPClient *pxClient )	/* Prepare sending a directory LIST */
{
BaseType_t xFindResult;
int iErrorNo;

	if( pxClient->bits1.bIsListen != pdFALSE )
	{
		/* True if PASV is used */
		snprintf( pxClient->pcConnectionAck, sizeof( pxClient->pcConnectionAck ),
			"150 Accepted data connection from %%xip:%%u\r\n" );
	}
	else
	{
	BaseType_t xLength;

		/* Here the FTP server is supposed to connect() */
		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
			"150 Opening ASCII mode data connection to for /bin/ls \r\n" );

		prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
		/* Clear the current connection acknowledge message */
		pxClient->pcConnectionAck[ 0 ] = '\0';
		prvTransferStart( pxClient );
	}

	pxClient->xDirCount = 0;
	xMakeAbsolute( pxClient, pcNEW_DIR, sizeof( pcNEW_DIR ), pxClient->pcCurrentDir );

	xFindResult = ff_findfirst( pcNEW_DIR, &pxClient->xFindData );

	pxClient->bits1.bDirHasEntry = ( xFindResult >= 0 );

	iErrorNo = stdioGET_ERRNO();
	if( ( xFindResult < 0 ) && ( iErrorNo == pdFREERTOS_ERRNO_ENOENT ) )
	{
		FreeRTOS_printf( ("prvListSendPrep: Empty directory? (%s)\n", pxClient->pcCurrentDir ) );
		prvSendReply( pxClient->xTransferSocket, "total 0\r\n", 0 );
		pxClient->xDirCount++;
	}
	else if( xFindResult < 0 )
	{
		FreeRTOS_printf( ( "prvListSendPrep: rc = %ld iErrorNo = %d\n", xFindResult, iErrorNo ) );
		prvSendReply( pxClient->xSocket, REPL_451, 0 );
	}
	else
	{
		FreeRTOS_printf( ( "prvListSendPrep: Has entry, connected %d\n",
			( int ) FreeRTOS_issocketconnected( pxClient->xTransferSocket ) ) );
	}
	pxClient->pcClientAck[ 0 ] = '\0';

	return pxClient->xDirCount;
}
/*-----------------------------------------------------------*/

#define	MAX_DIR_LIST_ENTRY_SIZE		256

static BaseType_t prvListSendWork( xFTPClient *pxClient )
{
BaseType_t xTxSpace;

	while( pxClient->bits1.bClientConnected != pdFALSE )
	{
	char *pcWritePtr = pcCOMMAND_BUFFER;
	BaseType_t xWriteLength;

		xTxSpace = FreeRTOS_tx_space( pxClient->xTransferSocket );

		if( xTxSpace > sizeof( pcCOMMAND_BUFFER ) )
		{
			xTxSpace = sizeof( pcCOMMAND_BUFFER );
		}
		FreeRTOS_printf( ( "prvListSendWork: conn %d entry %d tx space %ld\n",
			pxClient->bits1.bClientConnected, pxClient->bits1.bDirHasEntry, xTxSpace ) );

		while( ( xTxSpace >= MAX_DIR_LIST_ENTRY_SIZE ) && ( pxClient->bits1.bDirHasEntry != pdFALSE ) )
		{
		BaseType_t xLength, xEndOfDir;
		int32_t iRc;
		int iErrorNo;

			xLength = prvGetFileInfoStat( &( pxClient->xFindData.xDirectoryEntry ), pcWritePtr, xTxSpace );

			pxClient->xDirCount++;
			pcWritePtr += xLength;
			xTxSpace -= xLength;

			iRc = ff_findnext( &pxClient->xFindData );
			iErrorNo = stdioGET_ERRNO();

			xEndOfDir = ( iRc < 0 ) && ( iErrorNo == pdFREERTOS_ERRNO_ENOENT );

			pxClient->bits1.bDirHasEntry = ( xEndOfDir == pdFALSE ) && ( iRc >= 0 );

			if( ( iRc < 0 ) && ( xEndOfDir == pdFALSE ) )
			{
				FreeRTOS_printf( ("prvListSendWork: %s (rc %08x)\n",
					( const char * ) strerror( iErrorNo ),
					( unsigned )iRc ) );
			}
		}
		xWriteLength = ( BaseType_t ) ( pcWritePtr - pcCOMMAND_BUFFER );

		if( xWriteLength == 0 )
		{
			break;
		}

		if( pxClient->bits1.bDirHasEntry == pdFALSE )
		{
		uint32_t totalCount;
		unsigned long freeCount;
		unsigned perc;
		char freeSpace[40];

			pxClient->bits1.bClientPrepared = pdTRUE;	// Will wait for all data to be sent and acked
			totalCount = 1;
			freeCount = ff_diskfree( pxClient->pcCurrentDir, &totalCount );
			perc = ( unsigned ) ( ( 100ULL * freeCount + totalCount / 2 ) / totalCount );

			snprintf( freeSpace, sizeof freeSpace, "Total %lu KB (%u %% free)",
				totalCount /1024, perc );

			// Prepare the ACK which will be sent when all data has been sent
			snprintf( pxClient->pcClientAck, sizeof pxClient->pcClientAck,
				"226-Options: -l\r\n"
				"226-%ld matches total\r\n"
				"226 %s\r\n",
				pxClient->xDirCount, freeSpace );
		}

		if( xWriteLength )
		{
			if( pxClient->bits1.bDirHasEntry == pdFALSE )
			{
			BaseType_t xTrueValue = 1;

				FreeRTOS_setsockopt( pxClient->xTransferSocket, 0, FREERTOS_SO_CLOSE_AFTER_SEND, ( void * ) &xTrueValue, sizeof( xTrueValue ) );
			}

			prvSendReply( pxClient->xTransferSocket, pcCOMMAND_BUFFER, xWriteLength );
		}
		if( pxClient->bits1.bDirHasEntry == pdFALSE )
		{
			prvSendReply( pxClient->xSocket, pxClient->pcClientAck, 0 );
			break;
		}
		xTxSpace = FreeRTOS_tx_space( pxClient->xTransferSocket );
	}	/* while( pxClient->bits1.bClientConnected )  */

	return 0;
}
/*-----------------------------------------------------------*/

static const char *pcMonthAbbrev( BaseType_t xMonth )
{
static const char pcMonthList[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

	if( xMonth < 1 || xMonth > 12 )
		xMonth = 12;

	return pcMonthList + 3 * ( xMonth - 1 );
};
/*-----------------------------------------------------------*/

static BaseType_t prvGetFileInfoStat( FF_DirEnt_t *pxEntry, char *pcLine, BaseType_t xMaxLength )
{
	char date[16];
	char mode[11]	= "----------";
	BaseType_t st_nlink = 1;
	const char user[9] = "freertos";
	const char group[8] = "plusfat";

/*
 *	Creates a unix-style listing, understood by most FTP clients:
 *
 * -rw-rw-r--   1 freertos FreeRTOS+FAT 10564588 Sep 01 00:17 03.  Metaharmoniks - Star (Instrumental).mp3
 * -rw-rw-r--   1 freertos FreeRTOS+FAT 19087839 Sep 01 00:17 04.  Simon Le Grec - Dimitri (Wherever U Are) (Cosmos Mix).mp3
 * -rw-rw-r--   1 freertos FreeRTOS+FAT 11100621 Sep 01 00:16 05.  D-Chill - Mistake (feat. Katy Blue).mp3
 */

	#if ( ffconfigTIME_SUPPORT == 1 )
		const FF_SystemTime_t *pxCreateTime = &( pxEntry->xCreateTime );
	#else
	#warning Do not use this.
		FF_SystemTime_t xCreateTime;
		const FF_SystemTime_t *pxCreateTime = &xCreateTime;
	#endif
	size_t ulSize = pxEntry->ulFileSize;
	const char *pcFileName = pxEntry->pcFileName;

	mode[0] = ( ( pxEntry->ucAttrib & FF_FAT_ATTR_DIR ) != 0 ) ? 'd' : '-';
	#if( ffconfigDEV_SUPPORT != 0 ) && ( configFTP_USES_FULL_FAT != 0 )
	{
		if( ( pxEntry->ucAttrib & FF_FAT_ATTR_DIR ) == 0 )
		{
			switch( pxEntry->ucIsDeviceDir )
			{
			case FF_DEV_CHAR_DEV:
				mode[0] = 'c';
				break;
			case FF_DEV_BLOCK_DEV:
				mode[0] = 'b';
				break;
			}
		}
	}
	#endif /* ffconfigDEV_SUPPORT != 0 */

	mode[1] = 'r';	// owner
	mode[2] = ( ( pxEntry->ucAttrib & FF_FAT_ATTR_READONLY ) != 0 ) ? '-' : 'w';
	mode[3] = '-';	// x for executable

	mode[4] = 'r';	// group
	mode[5] = ( ( pxEntry->ucAttrib & FF_FAT_ATTR_READONLY ) != 0 ) ? '-' : 'w';
	mode[6] = '-';	// x for executable

	mode[7] = 'r';	// world
	mode[8] = '-';
	mode[9] = '-';	// x for executable

	if( pxCreateTime->Month && pxCreateTime->Day )
	{
		snprintf( date, sizeof date, "%-3.3s %02d %02d:%02d",
			pcMonthAbbrev( pxCreateTime->Month ),
			pxCreateTime->Day,
			pxCreateTime->Hour,
			pxCreateTime->Minute );
	}
	else
	{
		snprintf (date, sizeof date, "Jan 01 1970");
	}
	return snprintf( pcLine, xMaxLength, "%s %3ld %-4s %-4s %8d %12s %s\r\n",
		mode, st_nlink, user, group, ( int ) ulSize, date, pcFileName );
}
/*-----------------------------------------------------------*/

/*
  ####  #     # #####
 #    # #     #  #   #
#     # #     #  #    #
#       #  #  #  #    #
#       #  #  #  #    #
#       #  #  #  #    #
#     #  ## ##   #    #
 #    #  ## ##   #   #
  ####   ## ##  #####
*/
static BaseType_t prvChangeDir( xFTPClient *pxClient, char *pcDirectory )
{
BaseType_t xResult;
BaseType_t xIsRootDir, xLength, xValid;

	if( pcDirectory[0] == '.' )
	{
		strcpy( pcNEW_DIR, pxClient->pcCurrentDir );

		if( pcDirectory[1] == '.' )
		{
			char *p = strrchr( pcNEW_DIR, '/' );
			if( p != NULL )
			{
				if( p == pcNEW_DIR )
				{
					p[1] = '\0';
				}
				else
				{
					p[0] = '\0';
				}
			}
		}
	}
	else
	{
		if(pcDirectory[0] != '/' )
		{
		BaseType_t xCurLength;

			xCurLength = strlen( pxClient->pcCurrentDir );
			snprintf( pcNEW_DIR, sizeof( pcNEW_DIR ), "%s%s%s",
				pxClient->pcCurrentDir,
				pxClient->pcCurrentDir[ xCurLength - 1 ] == '/' ? "" : "/",
				pcDirectory );
		}
		else
		{
			snprintf( pcNEW_DIR, sizeof pcNEW_DIR, "%s", pcDirectory );
		}
	}

	xIsRootDir = ( pcNEW_DIR[0] == '/' ) && ( pcNEW_DIR[1] == '\0' );
	xMakeAbsolute( pxClient, pcNEW_DIR, sizeof( pcNEW_DIR ), pcNEW_DIR );

	if( ( ( xIsRootDir == pdFALSE ) || ( FF_FS_Count() == 0 ) ) &&	( ff_finddir( pcNEW_DIR ) == pdFALSE ) )
	{
		xValid = pdFALSE;
	}
	else
	{
		xValid = pdTRUE;
	}

	if( xValid == pdFALSE )
	{
		// Get the directory cluster, if it exists.
		FreeRTOS_printf( ("FTP: chdir \"%s\": No such dir\n", pcNEW_DIR ) );
		//#define REPL_550 "550 Requested action not taken.\r\n"
		//550 /home/hein/arch/h8300: No such file or directory
//		prvSendReply( pxClient->xSocket, REPL_451, 0 );
		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
			"550 %s: No such file or directory\r\n",
			pcNEW_DIR );
		prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
		xResult = pdFALSE;
	}
	else
	{
		memcpy( pxClient->pcCurrentDir, pcNEW_DIR, sizeof pxClient->pcCurrentDir );

		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "250 Changed to %s\r\n", pcNEW_DIR );
		prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
		xResult = pdTRUE;
	}

	return xResult;
}
/*-----------------------------------------------------------*/

/*
######  ##    # ####### ######
 #    # ##    #  #   ##  #    #
 #    # ##    #  #    #  #    #
 #    # ###   #  #   #   #    #
 ###### # ##  #  #####   ######
 #  ##  #  ## #  #   #   #  ##
 #   #  #   ###  #       #   #
 #    # #    ##  #       #    #
###  ## #    ## ####    ###  ##
*/
static BaseType_t prvRenameFrom( xFTPClient *pxClient, const char *pcFileName )
{
const char *myReply;
FF_FILE *fh;

	xMakeAbsolute( pxClient, pxClient->pcFileName, sizeof pxClient->pcFileName, pcFileName );

	myReply = NULL;

	fh = ff_fopen( pxClient->pcFileName, "rb" );

	if( fh != NULL )
	{
		ff_fclose( fh );
		// REPL_350;	// "350 Requested file action pending further information.\r\n"
		snprintf( pcCOMMAND_BUFFER, sizeof pcCOMMAND_BUFFER,
			"350 Rename '%s' ...\r\n", pxClient->pcFileName );
		myReply = pcCOMMAND_BUFFER;
		pxClient->bits.bInRename = pdTRUE;
	}
	else if( stdioGET_ERRNO() == pdFREERTOS_ERRNO_EISDIR )
	{
		snprintf( pcCOMMAND_BUFFER, sizeof pcCOMMAND_BUFFER,
			"350 Rename directory '%s' ...\r\n", pxClient->pcFileName );
		myReply = pcCOMMAND_BUFFER;
		pxClient->bits.bInRename = pdTRUE;
	}
	else
	{
		FreeRTOS_printf( ("ftp::renameFrom[%s]\n%s\n", pxClient->pcFileName, strerror( stdioGET_ERRNO() ) ) );
		myReply = REPL_451;		// "451 Requested action aborted. Local error in processing.\r\n"
	}
	if( myReply )
	{
		prvSendReply( pxClient->xSocket, myReply, 0 );
	}

	return pdTRUE;
}
/*-----------------------------------------------------------*/

/*
######  ##    # #####   ###
 #    # ##    # # # #  ## ##
 #    # ##    #   #   ##   ##
 #    # ###   #   #   #     #
 ###### # ##  #   #   #     #
 #  ##  #  ## #   #   #     #
 #   #  #   ###   #   ##   ##
 #    # #    ##   #    ## ##
###  ## #    ##  ####   ###
*/
static BaseType_t prvRenameTo( xFTPClient *pxClient, const char *pcFileName )
{
const char *myReply = NULL;
int iResult;

	xMakeAbsolute( pxClient, pcNEW_DIR, sizeof pcNEW_DIR, pcFileName );

	/* FreeRTOS+FAT rename has an extra parameter: "remove target if already exists" */
	iResult = ff_rename( pxClient->pcFileName, pcNEW_DIR, pdFALSE );

	if( iResult < 0 )
	{
		iResult = stdioGET_ERRNO();
	}
	else
	{
		iResult = 0;
	}

	switch( iResult )
	{
	case 0:
		FreeRTOS_printf( ( "ftp::renameTo[%s,%s]: Ok\n", pxClient->pcFileName, pcNEW_DIR ) );
		snprintf( pcCOMMAND_BUFFER, sizeof pcCOMMAND_BUFFER,
			"250 Rename successful to '%s'\r\n", pcNEW_DIR );
		myReply = pcCOMMAND_BUFFER;
		break;
	case pdFREERTOS_ERRNO_EEXIST:
		/* the destination file already exists.
		"450 Requested file action not taken.\r\n"*/
		snprintf( pcCOMMAND_BUFFER, sizeof pcCOMMAND_BUFFER,
			"450 Already exists '%s'\r\n", pcNEW_DIR );
		myReply = pcCOMMAND_BUFFER;
		break;
	case pdFREERTOS_ERRNO_EIO:	/* FF_ERR_FILE_COULD_NOT_CREATE_DIRENT */
		/* if dirent creation failed (fatal error!).
		"553 Requested action not taken.\r\n" */
		FreeRTOS_printf( ("ftp::renameTo[%s,%s]: Error creating DirEnt\n",
			pxClient->pcFileName, pcNEW_DIR ) );
		myReply = REPL_553;
		break;
	case pdFREERTOS_ERRNO_ENOENT:
		/* if the source file was not found.
		"450 Requested file action not taken.\r\n" */
		snprintf( pcCOMMAND_BUFFER, sizeof pcCOMMAND_BUFFER,
			"450 No such file '%s'\r\n", pxClient->pcFileName );
		myReply = pcCOMMAND_BUFFER;
		break;
	default:
		FreeRTOS_printf( ("ftp::renameTo[%s,%s]: %s\n", pxClient->pcFileName, pcNEW_DIR,
			(const char*)strerror( stdioGET_ERRNO() ) ) );
		myReply = REPL_451;	// "451 Requested action aborted. Local error in processing.\r\n"
		break;
	}
	prvSendReply( pxClient->xSocket, myReply, 0 );

	return pdTRUE;
}
/*-----------------------------------------------------------*/

/*
 ####    #
#    #   #     #
#    #         #
#      ###   ######  ####
 ##      #     #    #    #
   ##    #     #    ######
#    #   #     #    #
#    #   #     # ## #   ##
 ####  #####    ##   ####
*/
static BaseType_t prvSiteCmd( xFTPClient *pxClient, char *pcRestCommand )
{
	( void ) pxClient;
	( void ) pcRestCommand;

	return 0;
}
/*-----------------------------------------------------------*/

/*
#####          ###
 #   #           #            #
 #    #          #            #
 #    #  ####    #    ####  ######  ####
 #    # #    #   #   #    #   #    #    #
 #    # ######   #   ######   #    ######
 #    # #        #   #        #    #
 #   #  #   ##   #   #   ##   # ## #   ##
#####    ####  #####  ####     ##   ####
*/
static BaseType_t prvDeleteFile( xFTPClient *pxClient, char *pcFileName )
{
BaseType_t xResult, xLength;
int32_t iRc;
int iErrorNo;

	/* DELE: Delete a file. */
	xMakeAbsolute( pxClient, pxClient->pcFileName, sizeof( pxClient->pcFileName ), pcFileName );

	iRc = ff_remove( pxClient->pcFileName );

	if (iRc >= 0 )
	{
		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
			"250 File \"%s\" removed\r\n", pxClient->pcFileName );
		xResult = pdTRUE;
	}
	else
	{
		const char *errMsg = "other error";

		iErrorNo = stdioGET_ERRNO();
		switch( iErrorNo )
		{
		case pdFREERTOS_ERRNO_ENOENT:	errMsg = "No such file"; break;		//-31	///< File was not found.
		case pdFREERTOS_ERRNO_EALREADY:	errMsg = "File still open"; break;	//-30	///< File is in use.
		case pdFREERTOS_ERRNO_EISDIR:	errMsg = "Is a dir"; break;			//-32	///< Tried to FF_Open() a Directory.
		case pdFREERTOS_ERRNO_EROFS:	errMsg = "Read-only"; break;		//-33	///< Tried to FF_Open() a file marked read only.
		case pdFREERTOS_ERRNO_ENOTDIR:	errMsg = "Invalid path"; break;		//-34	///< The path of the file was not found.
		}
		FreeRTOS_printf( ( "ftp::delFile: '%s' because %s\n",
			pxClient->pcFileName, strerror( iErrorNo ) ) );

		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
			"521-\"%s\" %s;\r\n"
			"521 taking no action\r\n",
			pxClient->pcFileName, errMsg );

		xResult = pdFALSE;
	}
	prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );

	return xResult;
}
/*-----------------------------------------------------------*/

/*
 ####    #                       #####
#    #   #                        #   #            #
#    #                            #    #           #
#      ###   ######  ####         #    #  ####   ######  ####
 ##      #   #    # #    #        #    #      #    #    #    #
   ##    #       #  ######        #    #  #####    #    ######
#    #   #     #    #             #    # #    #    #    #
#    #   #    #     #   ##        #   #  #    #    # ## #   ##
 ####  ##### ######  ####        #####    ### ##    ##   ####
*/
static BaseType_t prvSizeDateFile( xFTPClient *pxClient, char *pcFileName, BaseType_t xSendDate )
{
BaseType_t xResult = pdFALSE;
char *pcPtr;

	/* SIZE: get the size of a file (xSendDate = 0)
	MDTM: get data and time properties (xSendDate = 1) */
	xMakeAbsolute( pxClient, pxClient->pcFileName, sizeof( pxClient->pcFileName ), pcFileName );

	pcPtr = strrchr( pxClient->pcFileName, '/' );

	if( ( pcPtr != NULL ) && ( pcPtr[1] != '\0' ) )
	{
		FF_Stat_t xStatBuf;
		int32_t iRc = ff_stat( pxClient->pcFileName, &xStatBuf );
		if (iRc < 0 )
			FreeRTOS_printf( ("In %s: %s\n", pxClient->pcFileName,
				( const char* )strerror( stdioGET_ERRNO() ) ) );

		if( iRc == 0 )
		{
		BaseType_t xLength;
			//"YYYYMMDDhhmmss"
			if( xSendDate != pdFALSE )
			{
				struct tm tmStruct;
				time_t secs = xStatBuf.st_mtime;
				gmtime_r( &secs, &tmStruct );

				xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "213 %04u%02u%02u%02u%02u%02u\r\n",
					tmStruct.tm_year + 1900,
					tmStruct.tm_mon+1,
					tmStruct.tm_mday,
					tmStruct.tm_hour,
					tmStruct.tm_min,
					tmStruct.tm_sec );
			}
			else
			{
				xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "213 %lu\r\n", xStatBuf.st_size );
			}
			prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );
			xResult = pdTRUE;
		}
		else
		{
			FreeRTOS_printf( ("ftp::sizeDateFile: No such file %s\n", pxClient->pcFileName ) );
		}
	} else {
		FreeRTOS_printf( ("ftp::sizeDateFile: Invalid file name: %s ?\n", pxClient->pcFileName ) );
	}
	if( xResult == pdFALSE )
	{
		prvSendReply( pxClient->xSocket, REPL_450, 0 );	// "Requested file action not taken"
	}

	return xResult;
}
/*-----------------------------------------------------------*/

/*
##   ## ##   ## #####      ######  ##   ## #####
### ###  #    #  #   #      #    # ### ###  #   #
# ### #  #   #   #    #     #    # # ### #  #    #
#  #  #  #   #   #    #     #    # #  #  #  #    #
#  #  #  ####    #    #     ###### #  #  #  #    #
#     #  #   #   #    #     #  ##  #     #  #    #
#     #  #   #   #    #     #   #  #     #  #    #
#     #  #    #  #   #      #    # #     #  #   #
#     # ###  ## #####      ###  ## #     # #####
*/
static BaseType_t prvMakeRemoveDir( xFTPClient *pxClient, const char *pcDirectory, BaseType_t xDoRemove )
{
BaseType_t xResult;
BaseType_t xLength;
int32_t iRc;
int iErrorNo;

	/* MKD: Make / create a directory (xDoRemove = 0)
	RMD: Remove a directory (xDoRemove = 1) */
	xMakeAbsolute( pxClient, pxClient->pcFileName, sizeof( pxClient->pcFileName ), pcDirectory );

	if( xDoRemove )
	{
		iRc = ff_rmdir( pxClient->pcFileName );
	}
	else
	{
		#if( ffconfigMKDIR_RECURSIVE != 0 )
		{
			iRc = ff_mkdir( pxClient->pcFileName, pdFALSE );
		}
		#else
		{
			iRc = ff_mkdir( pxClient->pcFileName );
		}
		#endif /* ffconfigMKDIR_RECURSIVE */
	}
	xResult = pdTRUE;

	if( iRc >= 0 )
	{
		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ), "257 \"%s\" directory %s\r\n",
			pxClient->pcFileName, xDoRemove ? "removed" : "created" );
	}
	else
	{
	const char *errMsg = "other error";
	BaseType_t xFTPCode = 521;

		xResult = pdFALSE;
		iErrorNo = stdioGET_ERRNO();
		switch( iErrorNo )
		{
		case pdFREERTOS_ERRNO_EEXIST:	errMsg = "Directory already exists"; break;
		case pdFREERTOS_ERRNO_ENOTDIR:	errMsg = "Invalid path"; break;			// -34	///< The path of the file was not found.
		case pdFREERTOS_ERRNO_ENOTEMPTY:errMsg = "Dir not empty"; break;
		case pdFREERTOS_ERRNO_EROFS:	errMsg = "Read-only"; break;			//-33	///< Tried to FF_Open() a file marked read only.
		default:						errMsg = strerror( iErrorNo ); break;
		}
		if( iErrorNo == pdFREERTOS_ERRNO_ENOSPC )
		{
			xFTPCode = 552;
		}
		xLength = snprintf( pcCOMMAND_BUFFER, sizeof( pcCOMMAND_BUFFER ),
			"%ld-\"%s\" %s;\r\n"
			"%ld taking no action\r\n",
			xFTPCode, pxClient->pcFileName, errMsg, xFTPCode );
		FreeRTOS_printf( ( "%sdir '%s': %s\n", xDoRemove ? "rm" : "mk", pxClient->pcFileName, errMsg ) );
	}
	prvSendReply( pxClient->xSocket, pcCOMMAND_BUFFER, xLength );

	return xResult;
}
/*-----------------------------------------------------------*/

static portINLINE BaseType_t IsDigit( char cChar )
{
BaseType_t xResult;

	if( cChar >= '0' && cChar <= '9' )
	{
		xResult = pdTRUE;
	}
	else
	{
		xResult = pdFALSE;
	}
	return xResult;
}

static BaseType_t prvSendReply( xSocket_t xSocket, const char *pcBuffer, BaseType_t xLength )
{
BaseType_t xResult;

	if( xLength == 0 )
	{
		xLength = strlen( pcBuffer );
	}
	xResult = FreeRTOS_send( xSocket, ( const void * )pcBuffer, ( size_t ) xLength, 0 );
	if( IsDigit( ( int ) pcBuffer[ 0 ] ) &&
		IsDigit( ( int ) pcBuffer[ 1 ] ) &&
		IsDigit( ( int ) pcBuffer[ 2 ] ) &&
		IsDigit( ( int ) pcBuffer[ 3 ] ) )
	{
		const char *last = pcBuffer + strlen( pcBuffer );
		int iLength;
		while( ( last > pcBuffer ) && ( ( last[ -1 ] == 13 ) || ( last[ -1 ] == 10 ) ) )
		{
			last--;
		}
		iLength = ( int )( last - pcBuffer );
		FF_PRINTF( "   %-*.*s", iLength, iLength, pcBuffer );
	}
	return xResult;
}
/*-----------------------------------------------------------*/

#if( ipconfigFTP_HAS_RECEIVED_HOOK != 0 )

	/*
	 * The following function is called for every file received:
	 *     void vApplicationFTPReceivedHook( pcFileName, ulSize, pxFTPClient );
	 * This callback function may do a callback to vFTPReplyMessage() to send messages
	 * to the FTP client like:
	 *      200-Please wait: Received new firmware
	 *      200-Please wait: Please wait a few seconds for reboot
	 */
	void vFTPReplyMessage( struct xFTP_CLIENT *pxFTPClient, const char *pcMessage )
	{
		if( ( pxFTPClient != NULL ) && ( pxFTPClient->xSocket != NULL ) )
		{
			prvSendReply( pxFTPClient->xSocket, pcMessage, 0 );
		}
	}
	/*-----------------------------------------------------------*/

#endif /* ipconfigFTP_HAS_RECEIVED_HOOK != 0 */

/*
 * Some explanation:
 * The FTP client may send: "DELE readme.txt"
 * Here the complete path is constructed consisting of 3 parts:
 *
 * pxClient->pcRootDir  +  pxClient->pcCurrentDir  +  pcFileName
 *
 * 'pcCurrentDir' will not be applied for an absolute path like in "DELE /.htaccess"
 */
BaseType_t xMakeAbsolute( xFTPClient *pxClient, char *pcBuffer, BaseType_t xBufferLength, const char *pcFileName )
{
BaseType_t xLength = strlen( pxClient->pcRootDir );

	if( pcFileName[ 0 ] != '/' )
	{
	char *pcNewDirBuffer = pcNEW_DIR;
	BaseType_t xCurLength;

		xCurLength = strlen( pxClient->pcCurrentDir );
		if( pcBuffer == pcNEW_DIR )
		{
			/* In one call, the result already goes into pcNEW_DIR.
			Use pcFILE_BUFFER in that case */
			pcNewDirBuffer = pcFILE_BUFFER;
		}
		snprintf( pcNewDirBuffer, sizeof pcNEW_DIR, "%s%s%s",
			pxClient->pcCurrentDir,
			pxClient->pcCurrentDir[ xCurLength - 1 ] == '/' ? "" : "/",
			pcFileName );
		pcFileName = pcNewDirBuffer;
	}

	xLength = snprintf( pcBuffer, xBufferLength, "%s/%s",
		pxClient->pcRootDir,
		pcFileName[ 0 ] == '/' ? ( pcFileName + 1 ) : pcFileName );
#if( ipconfigFTP_FS_USES_BACKSLAH == 1 )
	for( pcPtr = pcBuffer; *pcPtr; pcPtr++ )
	{
		if( pcPtr[ 0 ] == '/' )
		{
			pcPtr[ 0 ] = '\\';
		}
	}
#endif

	return xLength;
}
/*-----------------------------------------------------------*/
