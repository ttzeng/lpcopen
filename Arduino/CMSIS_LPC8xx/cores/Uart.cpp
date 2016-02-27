#include "Arduino.h"

#define LEN_BUF			32
static uint8_t rxHead , rxTail, rxBuf[LEN_BUF];

Uart Serial;

extern void UART0_IRQHandler(void)
{
	uint32_t Status = LPC_USART0->STAT;
	if (Status & RXRDY) {
		uint8_t p = rxHead;
		rxBuf[p++] = LPC_USART0->RXDATA;
		if (p >= LEN_BUF) p = 0;
		if (p != rxTail)
			rxHead = p;
	}
}

void Uart::begin(unsigned baudrate, unsigned config)
{
	/* Enable clock to USART0 */
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 14);
	/* Reset USART0 */
	LPC_SYSCON->PRESETCTRL &= ~(1 << 3);
	LPC_SYSCON->PRESETCTRL |=  (1 << 3);
	/* Set USART0 fractional baud rate generator clock divider */
	LPC_SYSCON->UARTCLKDIV = LPC_SYSCON->SYSAHBCLKDIV;
	LPC_USART0->CFG = config;
	LPC_USART0->BRG = SystemCoreClock/16/baudrate-1;
	LPC_SYSCON->UARTFRGDIV = 0xFF;
	LPC_SYSCON->UARTFRGMULT = (((SystemCoreClock/16) * (LPC_SYSCON->UARTFRGDIV+1)) / (baudrate*(LPC_USART0->BRG+1))) - (LPC_SYSCON->UARTFRGDIV+1);
	/* Clear all status bits */
	LPC_USART0->STAT = CTS_DELTA | DELTA_RXBRK;
	/* Enable USART0 interrupt */
	NVIC_EnableIRQ(UART0_IRQn);
	LPC_USART0->INTENSET = RXRDY | DELTA_RXBRK;
	LPC_USART0->INTENCLR = TXRDY;
	LPC_USART0->CFG |= UART_EN;

	rxHead = rxTail = 0;
}

void Uart::end()
{
	NVIC_DisableIRQ(UART0_IRQn);
}

int Uart::available()
{
	int delta = rxHead - rxTail;
	return (delta >= 0)? delta : delta + LEN_BUF;
}

int Uart::peek()
{
	return (rxTail != rxHead)? rxBuf[rxTail] : EOF;
}

int Uart::read()
{
	int byte = EOF;
	if (rxTail != rxHead) {
		byte = rxBuf[rxTail++];
		if (rxTail >= LEN_BUF)
			rxTail = 0;
	}
	return byte;
}

size_t Uart::write(const uint8_t data)
{
	while (!(LPC_USART0->STAT & TXRDY));
	LPC_USART0->TXDATA = data;
	return 1;
}

void Uart::flush()
{
}
