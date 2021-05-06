/*   -*- linux-c -*-
 *   arch/x86/include/asm/ipipe_base.h
 *
 *   Copyright (C) 2007-2009 Philippe Gerum.
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

#ifndef __X86_IPIPE_BASE_H
#define __X86_IPIPE_BASE_H

#include <linux/threads.h>
#include <asm/apicdef.h>
#include <asm/irq_vectors.h>

#ifdef CONFIG_X86_32
#define IPIPE_NR_FAULTS		33 /* 32 from IDT + iret_error */
#else
#define IPIPE_NR_FAULTS		32
#endif

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_LOCAL_APIC)
/*
 * System interrupts are mapped beyond the last defined external IRQ
 * number.
 */
#define IPIPE_NR_XIRQS		(NR_IRQS + 32)
#define IPIPE_FIRST_APIC_IRQ	NR_IRQS
#define IPIPE_SERVICE_VECTOR0	(INVALIDATE_TLB_VECTOR_END + 1)
#define IPIPE_SERVICE_IPI0	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR0)
#define IPIPE_SERVICE_VECTOR1	(INVALIDATE_TLB_VECTOR_END + 2)
#define IPIPE_SERVICE_IPI1	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR1)
#define IPIPE_SERVICE_VECTOR2	(INVALIDATE_TLB_VECTOR_END + 3)
#define IPIPE_SERVICE_IPI2	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR2)
#define IPIPE_SERVICE_VECTOR3	(INVALIDATE_TLB_VECTOR_END + 4)
#define IPIPE_SERVICE_IPI3	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR3)
#ifdef CONFIG_SMP
#define IPIPE_CRITICAL_VECTOR	(INVALIDATE_TLB_VECTOR_END + 5)
#define IPIPE_CRITICAL_IPI	ipipe_apic_vector_irq(IPIPE_CRITICAL_VECTOR)
#endif
#define ipipe_apic_irq_vector(irq)  ((irq) - IPIPE_FIRST_APIC_IRQ + FIRST_SYSTEM_VECTOR)
#define ipipe_apic_vector_irq(vec)  ((vec) - FIRST_SYSTEM_VECTOR + IPIPE_FIRST_APIC_IRQ)
#else /* !(CONFIG_X86_64 || CONFIG_X86_LOCAL_APIC) */
#define IPIPE_NR_XIRQS		NR_IRQS
#endif /* !(CONFIG_X86_64 || CONFIG_X86_LOCAL_APIC) */

/* Pseudo-vectors used for kernel events */
#define IPIPE_FIRST_EVENT	IPIPE_NR_FAULTS
#define IPIPE_EVENT_SYSCALL	(IPIPE_FIRST_EVENT)
#define IPIPE_EVENT_SCHEDULE	(IPIPE_FIRST_EVENT + 1)
#define IPIPE_EVENT_SIGWAKE	(IPIPE_FIRST_EVENT + 2)
#define IPIPE_EVENT_SETSCHED	(IPIPE_FIRST_EVENT + 3)
#define IPIPE_EVENT_INIT	(IPIPE_FIRST_EVENT + 4)
#define IPIPE_EVENT_EXIT	(IPIPE_FIRST_EVENT + 5)
#define IPIPE_EVENT_CLEANUP	(IPIPE_FIRST_EVENT + 6)
#define IPIPE_LAST_EVENT	IPIPE_EVENT_CLEANUP
#define IPIPE_NR_EVENTS		(IPIPE_LAST_EVENT + 1)

#define ex_do_divide_error			0
#define ex_do_debug				1
/* NMI not pipelined. */
#define ex_do_int3				3
#define ex_do_overflow				4
#define ex_do_bounds				5
#define ex_do_invalid_op			6
#define ex_do_device_not_available		7
/* Double fault not pipelined. */
#define ex_do_coprocessor_segment_overrun	9
#define ex_do_invalid_TSS			10
#define ex_do_segment_not_present		11
#define ex_do_stack_segment			12
#define ex_do_general_protection		13
#define ex_do_page_fault			14
#define ex_do_spurious_interrupt_bug		15
#define ex_do_coprocessor_error			16
#define ex_do_alignment_check			17
#define ex_machine_check_vector			18
#define ex_reserved				ex_machine_check_vector
#define ex_do_simd_coprocessor_error		19
#define ex_do_iret_error			32

