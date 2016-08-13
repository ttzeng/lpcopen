#ifndef RTTTL_h
#define RTTTL_h

#include <stdint.h>
#include <string>
using namespace std;

/*
 * Ring Tone Text Transfer Language Player
 * Ref: https://en.wikipedia.org/wiki/Ring_Tone_Transfer_Language
 *      http://www.mobilefish.com/tutorials/rtttl/rtttl_quickguide_specification.html
 */
class RTTTL : public string {
public:
	RTTTL(const char* s);
	char at(size_t pos) const {
		return (pos < size())? string::at(pos) : 0;
	}
	void rewind() { pos = start; }
	bool parseNextNote(int& octave, int& pitch, int& duration_in_msec);
private:
	bool parseKeyValuePair(string& key, int& value);
	size_t pos;
	string name;
	uint8_t def_duration;
	uint8_t def_octave;
	uint8_t def_bpm;
	size_t start;
	uint32_t full_note_duration_in_msec;
};

#endif
