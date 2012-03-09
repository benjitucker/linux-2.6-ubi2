#ifndef __UM_IO_H
#define __UM_IO_H

#include "asm/page.h"

#define IO_SPACE_LIMIT 0xdeadbeef /* Sure hope nothing uses this */

static inline int inb(unsigned long i) { return(0); }
static inline void outb(char c, unsigned long i) { }

/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa((void *) address);
}

static inline void * phys_to_virt(unsigned long address)
{
	return __va(address);
}

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

extern inline u8 readb(const volatile void __iomem *addr)
{
//	printk("readb %p\n", addr);
	return 0;
}
extern inline u16 readw(const volatile void __iomem *addr)
{
//	printk("readw %p\n", addr);
	return 0;
}
static inline u32 readl(const volatile void __iomem *addr)
{
//	printk("readl %p\n", addr);
	return 0;
}
static inline u64 readq(const volatile void __iomem *addr)
{
//	printk("readq %p\n", addr);
	return 0;
}
static inline void writeb(unsigned char b, volatile void __iomem *addr)
{
//	printk("writeb %p=0x%02x\n", addr, b);
	*(volatile unsigned char __force *) addr = b;
}
static inline void writew(unsigned short b, volatile void __iomem *addr)
{
//	printk("writew %p=0x%04x\n", addr, b);
	*(volatile unsigned short __force *) addr = b;
}
static inline void writel(unsigned int b, volatile void __iomem *addr)
{
//	printk("writel %p=0x%08x\n", addr, b);
	*(volatile unsigned int __force *) addr = b;
}
static inline void writeq(unsigned int b, volatile void __iomem *addr)
{
//	printk("writeq %p=0x%08x\n", addr, b);
	*(volatile unsigned long long __force *) addr = b;
}
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define __raw_writeq writeq
 
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_readq readq
 
#define readb(c)		({ u8  __v = readb_relaxed(c); __iormb(); __v; })
#define readw(c)		({ u16 __v = readw_relaxed(c); __iormb(); __v; })
#define readl(c)		({ u32 __v = readl_relaxed(c); __iormb(); __v; })

#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)
#define readq_relaxed(addr) readq(addr)

#define __io_virt(x) ((void __force *) (x))
#define memset_io(a, b, c)	memset(__io_virt(a), (b), (c))
#define memcpy_fromio(a, b, c)	memcpy((a), __io_virt(b), (c))
#define memcpy_toio(a, b, c)	memcpy(__io_virt(a), (b), (c))


#define __iormb()		do { } while (0)
#define __iowmb()		do { } while (0)

#endif
