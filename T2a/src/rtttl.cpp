#include <cctype>
#include <cstdlib>
#include "rtttl.h"

RTTTL::RTTTL(const char* s) : string(s), def_duration(4), def_octave(6), def_bpm(63), start(string::npos)
{
	// search for the closing colon of the name section
	if ((pos = find(':')) != string::npos) {
		name = substr(0, pos);

		// parse the default value section
		for (pos++; true; pos++) {
			string key;
			int value;
			if (parseKeyValuePair(key, value)) {
				if (!key.compare("d")) def_duration = value;
				if (!key.compare("o")) def_octave   = value;
				if (!key.compare("b")) def_bpm      = value;
			}
			if (pos == string::npos)
				break;
			else if (at(pos) == ':') {
				start = ++pos; // skip the colon represents end of default value section
				// BPM represents the number of quarter notes per minute
				full_note_duration_in_msec = (60 * 1000 / def_bpm) << 2;
				break;
			}
		}
	} // missing the name section
	rewind();
}

bool RTTTL::parseNextNote(int& octave, int& pitch, int& duration_in_msec)
{
	char c;
	// skip leading spaces
	for (; isspace(c = at(pos)); pos++);
	int duration = def_duration;
	if (isdigit(c)) {
		// parse the duration
		size_t m = pos++;
		for (; isdigit(c = at(pos)); pos++);
		duration = atoi(substr(m, pos - m).c_str());
	}
	duration_in_msec = full_note_duration_in_msec / duration;
	// parse the note
	switch (tolower(c)) {
		case 'p': pitch = 0;  break;
		case 'c': pitch = 1;  break;
		case 'd': pitch = 3;  break;
		case 'e': pitch = 5;  break;
		case 'f': pitch = 6;  break;
		case 'g': pitch = 8;  break;
		case 'a': pitch = 10; break;
		case 'b': pitch = 12; break;
		default : pitch = -1;
	}
	if (pitch >= 0) {
		if ((c = at(++pos)) == '#') {
			pitch++, c = at(++pos);
		}
		if ('4' <= c && c <= '7') {
			// parse the scale
			octave = c - '0', c = at(++pos);
		} else octave = def_octave;
		if (c == '.') {
			duration_in_msec += duration_in_msec >> 1, c = at(++pos);
		}
		pos++;
		return true;
	}
	if (pos >= size()) pos = string::npos;
	return false;
}

bool RTTTL::parseKeyValuePair(string& key, int& value)
{
	char c;
	// skip leading spaces
	for (; isspace(c = at(pos)); pos++);
	if (isalpha(c)) {
		// parse the key
		size_t m = pos++, n;
		for (; isalnum(c = at(pos)); pos++);
		// skip any tailing spaces
		for (n = pos; isspace(c); c = at(++pos));
		if (c == '=') {
			key = substr(m, n - m);
			// values should be terminated by commas or colons
			if ((pos = find_first_of(",:", m = pos + 1)) != string::npos) {
				value = atoi(substr(m, pos - m).c_str());
				return true;
			} // open value setting
		} // neither space nor '='
	} // neither alpha nor space
	if (pos >= size()) pos = string::npos;
	return false;
}
