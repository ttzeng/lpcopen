#include "Arduino.h"
#include "Wire.h"

#define SWM_I2C_SDA			10
#define SWM_I2C_SCL			11
#define I2C_BITRATE			100000	/* 100Kbps */
#define I2C_TIMEOUT			100000

uint8_t TwoWire::rxBuffer[BUFFER_LENGTH];
uint8_t TwoWire::rxBufferIndex;
uint8_t TwoWire::rxBufferLength = 0;
uint8_t TwoWire::txBuffer[BUFFER_LENGTH];
uint8_t TwoWire::txBufferIndex;

TwoWire::TwoWire()
{
	/* Enable the two pins required for the SCL and SDA outputs */
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 7) | (1 << 18);
	LPC_SWM->PINASSIGN7 = (LPC_SWM->PINASSIGN7 & 0x00ffffff) | ((uint32_t)SWM_I2C_SDA << 24);
	LPC_SWM->PINASSIGN8 = (LPC_SWM->PINASSIGN8 & 0xffffff00) | (SWM_I2C_SCL);
	/* Enable Fast Mode Plus for I2C pins */
	LPC_IOCON->PIO0_10 = (LPC_IOCON->PIO0_10 & ~0x300) | (2 << 8);
	LPC_IOCON->PIO0_11 = (LPC_IOCON->PIO0_11 & ~0x300) | (2 << 8);
	LPC_SYSCON->SYSAHBCLKCTRL &= ~((1 << 7) | (1 << 18));
}

void TwoWire::begin()
{
	/* Enable the clock to the I2C peripheral */
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 5);
	/* Reset I2C */
	//LPC_SYSCON->PRESETCTRL &= ~(1 << 6);
	//LPC_SYSCON->PRESETCTRL |=  (1 << 6);

	/* Allocate SRAM for the I2C ROM Driver */
	ramI2C = (uint32_t*)malloc(LPC_I2CD_API->i2c_get_mem_size());
	/* Create the I2C handle */
	hI2C = LPC_I2CD_API->i2c_setup(LPC_I2C_BASE, ramI2C);
	/* Set the I2C operating frequency */
	setClock(I2C_BITRATE);
	/* Enable I2C timeout */
	LPC_I2CD_API->i2c_set_timeout(hI2C, I2C_TIMEOUT);
}

void TwoWire::setClock(uint32_t baudrate)
{
	LPC_I2CD_API->i2c_set_bitrate(hI2C, SystemCoreClock, baudrate);
}

void TwoWire::beginTransmission(uint8_t address)
{
	txBufferIndex = 0;
	/* the first byte of the send buffer must have the slave address
	   in the most significant 7 bits and the least significant (R/W) bit = 0 */
	write(address << 1);
}

size_t TwoWire::write(uint8_t value)
{
	size_t bytes_written = 0;
	if (txBufferIndex < BUFFER_LENGTH) {
		txBuffer[txBufferIndex++] = value;
		bytes_written++;
	}
	return bytes_written;
}

size_t TwoWire::write(const uint8_t *data, size_t length)
{
	size_t bytes_written;
	for (bytes_written = 0; length > 0 && txBufferIndex < BUFFER_LENGTH; length--) {
		txBuffer[txBufferIndex++] = *data++;
		bytes_written++;
	}
	return bytes_written;
}

void TwoWire::flush(void)
{
}

uint8_t TwoWire::endTransmission(bool stop)
{
	I2C_PARAM param;
	I2C_RESULT result;
	param.buffer_ptr_send = txBuffer;
	param.num_bytes_send  = txBufferIndex;
	param.stop_flag	      = stop;
	/* The number of bytes to be transmitted should include the first byte of the buffer
	   which is the slave address byte */
	switch (LPC_I2CD_API->i2c_master_transmit_poll(hI2C, &param, &result)) {
	case LPC_OK :
		return 0;
	case ERR_I2C_NAK :
		return 3;
	default:
		return 4;
	}
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, bool stop)
{
	/* In all the master mode routines, the transmit buffer’s first byte must be
	   the slave address with the R/W bit set to “0”. To enable a master read,
	   the receive buffer’s first byte must be the slave address with the R/W bit set to “1” */
	rxBuffer[0] = (txBuffer[0] = (address << 1)) | 1;

	I2C_PARAM param;
	I2C_RESULT result;
	param.buffer_ptr_rec = rxBuffer;
	param.num_bytes_rec  = quantity;
	param.stop_flag	     = stop;
	if (LPC_I2CD_API->i2c_master_receive_poll(hI2C, &param, &result) == LPC_OK) {
		rxBufferIndex  = 1;
		rxBufferLength = result.n_bytes_recd;
	} else rxBufferLength = 0;
	return rxBufferLength;
}

int TwoWire::available(void)
{
	return rxBufferLength;
}

int TwoWire::read(void)
{
	int byte = EOF;
	if (rxBufferLength > 0) {
		byte = rxBuffer[rxBufferIndex++];
		rxBufferLength--;
	}
	return byte;
}

int TwoWire::peek(void)
{
	return (rxBufferLength > 0)? rxBuffer[rxBufferIndex] : EOF;
}

TwoWire Wire;
