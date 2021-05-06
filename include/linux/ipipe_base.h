/* -*- linux-c -*-
 * include/linux/ipipe_base.h
 *
 * Copyright (C) 2002-2007 Philippe Gerum.
 *               2007 Jan Kiszka.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __LINUX_IPIPE_BASE_H
#define __LINUX_IPIPE_BASE_H

#ifdef CONFIG_IPIPE

#include <asm/ipipe_base.h>

#define __bpl_up(x)		(((x)+(BITS_PER_LONG-1)) & ~(BITS_PER_LONG-1))
/* Number of virtual IRQs (must be a multiple of BITS_PER_LONG) */
#define IPIPE_NR_VIRQS		BITS_PER_LONG
/* First virtual IRQ # (must be aligned on BITS_PER_LONG) */
#define IPIPE_VIRQ_BASE		__bpl_up(IPIPE_NR_XIRQS)
/* Total number of IRQ slots */
#define IPIPE_NR_IRQS		(IPIPE_VIRQ_BASE+IPIPE_NR_VIRQS)

#define IPIPE_IRQ_LOMAPSZ	(IPIPE_NR_IRQS / BITS_PER_LONG)
#if IPIPE_IRQ_LOMAPSZ > BITS_PER_LONG
/*
 * We need a 3-level mapping. This allows us to handle up to 32k IRQ
 * vectors on 32bit machines, 256k on 64bit ones.
 */
#define __IPIPE_3LEVEL_IRQMAP	1
#define IPIPE_IRQ_MDMAPSZ	(__bpl_up(IPIPE_IRQ_LOMAPSZ) / BITS_PER_LONG)
#else
/*
 * 2-level mapping is enough. This allows us to handle up to 1024 IRQ
 * vectors on 32bit machines, 4096 on 64bit ones.
 */
#define __IPIPE_2LEVEL_IRQMAP	1
#endif

#define IPIPE_IRQ_DOALL		0
#define IPIPE_IRQ_DOVIRT	1

/* Per-cpu pipeline status */
#define IPIPE_STALL_FLAG	0	/* Stalls a pipeline stage -- guaranteed at bit #0 */
#define IPIPE_SYNC_FLAG		1	/* The interrupt syncer is running for the domain */
#define IPIPE_NOSTACK_FLAG	2	/* Domain currently runs on a foreign stack */

#define IPIPE_STALL_MASK	(1L << IPIPE_STALL_FLAG)
#define IPIPE_SYNC_MASK		(1L << IPIPE_SYNC_FLAG)
#define IPIPE_NOSTACK_MASK	(1L << IPIPE_NOSTACK_FLAG)

typedef void (*ipipe_irq_handler_t)(unsigned int irq,
				    void *cookie);

extern struct ipipe_domain ipipe_root;

#define ipipe_root_domain (&ipipe_root)

void __ipipe_unstall_root(void);

void __ipipe_restore_root(unsigned long x);

#define ipipe_preempt_disable(flags)		\
	do {					\
		local_irq_save_hw(flags);	\
		if (__ipipe_root_domain_p)	\
			preempt_disable();	\
	} while (0)

#define ipipe_preempt_enable(flags)			\
	do {						\
		if (__ipipe_root_domain_p) {		\
			preempt_enable_no_resched();	\
			local_irq_restore_hw(flags);	\
			preempt_check_resched();	\
		} else					\
			local_irq_restore_hw(flags);	\
	} while (0)
 
#ifdef CONFIG_IPIPE_DEBUG_CONTEXT
void ipipe_check_context(struct ipipe_domain *border_ipd);
#else /* !CONFIG_IPIPE_DEBUG_CONTEXT */
static inline void ipipe_check_context(struct ipipe_domain *border_ipd) { }
#endif /* !CONFIG_IPIPE_DEBUG_CONTEXT */

/* Generic features */

#ifdef CONFIG_GENERIC_CLOCKEVENTS
#define __IPIPE_FEATURE_REQUEST_TICKDEV    1
#endif
#define __IPIPE_FEATURE_DELAYED_ATOMICSW   1
#define __IPIPE_FEATURE_FASTPEND_IRQ       1
#define __IPIPE_FEATURE_TRACE_EVENT	   1

#else /* !CONFIG_IPIPE */
#define ipipe_preempt_disable(flags)	do { \
						preempt_disable(); \
						(void)(flags); \
					} while (0)
#define ipipe_preempt_enable(flags)	preempt_enable()
#define ipipe_check_context(ipd)	do { } while(0)
#endif	/* CONFIG_IPIPE */

#endif	/* !__LINUX_IPIPE_BASE_H */
