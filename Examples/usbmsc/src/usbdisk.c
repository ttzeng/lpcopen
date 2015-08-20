#include <stdio.h>
#include <board.h>
#include <FreeRTOS.h>
#include <task.h>
#include <FreeRTOS_CLI.h>
#include <USB.h>
#include <ff.h>
#include <diskio.h>
#include "rtc.h"
#include "usbdisk.h"

/** LPCUSBlib Mass Storage Class driver interface configuration and state information. This structure is
 *  passed to all Mass Storage Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
static USB_ClassInfo_MS_Host_t FlashDisk_MS_Interface = {
	.Config = {
		.DataINPipeNumber       = 1,
		.DataINPipeDoubleBank   = false,

		.DataOUTPipeNumber      = 2,
		.DataOUTPipeDoubleBank  = false,
		.PortNumber = 0,
	},
};

static volatile DSTATUS g_DiskStat = STA_NOINIT;
static SCSI_Capacity_t g_DiskCapacity;
static FATFS g_FatFS;

DSTATUS disk_initialize(BYTE drv)
{
	if (drv)
		return STA_NOINIT;				/* Supports only single drive */
	if (!(g_DiskStat & STA_NOINIT))
		return g_DiskStat;				/* card is already enumerated */

	rtc_initialize();
	return g_DiskStat &= ~STA_NOINIT;
}

DSTATUS disk_status(BYTE drv)
{
	if (drv)
		return STA_NOINIT;				/* Supports only single drive */

	return g_DiskStat;
}

DRESULT disk_read(BYTE drv, BYTE* buff, DWORD sector, UINT count)
{
	if (drv || !count)
		return RES_PARERR;
	if (g_DiskStat & STA_NOINIT)
		return RES_NOTRDY;

	return MS_Host_ReadDeviceBlocks(&FlashDisk_MS_Interface, 0,
									sector, count, g_DiskCapacity.BlockSize, buff)?
			RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE* buff, DWORD sector, UINT count)
{
	if (drv || !count)
		return RES_PARERR;
	if (g_DiskStat & STA_NOINIT)
		return RES_NOTRDY;

	return MS_Host_WriteDeviceBlocks(&FlashDisk_MS_Interface, 0,
									sector, count, g_DiskCapacity.BlockSize, buff)?
			RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void* buff)
{
	if (drv)
		return RES_PARERR;
	if (g_DiskStat & STA_NOINIT)
		return RES_NOTRDY;

	DRESULT res = RES_OK;
	switch (cmd) {
	case CTRL_SYNC:			/* Make sure that no pending write process */
		break;
	case GET_SECTOR_COUNT:	/* Get number of sectors on the disk (DWORD) */
		*(DWORD *) buff = g_DiskCapacity.Blocks;
		break;
	case GET_SECTOR_SIZE:	/* Get R/W sector size (WORD) */
		*(WORD *) buff = g_DiskCapacity.BlockSize;
		break;
	default:
		res = RES_PARERR;
	}
	return res;
}

static portBASE_TYPE xListFiles(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	static BaseType_t xMore = pdFALSE;
	static DIR dir;		/* Directory object */
	FILINFO fno;		/* File information object */
	FRESULT rc;
	if (xMore == pdTRUE || (rc = f_opendir(&dir, "")) == FR_OK) {
		if (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
			if (fno.fattrib & AM_DIR)
				*pcOutBuf++ = 'd';
			else if (fno.fattrib & AM_SYS)
				*pcOutBuf++ = 's';
			else
				*pcOutBuf++ = ' ';
			*pcOutBuf++ = 'r';
			*pcOutBuf++ = (fno.fattrib & AM_RDO)? '-' : 'w';
			sprintf(pcOutBuf,
					(fno.fattrib & AM_DIR)? "  %s\r\n" : "  %s %8lu\r\n",
					fno.fname, fno.fsize);
			return xMore = pdTRUE;
		}
		f_closedir(&dir);
	}
	*pcOutBuf = 0;
	return xMore = pdFALSE;
}

