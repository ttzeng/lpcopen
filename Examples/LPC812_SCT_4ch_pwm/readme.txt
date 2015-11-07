Purpose:
* Show simple four-channel PWM generation.
* Uses the unified 32-bit timer mode to generate single-edge aligned outputs.
* Channels can have different polarity. Demo state machine has been configured for positive pulses at CTOUT_0/1
  and negative pulses at CTOUT_2/3.
* CTIN_0 is used a #ABORT input. If low, forces the outputs to their idle states, halts the timer, and generates
  an interrupt.
* An interrupt can be generated after each cycle.

Running mode:
* Compile, Flash the program and reset.
* Default project target set to Blinky_Release (executing from flash)

Note:
Tested on LPC800 LPCXpresso Board
LPC800 running at 24 MHz

Input:
PIO0_1 configured as CTIN_0 (#ABORT)

Output:
PIO0_17 configured as CTOUT_0, Green LED
PIO0_7 configured as CTOUT_1, Red LED
PIO0_16 configured as CTOUT_2, Yellow LED
PIO0_8 configured as CTOUT_3


See abort.png:
* CTOUT_1 (red)    50%
  CTOUT_0 (green)  40%
  CTOUT_2 (yellow) 10%
  CTOUT_3 (white)  90%
  #ABORT (white)
* Cursor positions A and B mark the early stage of two consecutive PWM cycles.
* Cursor position D marks the abort state. Note that the idle level of CTOUT_1 and CTOUT_0 is low,
  while the idle level of CTOUT_2 and CTOUT_3 is high.

See change.png:
* Duty cycles of two channels change every five PWM cycles.




 