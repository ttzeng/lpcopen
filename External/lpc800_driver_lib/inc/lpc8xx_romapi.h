#ifndef LPC8XX_ROMAPI_H_
#define LPC8XX_ROMAPI_H_

#include "error_8xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LPC8xx Power ROM APIs - set_power response0 options
 */
typedef struct PWRD_API {
	void (*set_pll)(uint32_t cmd[], uint32_t resp[]);	/*!< Set PLL function */
	void (*set_power)(uint32_t cmd[], uint32_t resp[]);	/*!< Set power function */
} PWRD_API_T;

/**
 * @brief LPC8xx I2C ROM driver handle structure
 */
typedef void *I2C_HANDLE_T;

/**
 * @brief LPC8xx I2C ROM driver callback function
 */
typedef void (*I2C_CALLBK_T)(uint32_t err_code, uint32_t n);

/**
 * LPC8xx I2C ROM driver parameter structure
 */
typedef struct I2C_PARAM {
	uint32_t        num_bytes_send;		/*!< No. of bytes to send */
	uint32_t        num_bytes_rec;		/*!< No. of bytes to receive */
	uint8_t         *buffer_ptr_send;	/*!< Pointer to send buffer */
	uint8_t         *buffer_ptr_rec;	/*!< Pointer to receive buffer */
	I2C_CALLBK_T    func_pt;			/*!< Callback function */
	uint8_t         stop_flag;			/*!< Stop flag */
	uint8_t         dummy[3];
} I2C_PARAM_T;

/**
 * LPC8xx I2C ROM driver result structure
 */
typedef struct I2C_RESULT {
	uint32_t n_bytes_sent;				/*!< No. of bytes sent */
	uint32_t n_bytes_recd;				/*!< No. of bytes received */
} I2C_RESULT_T;

/**
 * LPC8xx I2C ROM driver modes enum
 */
typedef enum CHIP_I2C_MODE {
	IDLE,								/*!< IDLE state */
	MASTER_SEND,						/*!< Master send state */
	MASTER_RECEIVE,						/*!< Master Receive state */
	SLAVE_SEND,							/*!< Slave send state */
	SLAVE_RECEIVE						/*!< Slave receive state */
} CHIP_I2C_MODE_T;

/**
 * LPC8xx I2C ROM driver APIs structure
 */
typedef struct  I2CD_API {
	/*!< Interrupt Support Routine */
	void (*i2c_isr_handler)(I2C_HANDLE_T *handle);

	/*!< MASTER functions */
	ErrorCode_t (*i2c_master_transmit_poll)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_master_receive_poll)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_master_tx_rx_poll)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_master_transmit_intr)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_master_receive_intr)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_master_tx_rx_intr)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);

	/*!< SLAVE functions */
	ErrorCode_t (*i2c_slave_receive_poll)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_slave_transmit_poll)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_slave_receive_intr)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_slave_transmit_intr)(I2C_HANDLE_T *handle, I2C_PARAM_T *param, I2C_RESULT_T *result);
	ErrorCode_t (*i2c_set_slave_addr)(I2C_HANDLE_T *handle, uint32_t slave_addr_0_3, uint32_t slave_mask_0_3);

	/*!< OTHER support functions */
	uint32_t        (*i2c_get_mem_size)(void);
	I2C_HANDLE_T *  (*i2c_setup)( uint32_t  i2c_base_addr, uint32_t * start_of_ram);
	ErrorCode_t     (*i2c_set_bitrate)(I2C_HANDLE_T *handle, uint32_t  p_clk_in_hz, uint32_t bitrate_in_bps);
	uint32_t        (*i2c_get_firmware_version)(void);
	CHIP_I2C_MODE_T (*i2c_get_status)(I2C_HANDLE_T *handle);
	ErrorCode_t     (*i2c_set_timeout)(I2C_HANDLE_T *handle, uint32_t timeout);
} I2CD_API_T;

/**
 * @brief UART ROM driver UART handle
 */
typedef void UART_HANDLE_T;

/**
 * @brief UART ROM driver UART callback function
 */
typedef void (*UART_CALLBK_T)(uint32_t err_code, uint32_t n);

/**
 * @brief UART ROM driver UART DMA callback function
 */
typedef void (*UART_DMA_REQ_T)(uint32_t src_adr, uint32_t dst_adr, uint32_t size);

/**
 * @brief UART ROM driver configutaion structure
 */
typedef struct {
	uint32_t sys_clk_in_hz;		/*!< main clock in Hz */
	uint32_t baudrate_in_hz;	/*!< Baud rate in Hz */
	uint8_t  config;			/*!< Configuration value */
								/*!<  bit1:0  Data Length: 00: 7 bits length, 01: 8 bits length, others: reserved */
								/*!<  bit3:2  Parity: 00: No Parity, 01: reserved, 10: Even, 11: Odd */
								/*!<  bit4:   Stop Bit(s): 0: 1 Stop bit, 1: 2 Stop bits */
	uint8_t sync_mod;			/*!< Sync mode settings */
								/*!<  bit0:  Mode: 0: Asynchronous mode, 1: Synchronous  mode */
								/*!<  bit1:  0: Un_RXD is sampled on the falling edge of SCLK */
								/*!<         1: Un_RXD is sampled on the rising edge of SCLK */
								/*!<  bit2:  0: Start and stop bits are transmitted as in asynchronous mode) */
								/*!<         1: Start and stop bits are not transmitted) */
								/*!<  bit3:  0: The UART is a  slave in Synchronous mode */
								/*!<         1: The UART is a master in Synchronous mode */
	uint16_t error_en;			/*!< Errors to be enabled */
								/*!<  bit0: Overrun Errors Enabled */
								/*!<  bit1: Underrun Errors Enabled */
								/*!<  bit2: FrameErr Errors Enabled */
								/*!<  bit3: ParityErr Errors Enabled */
								/*!<  bit4: RxNoise Errors Enabled */
} UART_CONFIG_T;