static portBASE_TYPE xChangeDirectory(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	portBASE_TYPE len;
	const char* arg = FreeRTOS_CLIGetParameter(pcCmdStr, 1, &len);
	FRESULT rc;
	if ((rc = f_chdir(arg)) == FR_OK)
		*pcOutBuf = 0;
	else sprintf(pcOutBuf, "No such directory [%d]\r\n", rc);
	return pdFALSE;
}

static portBASE_TYPE xDumpFile(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	static BaseType_t xMore = pdFALSE;
	portBASE_TYPE len;
	const char* arg = FreeRTOS_CLIGetParameter(pcCmdStr, 1, &len);
	static FIL fil;			/* File object */
	FRESULT rc;
	if (xMore == pdTRUE || (rc = f_open(&fil, arg, FA_READ)) == FR_OK) {
		if (f_gets(pcOutBuf, xOutBufLen, &fil))
			xMore = pdTRUE;
		else {
			f_close(&fil);
			*pcOutBuf = 0, xMore = pdFALSE;
		}
	}
	return xMore;
}

static const CLI_Command_Definition_t xCmdDir =
{
	"dir",
	"dir\t\tList files\r\n",
	xListFiles,
	0
};
static const CLI_Command_Definition_t xCmdChdir =
{
	"cd",
	"cd\t\tChange directory\r\n",
	xChangeDirectory,
	1
};
static const CLI_Command_Definition_t xCmdCat =
{
	"cat",
	"cat\t\tDump file\r\n",
	xDumpFile,
	1
};

static void vUsbMscTask(void *pvParameters)
{
	uint8_t ErrorCode, *port = &(FlashDisk_MS_Interface.Config.PortNumber);
	USB_Init(*port, USB_MODE_Host);
	while (1) {
		if (USB_HostState[*port] == HOST_STATE_Configured && (g_DiskStat & STA_NOINIT)) {
			do {
				ErrorCode = MS_Host_TestUnitReady(&FlashDisk_MS_Interface, 0);
			} while (ErrorCode != PIPE_RWSTREAM_NoError);
			if (!(ErrorCode = MS_Host_ReadDeviceCapacity(&FlashDisk_MS_Interface, 0, &g_DiskCapacity))) {
				DEBUGOUT("%lu blocks of %lu bytes.\r\n", g_DiskCapacity.Blocks, g_DiskCapacity.BlockSize);
				f_mount(&g_FatFS, "", true);
			} else {
				USB_Host_SetDeviceConfiguration(*port, 0);
			}
		}
		MS_Host_USBTask(&FlashDisk_MS_Interface);
		USB_USBTask(*port, USB_MODE_Host);
	}
}

void UsbDiskInit(void)
{
	/* USB MSC maintenance thread */
	xTaskCreate(vUsbMscTask, "vUsbMscMaintenance",
				256, NULL, (tskIDLE_PRIORITY + 0UL),
				(xTaskHandle*)NULL);

	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdDir);
	FreeRTOS_CLIRegisterCommand(&xCmdChdir);
	FreeRTOS_CLIRegisterCommand(&xCmdCat);
}

/** Event handler for the USB_DeviceAttached event. This indicates that a device has been attached to the host, and
 *  starts the library USB task to begin the enumeration and USB management process.
 */
void EVENT_USB_Host_DeviceAttached(const uint8_t corenum)
{
	DEBUGOUT(("Device Attached on port %d\r\n"), corenum);
}

/** Event handler for the USB_DeviceUnattached event. This indicates that a device has been removed from the host, and
 *  stops the library USB task management process.
 */
void EVENT_USB_Host_DeviceUnattached(const uint8_t corenum)
{
	DEBUGOUT(("\r\nDevice Unattached on port %d\r\n"), corenum);
}

/** Event handler for the USB_DeviceEnumerationComplete event. This indicates that a device has been successfully
 *  enumerated by the host and is now ready to be used by the application.
 */
