#ifndef SPI_h
#define SPI_h

#include "Arduino.h"

#define CFG_ENABLE		(1 << 0)
#define CFG_MASTER		(1 << 2)
#define CFG_SLAVE		(0 << 2)
#define CFG_LSBF		(1 << 3)
#define CFG_CPHA		(1 << 4)
#define CFG_CPOL		(1 << 5)
#define CFG_LOOPBACK	(1 << 7)

#define SPI_MODE0		(    0    |     0   )
#define SPI_MODE1		(    0    | CFG_CPHA)
#define SPI_MODE2		(CFG_CPOL |     0   )
#define SPI_MODE3		(CFG_CPOL | CFG_CPHA)

#define DLY_PREDELAY(d)		((d) << 0)
#define DLY_POSTDELAY(d)	((d) << 4)
#define DLY_FRAMEDELAY(d)	((d) << 8)
#define DLY_INTERDELAY(d)	((d) << 12)

#define STAT_RXRDY		(1 << 0)
#define STAT_TXRDY		(1 << 1)
#define STAT_RXOV		(1 << 2)
#define STAT_TXUR		(1 << 3)
#define STAT_SSA		(1 << 4)
#define STAT_SSD		(1 << 5)
#define STAT_STALLED	(1 << 6)
#define STAT_ENDTRANSFER (1 << 7)
#define STAT_MSTIDLE	(1 << 8)

#define TXDATCTL_EOT	(1 << 20)
#define TXDATCTL_EOF	(1 << 21)
#define TXDATCTL_RX_IGNORE	(1 << 22)
#define TXDATCTL_FSIZE(s)	((s) << 24)

class SPISettings {
public:
	// Default speed set to 4MHz, SPI mode set to MODE 0 and Bit order set to MSB first
	SPISettings(uint32_t clock = 4000000, uint8_t bitOrder = MSBFIRST, uint8_t dataMode = SPI_MODE0,
	            byte preDelay = 0, byte postDelay = 0, byte frameDelay = 0, byte interDelay = 0);
	friend class SPIClass;
private:
	uint8_t spiBitOrder;
	uint8_t spiDataMode;
	uint8_t spiClockDivider;
	uint16_t spiDelay;
};

class SPIClass : public SPISettings {
public:
	SPIClass(byte ssel, byte sck, byte mosi, byte miso);
	void begin();
	void end();
	void beginTransaction(SPISettings settings);
	void endTransaction(void);
	byte transfer(uint8_t data);
	void transfer(void *buf, size_t length);
	void setHardwarePins(byte ssel, byte sck, byte mosi, byte miso);
protected:
	byte SPI0_SSEL;
	byte SPI0_SCK;
	byte SPI0_MOSI;
	byte SPI0_MISO;
};

extern SPIClass SPI;

#endif
