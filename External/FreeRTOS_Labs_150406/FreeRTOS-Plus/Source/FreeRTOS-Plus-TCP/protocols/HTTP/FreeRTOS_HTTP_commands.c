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
#include <stdio.h>
#include <stdlib.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"

#include "FreeRTOS_HTTP_commands.h"

const struct xWEB_COMMAND xWebCommands[ WEB_CMD_COUNT ] =
{
	{	3,     "GET",		ECMD_GET },
	{	4,    "HEAD",		ECMD_HEAD },
	{	4,    "POST",		ECMD_POST },
	{	3,     "PUT",		ECMD_PUT },
	{	6,  "DELETE",		ECMD_DELETE },
	{	5,   "TRACE",		ECMD_TRACE },
	{	7, "OPTIONS",		ECMD_OPTIONS },
	{	7, "CONNECT",		ECMD_CONNECT },
	{	5,   "PATCH",		ECMD_PATCH },
	{	4,    "UNKN",		ECMD_UNK },
};

const char *webCodename (int aCode)
{
	switch (aCode) {
	case WEB_REPLY_OK:	//  = 200,
		return "OK";
	case WEB_NO_CONTENT:    // 204
		return "No content";
	case WEB_BAD_REQUEST:	//  = 400,
		return "Bad request";
	case WEB_UNAUTHORIZED:	//  = 401,
		return "Authorization Required";
	case WEB_NOT_FOUND:	//  = 404,
		return "Not Found";
	case WEB_GONE:	//  = 410,
		return "Done";
	case WEB_PRECONDITION_FAILED:	//  = 412,
		return "Precondition Failed";
	case WEB_INTERNAL_SERVER_ERROR:	//  = 500,
		return "Internal Server Error";
	}
	return "Unknown";
}
