/* $Id: reg_rdwr.h 2387 2006-11-01 05:32:21Z magicyang $
 *
 * Read/write register macros used by *_defs.h
 */

#ifndef reg_rdwr_h
#define reg_rdwr_h


#define REG_READ(type, addr) *((volatile type *) (addr))

#define REG_WRITE(type, addr, val) \
   do { *((volatile type *) (addr)) = (val); } while(0)

#endif
