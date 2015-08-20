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
	Some code which is common to TCP servers like HTTP en FTP
*/

#ifndef FREERTOS_TCP_SERVER_H
#define	FREERTOS_TCP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef	FTP_SERVER_USES_RELATIVE_DIRECTORY
	#define	FTP_SERVER_USES_RELATIVE_DIRECTORY		0
#endif

enum eSERVER_TYPE
{
	eSERVER_NONE,
	eSERVER_HTTP,
	eSERVER_FTP,
};

struct xFTP_CLIENT;

#if( ipconfigFTP_HAS_RECEIVED_HOOK != 0 )
	extern void vApplicationFTPReceivedHook( const char *pcFileName, uint32_t ulSize, struct xFTP_CLIENT *pxFTPClient );
	extern void vFTPReplyMessage( struct xFTP_CLIENT *pxFTPClient, const char *pcMessage );
#endif /* ipconfigFTP_HAS_RECEIVED_HOOK != 0 */

#if( ipconfigFTP_HAS_USER_PASSWORD_HOOK != 0 )
	/*
	 * Function is called when a user name has been submitted.
	 */
	extern const char *pcApplicationFTPUserHook( const char *pcUserName );
#endif /* ipconfigFTP_HAS_USER_PASSWORD_HOOK */

#if( ipconfigFTP_HAS_USER_PASSWORD_HOOK != 0 )
	/*
	 * Function is called when a password was received.
	 * Return positive value to allow the user
	 */
	extern BaseType_t xApplicationFTPPasswordHook( const char *pcuserName, const char *pcPassword );
#endif /* ipconfigFTP_HAS_USER_PASSWORD_HOOK */

struct xSERVER_CONFIG
{
	enum eSERVER_TYPE eType;		/* eSERVER_HTTP | eSERVER_FTP */
	BaseType_t xPortNumber;			/* e.g. 80, 8080, 21 */
	BaseType_t xBackLog;			/* e.g. 10, maximum number of connected TCP clients */
	const char * const pcRootDir;	/* Treat this directory as the root directory */
};

/*
	static const struct xSERVER_CONFIG xConfig[] =
	{
		{ eSERVER_HTTP, 80,   12, "/websrc" },
		{ eSERVER_HTTP, 8080, 12, "/websrc" },
		{ eSERVER_FTP,  21,   12, "/ftp/public" },
	};
	xTCPServer *tcpServer = FreeRTOS_CreateTCPServer( xConfig, sizeof( xConfig ) / sizeof( xConfig[ 0 ] ) );
*/

struct xTCP_SERVER;

typedef struct xTCP_SERVER xTCPServer;

xTCPServer *FreeRTOS_CreateTCPServer( const struct xSERVER_CONFIG *pxConfigs, BaseType_t xCount );
void FreeRTOS_TCPServerWork( xTCPServer *pxServer, TickType_t xBlockingTime );

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FREERTOS_TCP_SERVER_H */
