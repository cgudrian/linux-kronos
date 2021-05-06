/*   -*- linux-c -*-
 *   include/linux/ipipe_percpu.h
 *
 *   Copyright (C) 2007 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __LINUX_IPIPE_PERCPU_H
#define __LINUX_IPIPE_PERCPU_H

#include <asm/percpu.h>
#include <asm/ptrace.h>

struct ipipe_domain;

struct ipipe_percpu_domain_data {
	unsigned long status;	/* <= Must be first in struct. */
	unsigned long irqpend_himap;
#ifdef __IPIPE_3LEVEL_IRQMAP
	unsigned long irqpend_mdmap[IPIPE_IRQ_MDMAPSZ];
#endif
	unsigned long irqpend_lomap[IPIPE_IRQ_LOMAPSZ];
	unsigned long irqheld_map[IPIPE_IRQ_LOMAPSZ];
	unsigned long irqall[IPIPE_NR_IRQS];
	u64 evsync;
};

/*
 * CAREFUL: all accessors based on __raw_get_cpu_var() you may find in
 * this file should be used only while hw interrupts are off, to
 * prevent from CPU migration regardless of the running domain.
 */
#ifdef CONFIG_SMP
#define ipipe_percpudom_ptr(ipd, cpu)	\
	(&per_cpu(ipipe_percpu_darray, cpu)[(ipd)->slot])
#define ipipe_cpudom_ptr(ipd)	\
	(&__ipipe_get_cpu_var(ipipe_percpu_darray)[(ipd)->slot])
#else
DECLARE_PER_CPU(struct ipipe_percpu_domain_data *, ipipe_percpu_daddr[CONFIG_IPIPE_DOMAINS]);
#define ipipe_percpudom_ptr(ipd, cpu)	\
	(per_cpu(ipipe_percpu_daddr, cpu)[(ipd)->slot])
#define ipipe_cpudom_ptr(ipd)	\
	(__ipipe_get_cpu_var(ipipe_percpu_daddr)[(ipd)->slot])
#endif
#define ipipe_percpudom(ipd, var, cpu)	(ipipe_percpudom_ptr(ipd, cpu)->var)
#define ipipe_cpudom_var(ipd, var)	(ipipe_cpudom_ptr(ipd)->var)

#define IPIPE_ROOT_SLOT			0
#define IPIPE_HEAD_SLOT			(CONFIG_IPIPE_DOMAINS - 1)

DECLARE_PER_CPU(struct ipipe_percpu_domain_data, ipipe_percpu_darray[CONFIG_IPIPE_DOMAINS]);

DECLARE_PER_CPU(struct ipipe_domain *, ipipe_percpu_domain);

DECLARE_PER_CPU(unsigned long, ipipe_nmi_saved_root);

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT
DECLARE_PER_CPU(int, ipipe_percpu_context_check);
DECLARE_PER_CPU(int, ipipe_saved_context_check_state);
#endif

#define ipipe_root_cpudom_ptr(var)	\
	(&__ipipe_get_cpu_var(ipipe_percpu_darray)[IPIPE_ROOT_SLOT])

#define ipipe_root_cpudom_var(var)	ipipe_root_cpudom_ptr()->var

#define ipipe_this_cpudom_var(var)	\
	ipipe_cpudom_var(__ipipe_current_domain, var)

#define ipipe_head_cpudom_ptr()		\
	(&__ipipe_get_cpu_var(ipipe_percpu_darray)[IPIPE_HEAD_SLOT])

#define ipipe_head_cpudom_var(var)	ipipe_head_cpudom_ptr()->var

#endif	/* !__LINUX_IPIPE_PERCPU_H */
