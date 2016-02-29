#ifndef TwoWire_h
#define TwoWire_h

#include "Stream.h"

#define BUFFER_LENGTH	32

class TwoWire : public Stream {
public:
	TwoWire();
	void begin();
	void setClock(uint32_t);
	void beginTransmission(uint8_t);
	uint8_t endTransmission(bool stop = true);
	uint8_t requestFrom(uint8_t, uint8_t, bool stop = true);
	virtual size_t write(uint8_t);
	virtual size_t write(const uint8_t *, size_t);
	virtual int available(void);
	virtual int read(void);
	virtual int peek(void);
	virtual void flush(void);
	using Print::write;
protected:
	I2C_HANDLE_T* hI2C;
	uint32_t* ramI2C;
	static uint8_t rxBuffer[];
    static uint8_t rxBufferIndex;
    static uint8_t rxBufferLength;
	static uint8_t txBuffer[];
	static uint8_t txBufferIndex;
};

extern TwoWire Wire;

#endif
