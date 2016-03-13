#ifndef Tone_h
#define Tone_h

class Tone {
public:
	Tone() : toneOutput(-1) {};
	void attach(uint8_t pin, unsigned int frequency, unsigned long duration_in_msec = 0);
	void detach(uint8_t pin);
	void handler();
private:
	int8_t   toneOutput;
	uint32_t toneToggleCount;
};

void tone(uint8_t pin, unsigned int frequency, unsigned long duration_in_msec = 0);
void noTone(uint8_t pin);

extern "C" void tone_handler(void);
extern Tone toneGenerator;

#endif
