#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <FreeRTOS_CLI.h>
#include "api/fat_sl.h"
#include "api/api_mdriver_ram.h"
#include "ramdisk.h"

static portBASE_TYPE xShowFs(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	F_SPACE xSpace;
	uint8_t err = f_getfreespace(&xSpace);
	if (err == F_NO_ERROR) {
		sprintf(pcOutBuf, "Total: %d, free: %d, used: %d\r\n", xSpace.total, xSpace.free, xSpace.used);
	} else *pcOutBuf = 0;
	return pdFALSE;
}

static void getFileInfo(char* pcOutBuf, F_FIND* find)
{
	*pcOutBuf++ = (find->attr & F_ATTR_HIDDEN)?	'(' : ' ';
	*pcOutBuf++ = (find->attr & F_ATTR_SYSTEM)?	's' : ' ';
	*pcOutBuf++ = 'r';
	*pcOutBuf++ = (find->attr & F_ATTR_READONLY)? ' ' : 'w';
	*pcOutBuf++ = ' ';
	uint32_t len = strlen(find->filename);
	strncpy(pcOutBuf, find->filename, len);
	pcOutBuf += len;
	if (find->attr & F_ATTR_DIR)
		*pcOutBuf++ = '\\';
	else sprintf(pcOutBuf, ", size: %d", find->filesize);
	strcat(pcOutBuf, (find->attr & F_ATTR_HIDDEN)? ")\r\n" : "\r\n");
}

static portBASE_TYPE xListFiles(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	static BaseType_t xMore = pdFALSE;
	static F_FIND find;
	uint8_t err;
	*pcOutBuf = 0;
	if (xMore == pdFALSE) {
		if ((err = f_findfirst("*.*", &find)) == F_NO_ERROR) {
			getFileInfo(pcOutBuf, &find);
			xMore = pdTRUE;
		}
	} else {
		if ((err = f_findnext(&find)) == F_NO_ERROR) {
			getFileInfo(pcOutBuf, &find);
		} else xMore = pdFALSE;
	}
	return xMore;
}

static const CLI_Command_Definition_t xCmdFS =
{
	"fs",
	"fs\t\tDisplay filesystem info\r\n",
	xShowFs,
	0
};

static const CLI_Command_Definition_t xCmdDir =
{
	"dir",
	"dir\t\tList files in current directory\r\n",
	xListFiles,
	0
};

void RamdiskInit(void)
{
	/* Initialize RAM disk */
	uint8_t err;
	if ((err = f_initvolume(ram_initfunc)) == F_ERR_NOTFORMATTED)
		err = f_format(F_FAT12_MEDIA);
	if (err != F_NO_ERROR)
		return;

	/* Create a testing file on the volume */
	int i;
	char fname[32], data[256];
	F_FILE* pxFile;
	for (i = 0; i < 1; i++) {
		sprintf(fname, "file%d.dat", i);
		if ((pxFile = f_open(fname, "w")) != NULL) {
			printf("%d ", f_write(data, 1, sizeof(data), pxFile));
			f_close(pxFile);
		}
	}

	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdFS);
	FreeRTOS_CLIRegisterCommand(&xCmdDir);
}
