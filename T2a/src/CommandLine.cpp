#include "CommandLine.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#ifdef __cplusplus
}
#endif

enum {
	CHAR_BS = 8,
	CHAR_LF = 10,
	CHAR_CR = 13,
};

CommandLine::CommandLine(Uart* serial, const char* prompt)
{
	index = 0;
	this->serial = serial;
	setPrompt(prompt);
}

void CommandLine::run()
{
	while (serial && serial->available() > 0) {
		BaseType_t more;
		char ch = serial->read();
		switch (ch) {
		case CHAR_CR:
			serial->println("");
			do {
				more = FreeRTOS_CLIProcessCommand(pcInStr, pcOutStr, sizeof(pcOutStr));
				serial->print(pcOutStr);
			} while (more == pdTRUE);
			index = 0;
			serial->print(szPrompt);
		case CHAR_LF:
			break;
		case CHAR_BS:
			if (index > 0) {
				pcInStr[--index] = 0;
				serial->print(ch);
			}
			break;
		default:
			if (index < MAX_INPUT_LENGTH - 1) {
				pcInStr[index++] = ch, pcInStr[index] = 0;
				serial->print(ch);
			}
		}
	}
}