#ifndef __ASSEMBLY__

#ifdef CONFIG_SMP

#include <asm/alternative.h>

#ifdef CONFIG_X86_32
#define GET_ROOT_STATUS_ADDR					\
	"pushfl; cli;"						\
	"movl %%fs:per_cpu__this_cpu_off, %%eax;"		\
	"lea per_cpu__ipipe_percpu_darray(%%eax), %%eax;"
#define PUT_ROOT_STATUS_ADDR	"popfl;"
#define TEST_AND_SET_ROOT_STATUS \
	"btsl $0,(%%eax);"
#define TEST_ROOT_STATUS \
	"btl $0,(%%eax);"
#define ROOT_TEST_CLOBBER_LIST  "eax"
#else /* CONFIG_X86_64 */
#define GET_ROOT_STATUS_ADDR					\
	"pushfq; cli;"						\
	"movq %%gs:per_cpu__this_cpu_off, %%rax;"		\
	"lea per_cpu__ipipe_percpu_darray(%%rax), %%rax;"
#define PUT_ROOT_STATUS_ADDR	"popfq;"
#define TEST_AND_SET_ROOT_STATUS \
	"btsl $0,(%%rax);"
#define TEST_ROOT_STATUS \
	"btl $0,(%%rax);"
#define ROOT_TEST_CLOBBER_LIST  "rax"
#endif /* CONFIG_X86_64 */

static inline void __ipipe_stall_root(void)
{
	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     LOCK_PREFIX
			     TEST_AND_SET_ROOT_STATUS
			     PUT_ROOT_STATUS_ADDR
			     : : : ROOT_TEST_CLOBBER_LIST, "memory");
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	int oldbit;

	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     LOCK_PREFIX
			     TEST_AND_SET_ROOT_STATUS
			     "sbbl %0,%0;"
			     PUT_ROOT_STATUS_ADDR
			     :"=r" (oldbit)
			     : : ROOT_TEST_CLOBBER_LIST, "memory");
	return oldbit;
}

static inline unsigned long __ipipe_test_root(void)
{
	int oldbit;

	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     TEST_ROOT_STATUS
			     "sbbl %0,%0;"
			     PUT_ROOT_STATUS_ADDR
			     :"=r" (oldbit)
			     : : ROOT_TEST_CLOBBER_LIST);
	return oldbit;
}

#else /* !CONFIG_SMP */

#if __GNUC__ >= 4
/* Alias to ipipe_root_cpudom_var(status) */
extern unsigned long __ipipe_root_status;
#else
extern unsigned long *const __ipipe_root_status_addr;
#define __ipipe_root_status	(*__ipipe_root_status_addr)
#endif

static inline void __ipipe_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	__asm__ __volatile__("btsl $0,%0;"
			     :"+m" (*p) : : "memory");
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	int oldbit;

	__asm__ __volatile__("btsl $0,%1;"
			     "sbbl %0,%0;"
			     :"=r" (oldbit), "+m" (*p)
			     : : "memory");
	return oldbit;
}

static inline unsigned long __ipipe_test_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	int oldbit;

	__asm__ __volatile__("btl $0,%1;"
			     "sbbl %0,%0;"
			     :"=r" (oldbit)
			     :"m" (*p));
	return oldbit;
}

#endif /* !CONFIG_SMP */

void __ipipe_halt_root(void);

void __ipipe_serial_debug(const char *fmt, ...);

#endif	/* !__ASSEMBLY__ */

#endif	/* !__X86_IPIPE_BASE_H */