void EVENT_USB_Host_DeviceEnumerationComplete(const uint8_t corenum)
{
	uint16_t ConfigDescriptorSize;
	uint8_t  ConfigDescriptorData[512];

	if (USB_Host_GetDeviceConfigDescriptor(corenum, 1, &ConfigDescriptorSize, ConfigDescriptorData,
										   sizeof(ConfigDescriptorData)) != HOST_GETCONFIG_Successful) {
		DEBUGOUT("Error Retrieving Configuration Descriptor.\r\n");
		return;
	}

	FlashDisk_MS_Interface.Config.PortNumber = corenum;
	if (MS_Host_ConfigurePipes(&FlashDisk_MS_Interface,
							   ConfigDescriptorSize, ConfigDescriptorData) != MS_ENUMERROR_NoError) {
		DEBUGOUT("Attached Device Not a Valid Mass Storage Device.\r\n");
		return;
	}

	if (USB_Host_SetDeviceConfiguration(FlashDisk_MS_Interface.Config.PortNumber, 1) != HOST_SENDCONTROL_Successful) {
		DEBUGOUT("Error Setting Device Configuration.\r\n");
		return;
	}

	uint8_t MaxLUNIndex;
	if (MS_Host_GetMaxLUN(&FlashDisk_MS_Interface, &MaxLUNIndex)) {
		DEBUGOUT("Error retrieving max LUN index.\r\n");
		USB_Host_SetDeviceConfiguration(FlashDisk_MS_Interface.Config.PortNumber, 0);
		return;
	}

	DEBUGOUT(("Total LUNs: %d - Using first LUN in device.\r\n"), (MaxLUNIndex + 1));

	if (MS_Host_ResetMSInterface(&FlashDisk_MS_Interface)) {
		DEBUGOUT("Error resetting Mass Storage interface.\r\n");
		USB_Host_SetDeviceConfiguration(FlashDisk_MS_Interface.Config.PortNumber, 0);
		return;
	}

	SCSI_Request_Sense_Response_t SenseData;
	if (MS_Host_RequestSense(&FlashDisk_MS_Interface, 0, &SenseData) != 0) {
		DEBUGOUT("Error retrieving device sense.\r\n");
		USB_Host_SetDeviceConfiguration(FlashDisk_MS_Interface.Config.PortNumber, 0);
		return;
	}

	//  if (MS_Host_PreventAllowMediumRemoval(&FlashDisk_MS_Interface, 0, true)) {
	//      DEBUGOUT("Error setting Prevent Device Removal bit.\r\n");
	//      USB_Host_SetDeviceConfiguration(FlashDisk_MS_Interface.Config.PortNumber, 0);
	//      return;
	//  }

	SCSI_Inquiry_Response_t InquiryData;
	if (MS_Host_GetInquiryData(&FlashDisk_MS_Interface, 0, &InquiryData)) {
		DEBUGOUT("Error retrieving device Inquiry data.\r\n");
		USB_Host_SetDeviceConfiguration(FlashDisk_MS_Interface.Config.PortNumber, 0);
		return;
	}

	DEBUGOUT("Vendor \"%.8s\", Product \"%.16s\"\r\n", InquiryData.VendorID, InquiryData.ProductID);

	DEBUGOUT("Mass Storage Device Enumerated.\r\n");
}

/** Event handler for the USB_HostError event. This indicates that a hardware error occurred while in host mode. */
void EVENT_USB_Host_HostError(const uint8_t corenum, const uint8_t ErrorCode)
{
	USB_Disable(corenum, USB_MODE_Host);

	DEBUGOUT(("Host Mode Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n" ), corenum, ErrorCode);

	while (1) {}
}

/** Event handler for the USB_DeviceEnumerationFailed event. This indicates that a problem occurred while
 *  enumerating an attached USB device.
 */
void EVENT_USB_Host_DeviceEnumerationFailed(const uint8_t corenum,
											const uint8_t ErrorCode,
											const uint8_t SubErrorCode)
{
	DEBUGOUT(("Dev Enum Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n"
			  " -- Sub Error Code %d\r\n"
			  " -- In State %d\r\n" ),
			 corenum, ErrorCode, SubErrorCode, USB_HostState[corenum]);
}
