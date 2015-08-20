#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif
#include <cr_section_macros.h>
#include "FreeRTOS.h"
#include "task.h"
#include "console.h"
#include "ramdisk.h"

int main(void)
{
#if defined (__USE_LPCOPEN)
#if !defined(NO_BOARD_LIB)
    // Read clock settings and update SystemCoreClock variable
    SystemCoreClockUpdate();
    // Turn off all peripheral clocks
    LPC_SYSCTL->PCONP = 0;
    // Set up and initialize all required blocks and
    // functions related to the board hardware
    Board_Init();
#endif
#endif

    ConsoleInit();
    RamdiskInit();

    /* Start the scheduler */
	vTaskStartScheduler();
    return 0 ;
}
