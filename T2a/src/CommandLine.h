#ifndef CommandLine_h
#define CommandLine_h

#include "Arduino.h"

class CommandLine {
public:
	CommandLine(Uart* serial, const char* prompt = "");
	void run();
	void setPrompt(const char* prompt) { szPrompt = prompt; }
private:
	static const byte MAX_INPUT_LENGTH  = 50;
	static const byte MAX_OUTPUT_LENGTH = 100;
	const char* szPrompt;
	uint8_t index;
	char pcInStr[MAX_INPUT_LENGTH];
	char pcOutStr[MAX_OUTPUT_LENGTH];
	Uart* serial;
};

#endif
