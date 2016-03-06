#include "SPI.h"

/* Default pins used by the SPI movable function */
#define SWM_SPI_SSEL		13
#define SWM_SPI_MOSI		12
#define SWM_SPI_MISO		10
#define SWM_SPI_SCK			11

SPISettings::SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode,
                         byte preD, byte postD, byte frameD, byte interD)
{
	spiBitOrder = bitOrder;
	spiDataMode = dataMode;
	spiClockDivider = SystemCoreClock / clock;
	spiDelay = DLY_PREDELAY(preD) | DLY_POSTDELAY(postD) | DLY_FRAMEDELAY(frameD) | DLY_INTERDELAY(interD);
}

SPIClass::SPIClass(byte ssel, byte sck, byte mosi, byte miso)
{
	setHardwarePins(ssel, sck, mosi, miso);
}

void SPIClass::begin()
{
	/* Enable SPI0 clock */
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 11);
	/* Reset SPI0 */
	LPC_SYSCON->PRESETCTRL &= ~(1 << 0);
	LPC_SYSCON->PRESETCTRL |=  (1 << 0);
	/* Configure pins used by SPI0 */
	Board::assignMovablePin(3, 24, SPI0_SCK);
	Board::assignMovablePin(4,  0, SPI0_MOSI);
	Board::assignMovablePin(4,  8, SPI0_MISO);
	Board::assignMovablePin(4, 16, SPI0_SSEL);
	/* Initialize the SPI bus */
	LPC_SPI0->CFG = CFG_MASTER
	                | ((spiBitOrder != MSBFIRST)? CFG_LSBF : 0)
	                | spiDataMode;
	LPC_SPI0->DLY = spiDelay;
	LPC_SPI0->DIV = spiClockDivider;
	LPC_SPI0->INTENCLR = STAT_RXRDY | STAT_TXRDY | STAT_RXOV | STAT_TXUR | STAT_SSA | STAT_SSD;
	LPC_SPI0->CFG |= CFG_ENABLE;
}

void SPIClass::end()
{
	endTransaction();
	/* Leave pin modes unchanged */
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 7);
	LPC_SWM->PINASSIGN3 |= 0xff000000;
	LPC_SWM->PINASSIGN4 |= 0x00ffffff;
	LPC_SYSCON->SYSAHBCLKCTRL &= ~(1 << 7);
	/* Disable SPI0 clock */
	LPC_SYSCON->SYSAHBCLKCTRL &= ~(1 << 11);
}

void SPIClass::beginTransaction(SPISettings settings)
{
	spiBitOrder = settings.spiBitOrder;
	spiDataMode = settings.spiDataMode;
	spiClockDivider = settings.spiClockDivider;
	spiDelay = settings.spiDelay;
	begin();
}

void SPIClass::endTransaction()
{
	/* Wait the bus full idle */
	while (!(LPC_SPI0->STAT & STAT_MSTIDLE));
	LPC_SPI0->CFG &= ~CFG_ENABLE;
}

byte SPIClass::transfer(uint8_t data)
{
	while (!(LPC_SPI0->STAT & STAT_TXRDY));
	LPC_SPI0->TXDATCTL = TXDATCTL_FSIZE(8-1) | TXDATCTL_EOT | data;

	while (!(LPC_SPI0->STAT & STAT_RXRDY));
	return LPC_SPI0->RXDAT;
}

void SPIClass::transfer(void *buf, size_t length)
{
	byte i, *p = (byte*)buf;
	for (i = 0; i < length; i++, p++) {
		while (!(LPC_SPI0->STAT & STAT_TXRDY));
		if (0 < i && i < length - 1)
			LPC_SPI0->TXDAT = *p;
		else
			LPC_SPI0->TXDATCTL = TXDATCTL_FSIZE(7) | ((i == length-1)? TXDATCTL_EOT : 0) | *p;

		while (!(LPC_SPI0->STAT & STAT_RXRDY));
		*p = LPC_SPI0->RXDAT;
	}
}

void SPIClass::setHardwarePins(byte ssel, byte sck, byte mosi, byte miso)
{
	SPI0_SSEL = ssel;
	SPI0_SCK  = sck;
	SPI0_MOSI = mosi;
	SPI0_MISO = miso;
}

SPIClass SPI(SWM_SPI_SSEL, SWM_SPI_SCK, SWM_SPI_MOSI, SWM_SPI_MISO);
