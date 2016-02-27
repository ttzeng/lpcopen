#ifndef Hardware_h
#define Hardware_h

class Board {
public:
	virtual void init();
protected:
	virtual void initSwitchMatrix();
	virtual void initIOCON();
	virtual void setupSysClock();
	virtual void setupSysTick();
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
