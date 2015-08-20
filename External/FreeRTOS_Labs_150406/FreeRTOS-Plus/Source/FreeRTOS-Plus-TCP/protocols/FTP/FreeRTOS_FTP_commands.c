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

/* FreeRTOS includes. */
#include "FreeRTOS.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_FTP_commands.h"

const struct xFTP_COMMAND xFTPCommands[ FTP_CMD_COUNT ] =
{
/* cmdLen cmdName[7]    cmdType  checkLogin checkNullArg */
	{ 4, "USER",		ECMD_USER, pdFALSE, pdFALSE },
	{ 4, "PASS",		ECMD_PASS, pdFALSE, pdFALSE },
	{ 4, "ACCT",		ECMD_ACCT,	pdTRUE, pdFALSE },
	{ 3,  "CWD",		ECMD_CWD,	pdTRUE, pdTRUE },
	{ 4, "CDUP",		ECMD_CDUP,	pdTRUE, pdFALSE },
	{ 4, "SMNT",		ECMD_SMNT,	pdTRUE, pdFALSE },
	{ 4, "QUIT",		ECMD_QUIT,	pdTRUE, pdFALSE },
	{ 4, "REIN",		ECMD_REIN,	pdTRUE, pdFALSE },
	{ 4, "PORT",		ECMD_PORT,	pdTRUE, pdFALSE },
	{ 4, "PASV",		ECMD_PASV,	pdTRUE, pdFALSE },
	{ 4, "TYPE",		ECMD_TYPE,	pdTRUE, pdFALSE },
	{ 4, "STRU",		ECMD_STRU,	pdTRUE, pdFALSE },
	{ 4, "MODE",		ECMD_MODE,	pdTRUE, pdFALSE },
	{ 4, "RETR",		ECMD_RETR,	pdTRUE, pdTRUE },
	{ 4, "STOR",		ECMD_STOR,	pdTRUE, pdTRUE },
	{ 4, "STOU",		ECMD_STOU,	pdTRUE, pdFALSE },
	{ 4, "APPE",		ECMD_APPE,	pdTRUE, pdFALSE },
	{ 4, "ALLO",		ECMD_ALLO,	pdTRUE, pdFALSE },
	{ 4, "REST",		ECMD_REST,	pdTRUE, pdFALSE },
	{ 4, "RNFR",		ECMD_RNFR,	pdTRUE, pdTRUE },
	{ 4, "RNTO",		ECMD_RNTO,	pdTRUE, pdTRUE },
	{ 4, "ABOR",		ECMD_ABOR,	pdTRUE, pdFALSE },
	{ 4, "SIZE",		ECMD_SIZE,	pdTRUE, pdTRUE },
	{ 4, "MDTM",		ECMD_MDTM,	pdTRUE, pdTRUE },
	{ 4, "DELE",		ECMD_DELE,	pdTRUE, pdTRUE },
	{ 3,  "RMD",		ECMD_RMD,	pdTRUE, pdTRUE },
	{ 3,  "MKD",		ECMD_MKD,	pdTRUE, pdTRUE },
	{ 3,  "PWD",		ECMD_PWD,	pdTRUE, pdFALSE },
	{ 4, "LIST",		ECMD_LIST,	pdTRUE, pdFALSE },
	{ 4, "NLST",		ECMD_NLST,	pdTRUE, pdFALSE },
	{ 4, "SITE",		ECMD_SITE,	pdTRUE, pdFALSE },
	{ 4, "SYST",		ECMD_SYST,	pdFALSE, pdFALSE },
	{ 4, "FEAT",		ECMD_FEAT,	pdFALSE, pdFALSE },
	{ 4, "STAT",		ECMD_STAT,	pdTRUE, pdFALSE },
	{ 4, "HELP",		ECMD_HELP,	pdFALSE, pdFALSE },
	{ 4, "NOOP",		ECMD_NOOP,	pdFALSE, pdFALSE },
	{ 4, "EMPT",		ECMD_EMPTY,	pdFALSE, pdFALSE },
	{ 4, "CLOS",		ECMD_CLOSE,	pdTRUE, pdFALSE },
	{ 4, "UNKN",		ECMD_UNKNOWN, pdFALSE, pdFALSE },
};
