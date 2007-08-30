/* $Id: testrescue.S 2386 2006-11-01 04:57:26Z magicyang $
 *
 * Simple testcode to download by the rescue block.
 * Just lits some LEDs to show it was downloaded correctly.
 * 
 * Copyright (C) 1999 Axis Communications AB
 */

#define ASSEMBLER_MACROS_ONLY
#include <asm/sv_addr_ag.h>

	.text

	nop
	nop
	moveq	-1, $r2
	move.b	$r2, [R_PORT_PA_DIR]
	moveq	0, $r2
	move.b	$r2, [R_PORT_PA_DATA]	

endless:
	nop
	ba	endless
	nop

	
