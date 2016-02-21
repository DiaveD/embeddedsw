/******************************************************************************
*
* Copyright (C) 2014 - 2015 Xilinx, Inc. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/
/*****************************************************************************
*
* @file sleep.c
*
* This function provides a second delay using the Global Timer register in
* the ARM Cortex R5 MP core.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who      Date     Changes
* ----- -------- -------- -----------------------------------------------
* 5.00 	pkp  	 02/20/14 First release
* 5.04  pkp		 02/19/16 sleep routine is modified to use TTC3 if present
*						  else it will use set of assembly instructions to
*						  provide the required delay
* </pre>
*
******************************************************************************/
/***************************** Include Files *********************************/

#include "sleep.h"
#include "xtime_l.h"
#include "xparameters.h"

/*****************************************************************************/
/*
*
* This API is used to provide delays in seconds.
*
* @param	seconds requested
*
* @return	0 always
*
* @note		The sleep API is implemented using TTC3 counter 0 timer if present.
*			When TTC3 is absent, sleep is implemented using assembly
*			instructions which is tested with instruction and data caches
*			enabled and it gives proper delay. It may give more delay than
*			exepcted when caches are disabled.
*
****************************************************************************/

s32 sleep(u32 seconds)
{
#ifdef SLEEP_TIMER_BASEADDR
	XTime tEnd, tCur;

	XTime_GetTime(&tCur);
	tEnd  = tCur + (((XTime) seconds) * COUNTS_PER_SECOND);
	do
	{
	    XTime_GetTime(&tCur);

	} while (tCur < tEnd);

	return 0;
#else

    u32 currmask;
	currmask = mfcpsr();
	/*disable the interrupts*/
	mtcpsr(currmask | IRQ_FIQ_MASK);

	__asm__ __volatile__ (
			" push {r0,r1}		\n\t"
			" mov r0, %[sec]	\n\t"
			" 1: \n\t"
			" mov r1, %[iter] 	\n\t"
			" 2:				\n\t"
			" subs r1, r1, #0x1 \n\t"
			" bne   2b    		\n\t"
			" subs r0,r0,#0x1 	\n\t"
			"  bne 1b 			\n\t"
			" pop {r0,r1} 		\n\t"
			:: [iter] "r" (ITERS_PER_SEC), [sec] "r" (seconds)
	);
	mtcpsr(currmask);
#endif
}
