#ifndef Uart_h
#define Uart_h

#include <inttypes.h>

#include "Stream.h"

#define SERIAL_7N1	(DATA_LENG_7 | PARITY_NONE | STOP_BIT_1)
#define SERIAL_8N1	(DATA_LENG_8 | PARITY_NONE | STOP_BIT_1)
#define SERIAL_7N2	(DATA_LENG_7 | PARITY_NONE | STOP_BIT_2)
#define SERIAL_8N2	(DATA_LENG_8 | PARITY_NONE | STOP_BIT_2)
#define SERIAL_7E1	(DATA_LENG_7 | PARITY_EVEN | STOP_BIT_1)
#define SERIAL_8E1	(DATA_LENG_8 | PARITY_EVEN | STOP_BIT_1)
#define SERIAL_7E2	(DATA_LENG_7 | PARITY_EVEN | STOP_BIT_2)
#define SERIAL_8E2	(DATA_LENG_8 | PARITY_EVEN | STOP_BIT_2)
#define SERIAL_7O1	(DATA_LENG_7 | PARITY_ODD  | STOP_BIT_1)
#define SERIAL_8O1	(DATA_LENG_8 | PARITY_ODD  | STOP_BIT_1)
#define SERIAL_7O2	(DATA_LENG_7 | PARITY_ODD  | STOP_BIT_2)
#define SERIAL_8O2	(DATA_LENG_8 | PARITY_ODD  | STOP_BIT_2)

class Uart : public Stream
{
  public:
    virtual void begin(unsigned baudrate, unsigned config = SERIAL_8N1);
    virtual void end();
    virtual int available(void);
    virtual int peek(void);
    virtual int read(void);
    virtual void flush(void);
    virtual size_t write(uint8_t);
    operator bool() { return true; }
};

extern Uart Serial;

extern void serialEventRun(void) __attribute__((weak));

#endif
