#ifndef Tone_Player_h
#define Tone_Player_h

#include "Arduino.h"
#include "rtttl.h"

class TonePlayer : public RTTTL {
public:
	const uint8_t pinTone = 8;
	TonePlayer(const char* melody);
	TonePlayer& operator = (const char* melody);
	bool loop() { return loopMode; }
	void loop(bool mode) { loopMode = mode; }
private:
	bool loopMode;
};

#endif