/**
 * @brief UART ROM driver parameter structure
 */
#define TX_MODE_BUF_EMPTY		(0x00)
#define RX_MODE_BUF_FULL		(0x00)
#define TX_MODE_SZERO_SEND_CRLF	(0x01)
#define RX_MODE_CRLF_RECVD		(0x01)
#define TX_MODE_SZERO_SEND_LF	(0x02)
#define RX_MODE_LF_RECVD		(0x02)
#define TX_MODE_SZERO			(0x03)

#define DRIVER_MODE_POLLING		(0x00)
#define DRIVER_MODE_INTERRUPT	(0x01)
#define DRIVER_MODE_DMA			(0x02)

typedef struct {
	uint8_t         *buffer;		/*!< Pointer to data buffer */
	uint32_t        size;			/*!< Size of the buffer */
	uint16_t        transfer_mode;	/*!< Transfer mode settings */
									/*!<   0x00: uart_get_line: stop transfer when the buffer is full */
									/*!<   0x00: uart_put_line: stop transfer when the buffer is empty */
									/*!<   0x01: uart_get_line: stop transfer when CRLF are received */
									/*!<   0x01: uart_put_line: transfer stopped after reaching \0 and CRLF is sent out after that */
									/*!<   0x02: uart_get_line: stop transfer when LF are received */
									/*!<   0x02: uart_put_line: transfer stopped after reaching \0 and LF is sent out after that */
									/*!<   0x03: uart_get_line: RESERVED */
									/*!<   0x03: uart_put_line: transfer stopped after reaching \0 */
	uint16_t        driver_mode;	/*!< Driver mode */
									/*!<  0x00: Polling mode, function blocked until transfer completes */
									/*!<  0x01: Interrupt mode, function immediately returns, callback invoked when transfer completes */
									/*!<  0x02: DMA mode, in case DMA block is available, DMA req function is called for UART DMA channel setup, then callback function indicate that transfer completes */
	UART_CALLBK_T   callback_func_pt;	/*!< callback function pointer */
	UART_DMA_REQ_T  dma_req_func_pt;	/*!< UART DMA channel setup function pointer, not applicable on LPC8xx */
} UART_PARAM_T;

/**
 * @brief UART ROM driver APIs structure
 */
typedef struct UARTD_API {
	/*!< UART Configuration functions */
	uint32_t        (*uart_get_mem_size)(void);	/*!< Get the memory size needed by one Min UART instance */
	UART_HANDLE_T * (*uart_setup)(uint32_t base_addr, uint8_t * ram);	/*!< Setup Min UART instance with provided memory and return the handle to this instance */
	uint32_t        (*uart_init)(UART_HANDLE_T *handle, UART_CONFIG_T *set);	/*!< Setup baud rate and operation mode for uart, then enable uart */

	/*!< UART polling functions block until completed */
	uint8_t         (*uart_get_char)(UART_HANDLE_T *handle);	/*!< Receive one Char from uart. This functions is only returned after Char is received. In case Echo is enabled, the received data is sent out immediately */
	void            (*uart_put_char)(UART_HANDLE_T *handle, uint8_t data);	/*!< Send one Char through uart. This function is only returned after data is sent */
	uint32_t        (*uart_get_line)(UART_HANDLE_T *handle, UART_PARAM_T *param);	/*!< Receive multiple bytes from UART */
	uint32_t        (*uart_put_line)(UART_HANDLE_T *handle, UART_PARAM_T *param);	/*!< Send string (end with \0) or raw data through UART */

	/*!< UART interrupt functions return immediately and callback when completed */
	void            (*uart_isr)(UART_HANDLE_T *handle);	/*!< UART interrupt service routine. To use this routine, the corresponding USART interrupt must be enabled. This function is invoked by the user ISR */
} UARTD_API_T;

/**
 * @brief LPC8XX High level ROM API structure
 */
typedef struct ROM_API {
	const uint32_t    unused[3];
	const PWRD_API_T  *pPWRD;	/*!< Power profiles API function table */
	const uint32_t    p_dev1;
	const I2CD_API_T  *pI2CD;	/*!< I2C driver routines functions table */
	const uint32_t    p_dev3;
	const uint32_t    p_dev4;
	const uint32_t    p_dev5;
	const UARTD_API_T *pUARTD;	/*!< UART driver routines function table */
} LPC_ROM_API_T;

/* Pointer to ROM API function address */
#define LPC_ROM_API_BASE_LOC	0x1FFF1FF8UL
#define LPC_ROM_API		(*(LPC_ROM_API_T * *) LPC_ROM_API_BASE_LOC)

/* Pointer to @ref PWRD_API_T functions in ROM */
#define LPC_PWRD_API    ((LPC_ROM_API)->pPWRD)

/* Pointer to @ref I2CD_API_T functions in ROM */
#define LPC_I2CD_API    ((LPC_ROM_API)->pI2CD)

/* Pointer to @ref UARTD_API_T functions in ROM */
#define LPC_UARTD_API   ((LPC_ROM_API)->pUARTD)

#ifdef __cplusplus
}
#endif
#endif /* LPC8XX_ROMAPI_H_ */
