#ifndef Hardware_h
#define Hardware_h

class Board {
public:
	virtual void init();
	static void assignMovablePin(byte reg, byte ofs, byte pin);
protected:
	virtual void initSwitchMatrix();
	virtual void initIOCON();
	virtual void setupSysClock();
	virtual void setupSysTick();
	virtual void setupMRT(uint32_t hz);
};

class TargetBoard {
public:
	TargetBoard(class Board* board = new Board) {
		(targetBoard = board)->init();
	}
private:
	class Board* targetBoard;
};

#endif
