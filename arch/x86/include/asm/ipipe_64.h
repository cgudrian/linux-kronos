/*   -*- linux-c -*-
 *   arch/x86/include/asm/ipipe_64.h
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

#ifndef __X86_IPIPE_64_H
#define __X86_IPIPE_64_H

#include <asm/ptrace.h>
#include <asm/irq.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/ipipe_percpu.h>
#ifdef CONFIG_SMP
#include <asm/mpspec.h>
#include <linux/thread_info.h>
#endif

#define ipipe_read_tsc(t)  do {		\
	unsigned int __a,__d;			\
	asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
	(t) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)

extern unsigned cpu_khz;
#define ipipe_cpu_freq() ({ unsigned long __freq = (1000UL * cpu_khz); __freq; })
#define ipipe_tsc2ns(t)	(((t) * 1000UL) / (ipipe_cpu_freq() / 1000000UL))
#define ipipe_tsc2us(t)	((t) / (ipipe_cpu_freq() / 1000000UL))

/* Private interface -- Internal use only */

int __ipipe_handle_irq(struct pt_regs *regs);

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
      __asm__("bsrq %1, %0":"=r"(ul)
	      :	"rm"(ul));
      return ul;
}

struct irq_desc;

void __ipipe_ack_edge_irq(unsigned irq, struct irq_desc *desc);

void __ipipe_end_edge_irq(unsigned irq, struct irq_desc *desc);

static inline void __ipipe_call_root_xirq_handler(unsigned irq,
						  void (*handler)(unsigned, void *))
{
	struct pt_regs *regs = &__raw_get_cpu_var(__ipipe_tick_regs);

	regs->orig_ax = ~__ipipe_get_irq_vector(irq);

	__asm__ __volatile__("movq  %%rsp, %%rax\n\t"
			     "pushq $0\n\t"
			     "pushq %%rax\n\t"
			     "pushfq\n\t"
			     "pushq %[kernel_cs]\n\t"
			     "pushq $__xirq_end\n\t"
			     "pushq %[vector]\n\t"
			     "subq  $9*8,%%rsp\n\t"
			     "movq  %%rdi,8*8(%%rsp)\n\t"
			     "movq  %%rsi,7*8(%%rsp)\n\t"
			     "movq  %%rdx,6*8(%%rsp)\n\t"
			     "movq  %%rcx,5*8(%%rsp)\n\t"
			     "movq  %%rax,4*8(%%rsp)\n\t"
			     "movq  %%r8,3*8(%%rsp)\n\t"
			     "movq  %%r9,2*8(%%rsp)\n\t"
			     "movq  %%r10,1*8(%%rsp)\n\t"
			     "movq  %%r11,(%%rsp)\n\t"
			     "call  *%[handler]\n\t"
			     "cli\n\t"
			     "jmp exit_intr\n\t"
			     "__xirq_end: cli\n"
			     : /* no output */
			     : [kernel_cs] "i" (__KERNEL_CS),
			       [vector] "rm" (regs->orig_ax),
			       [handler] "r" (handler), "D" (regs)
			     : "rax");
}

void irq_enter(void);
void irq_exit(void);

static inline void __ipipe_call_root_virq_handler(unsigned irq,
						  void (*handler)(unsigned, void *),
						  void *cookie)
{
	irq_enter();
	__asm__ __volatile__("movq  %%rsp, %%rax\n\t"
			     "pushq $0\n\t"
			     "pushq %%rax\n\t"
			     "pushfq\n\t"
			     "pushq %[kernel_cs]\n\t"
			     "pushq $__virq_end\n\t"
			     "pushq $-1\n\t"
			     "subq  $9*8,%%rsp\n\t"
			     "movq  %%rdi,8*8(%%rsp)\n\t"
			     "movq  %%rsi,7*8(%%rsp)\n\t"
			     "movq  %%rdx,6*8(%%rsp)\n\t"
			     "movq  %%rcx,5*8(%%rsp)\n\t"
			     "movq  %%rax,4*8(%%rsp)\n\t"
			     "movq  %%r8,3*8(%%rsp)\n\t"
			     "movq  %%r9,2*8(%%rsp)\n\t"
			     "movq  %%r10,1*8(%%rsp)\n\t"
			     "movq  %%r11,(%%rsp)\n\t"
			     "call  *%[handler]\n\t"
			     : /* no output */
			     : [kernel_cs] "i" (__KERNEL_CS),
			       [handler] "r" (handler), "D" (irq), "S" (cookie)
			     : "rax");
	irq_exit();
	__asm__ __volatile__("cli\n\t"
			     "jmp exit_intr\n\t"
			     "__virq_end: cli\n"
			     : /* no output */
			     : /* no input */);
}

/*
 * When running handlers, enable hw interrupts for all domains but the
 * one heading the pipeline, so that IRQs can never be significantly
 * deferred for the latter.
 */
#define __ipipe_run_isr(ipd, irq)					\
	do {								\
		if (!__ipipe_pipeline_head_p(ipd))			\
			local_irq_enable_hw();				\
		if (ipd == ipipe_root_domain) {				\
			if (likely(!ipipe_virtual_irq_p(irq))) 		\
				__ipipe_call_root_xirq_handler(		\
					irq, (ipd)->irqs[irq].handler);	\
			else						\
				__ipipe_call_root_virq_handler(		\
					irq, (ipd)->irqs[irq].handler,	\
					(ipd)->irqs[irq].cookie);	\
		} else {						\
			__clear_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie); \
			__set_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
		}							\
		local_irq_disable_hw();					\
	} while(0)

#endif	/* !__X86_IPIPE_64_H */
