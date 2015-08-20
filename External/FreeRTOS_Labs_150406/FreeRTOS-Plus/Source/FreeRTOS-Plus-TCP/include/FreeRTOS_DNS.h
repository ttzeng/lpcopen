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

#ifndef FREERTOS_DNS_H
#define FREERTOS_DNS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Application level configuration options. */
#include "FreeRTOSIPConfig.h"
#include "IPTraceMacroDefaults.h"


/* The Link-local Multicast Name Resolution (LLMNR)
 * is included.
 * Note that a special MAC address is required in addition to the NIC's actual
 * MAC address: 01:00:5E:00:00:FC
 *
 * The target IP address will be 224.0.0.252
 */
#if( ipconfigBYTE_ORDER == pdFREERTOS_BIG_ENDIAN )
	#define	ipLLMNR_IP_ADDR			0xE00000FC
#else
	#define	ipLLMNR_IP_ADDR			0xFC0000E0
#endif /* ipconfigBYTE_ORDER == pdFREERTOS_BIG_ENDIAN */

#define	ipLLMNR_PORT	5355 /* Standard LLMNR port. */
#define	ipDNS_PORT		53	/* Standard DNS port. */
#define	ipDHCP_CLIENT	67
#define	ipDHCP_SERVER	68
#define	ipNBNS_PORT		137	/* NetBIOS Name Service. */
#define	ipNBDGM_PORT	138 /* Datagram Service, not included. */

/*
 * The following function should be provided by the user and return true if it
 * matches the domain name.
 */
extern BaseType_t xApplicationDNSQueryHook( const char *pcName );

/*
 * LLMNR is very similar to DNS, so is handled by the DNS routines.
 */
uint32_t ulDNSHandlePacket( xNetworkBufferDescriptor_t *pxNetworkBuffer );

#if( ipconfigUSE_LLMNR == 1 )
	extern const xMACAddress_t xLLMNR_MacAdress;
#endif /* ipconfigUSE_LLMNR */

#if( ipconfigUSE_NBNS != 0 )

	/*
	 * Inspect a NetBIOS Names-Service message.  If the name matches with ours
	 * (xApplicationDNSQueryHook returns true) an answer will be sent back.
	 * Note that LLMNR is a better protocol for name services on a LAN as it is
	 * less polluted
	 */
	uint32_t ulNBNSHandlePacket (xNetworkBufferDescriptor_t *pxNetworkBuffer );

#endif /* ipconfigUSE_NBNS */

#if( ipconfigUSE_DNS_CACHE != 0 )

	uint32_t FreeRTOS_dnslookup( const char *pcHostName );

#endif /* ipconfigUSE_DNS_CACHE != 0 */

#if( ipconfigDNS_USE_CALLBACKS != 0 )

	/*
	 * Users may define this type of function as a callback.
	 * It will be called when a DNS reply is received or when a timeout has been reached.
	 */
	typedef void (* FOnDNSEvent ) ( const char * /* pcName */, void * /* pvSearchID */, uint32_t /* ulIPAddress */ );

	/*
	 * Asynchronous version of gethostbyname()
	 * xTimeout is in units of ms.
	 */
	uint32_t FreeRTOS_gethostbyname_a( const char *pcHostName, FOnDNSEvent pCallback, void *pvSearchID, TickType_t xTimeout );
	void FreeRTOS_gethostbyname_cancel( void *pvSearchID );

#endif

/*
 * FULL, UP-TO-DATE AND MAINTAINED REFERENCE DOCUMENTATION FOR ALL THESE
 * FUNCTIONS IS AVAILABLE ON THE FOLLOWING URL:
 * _TBD_ Add URL
 */
uint32_t FreeRTOS_gethostbyname( const char *pcHostName );


#ifdef __cplusplus
}	/* extern "C" */
#endif

#endif /* FREERTOS_DNS_H */













