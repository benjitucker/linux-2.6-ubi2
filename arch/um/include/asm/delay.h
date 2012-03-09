#ifndef __UM_DELAY_H
#define __UM_DELAY_H

/* Undefined on purpose */
extern void __bad_udelay(void);
extern void __bad_ndelay(void);

extern void __udelay(unsigned long usecs);
extern void __ndelay(unsigned long usecs);
extern void __delay(unsigned long loops);

#define udelay(n) ((__builtin_constant_p(n) && (n) > 20000) ? \
	__bad_udelay() : __udelay(n))

#define ndelay(n) ((__builtin_constant_p(n) && (n) > 20000) ? \
	__bad_ndelay() : __ndelay(n))
//extern void __unimplemented_ndelay(void);
//#define ndelay(n) __unimplemented_ndelay()
#define ndelay(n)		do { } while (0)

#endif
