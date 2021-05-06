/* -*- linux-c -*-
 * include/linux/ipipe.h
 *
 * Copyright (C) 2002-2007 Philippe Gerum.
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

#ifndef __LINUX_IPIPE_H
#define __LINUX_IPIPE_H

#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/percpu.h>
#include <linux/mutex.h>
#include <linux/linkage.h>
#include <linux/ipipe_base.h>
#include <linux/ipipe_compat.h>
#include <asm/ipipe.h>

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT

#include <linux/cpumask.h>
#include <asm/system.h>

static inline int ipipe_disable_context_check(int cpu)
{
	return xchg(&per_cpu(ipipe_percpu_context_check, cpu), 0);
}

static inline void ipipe_restore_context_check(int cpu, int old_state)
{
	per_cpu(ipipe_percpu_context_check, cpu) = old_state;
}

static inline void ipipe_context_check_off(void)
{
	int cpu;
	for_each_online_cpu(cpu)
		per_cpu(ipipe_percpu_context_check, cpu) = 0;
}

#else	/* !CONFIG_IPIPE_DEBUG_CONTEXT */

static inline int ipipe_disable_context_check(int cpu)
{
	return 0;
}

static inline void ipipe_restore_context_check(int cpu, int old_state) { }

static inline void ipipe_context_check_off(void) { }

#endif	/* !CONFIG_IPIPE_DEBUG_CONTEXT */

#ifdef CONFIG_IPIPE

#define IPIPE_VERSION_STRING	IPIPE_ARCH_STRING
#define IPIPE_RELEASE_NUMBER	((IPIPE_MAJOR_NUMBER << 16) | \
				 (IPIPE_MINOR_NUMBER <<  8) | \
				 (IPIPE_PATCH_NUMBER))

#ifndef BROKEN_BUILTIN_RETURN_ADDRESS
#define __BUILTIN_RETURN_ADDRESS0 ((unsigned long)__builtin_return_address(0))
#define __BUILTIN_RETURN_ADDRESS1 ((unsigned long)__builtin_return_address(1))
#endif /* !BUILTIN_RETURN_ADDRESS */

#define IPIPE_ROOT_PRIO		100
#define IPIPE_ROOT_ID		0
#define IPIPE_ROOT_NPTDKEYS	4	/* Must be <= BITS_PER_LONG */

#define IPIPE_RESET_TIMER	0x1
#define IPIPE_GRAB_TIMER	0x2

/* Global domain flags */
#define IPIPE_SPRINTK_FLAG	0	/* Synchronous printk() allowed */
#define IPIPE_AHEAD_FLAG	1	/* Domain always heads the pipeline */

/* Interrupt control bits */
#define IPIPE_HANDLE_FLAG	0
#define IPIPE_PASS_FLAG		1
#define IPIPE_ENABLE_FLAG	2
#define IPIPE_DYNAMIC_FLAG	IPIPE_HANDLE_FLAG
#define IPIPE_STICKY_FLAG	3
#define IPIPE_SYSTEM_FLAG	4
#define IPIPE_LOCK_FLAG		5
#define IPIPE_WIRED_FLAG	6
#define IPIPE_EXCLUSIVE_FLAG	7

#define IPIPE_HANDLE_MASK	(1 << IPIPE_HANDLE_FLAG)
#define IPIPE_PASS_MASK		(1 << IPIPE_PASS_FLAG)
#define IPIPE_ENABLE_MASK	(1 << IPIPE_ENABLE_FLAG)
#define IPIPE_DYNAMIC_MASK	IPIPE_HANDLE_MASK
#define IPIPE_STICKY_MASK	(1 << IPIPE_STICKY_FLAG)
#define IPIPE_SYSTEM_MASK	(1 << IPIPE_SYSTEM_FLAG)
#define IPIPE_LOCK_MASK		(1 << IPIPE_LOCK_FLAG)
#define IPIPE_WIRED_MASK	(1 << IPIPE_WIRED_FLAG)
#define IPIPE_EXCLUSIVE_MASK	(1 << IPIPE_EXCLUSIVE_FLAG)

#define IPIPE_DEFAULT_MASK	(IPIPE_HANDLE_MASK|IPIPE_PASS_MASK)
#define IPIPE_STDROOT_MASK	(IPIPE_HANDLE_MASK|IPIPE_PASS_MASK|IPIPE_SYSTEM_MASK)

#define IPIPE_EVENT_SELF        0x80000000

#define IPIPE_NR_CPUS		NR_CPUS

/* This accessor assumes hw IRQs are off on SMP; allows assignment. */
#define __ipipe_current_domain	__ipipe_get_cpu_var(ipipe_percpu_domain)
/* This read-only accessor makes sure that hw IRQs are off on SMP. */
#define ipipe_current_domain				\
	({						\
		struct ipipe_domain *__ipd__;		\
		unsigned long __flags__;		\
		local_irq_save_hw_smp(__flags__);	\
		__ipd__ = __ipipe_current_domain;	\
		local_irq_restore_hw_smp(__flags__);	\
		__ipd__;				\
	})

#define ipipe_virtual_irq_p(irq)	((irq) >= IPIPE_VIRQ_BASE && \
					 (irq) < IPIPE_NR_IRQS)

#define IPIPE_SAME_HANDLER	((ipipe_irq_handler_t)(-1))

struct irq_desc;

typedef void (*ipipe_irq_ackfn_t)(unsigned irq, struct irq_desc *desc);

typedef int (*ipipe_event_handler_t)(unsigned event,
				     struct ipipe_domain *from,
				     void *data);
struct ipipe_domain {

	int slot;			/* Slot number in percpu domain data array. */
	struct list_head p_link;	/* Link in pipeline */
	ipipe_event_handler_t evhand[IPIPE_NR_EVENTS]; /* Event handlers. */
	unsigned long long evself;	/* Self-monitored event bits. */

	struct irqdesc {
		unsigned long control;
		ipipe_irq_ackfn_t acknowledge;
		ipipe_irq_handler_t handler;
		void *cookie;
	} ____cacheline_aligned irqs[IPIPE_NR_IRQS];

	int priority;
	void *pdd;
	unsigned long flags;
	unsigned domid;
	const char *name;
	struct mutex mutex;
};

#define IPIPE_HEAD_PRIORITY	(-1) /* For domains always heading the pipeline */

struct ipipe_domain_attr {

	unsigned domid;		/* Domain identifier -- Magic value set by caller */
	const char *name;	/* Domain name -- Warning: won't be dup'ed! */
	int priority;		/* Priority in interrupt pipeline */
	void (*entry) (void);	/* Domain entry point */
	void *pdd;		/* Per-domain (opaque) data pointer */
};

#define __ipipe_irq_cookie(ipd, irq)		(ipd)->irqs[irq].cookie
#define __ipipe_irq_handler(ipd, irq)		(ipd)->irqs[irq].handler
#define __ipipe_cpudata_irq_hits(ipd, cpu, irq)	ipipe_percpudom(ipd, irqall, cpu)[irq]

extern unsigned __ipipe_printk_virq;

extern unsigned long __ipipe_virtual_irq_map;

extern struct list_head __ipipe_pipeline;

extern int __ipipe_event_monitors[];

/* Private interface */

void ipipe_init_early(void);

void ipipe_init(void);

#ifdef CONFIG_PROC_FS
void ipipe_init_proc(void);

#ifdef CONFIG_IPIPE_TRACE
void __ipipe_init_tracer(void);
#else /* !CONFIG_IPIPE_TRACE */
#define __ipipe_init_tracer()       do { } while(0)
#endif /* CONFIG_IPIPE_TRACE */

#else	/* !CONFIG_PROC_FS */
#define ipipe_init_proc()	do { } while(0)
#endif	/* CONFIG_PROC_FS */

void __ipipe_init_stage(struct ipipe_domain *ipd);

void __ipipe_cleanup_domain(struct ipipe_domain *ipd);

void __ipipe_add_domain_proc(struct ipipe_domain *ipd);

void __ipipe_remove_domain_proc(struct ipipe_domain *ipd);

void __ipipe_flush_printk(unsigned irq, void *cookie);

void __ipipe_walk_pipeline(struct list_head *pos);

void __ipipe_pend_irq(unsigned irq, struct list_head *head);

int __ipipe_dispatch_event(unsigned event, void *data);

void __ipipe_dispatch_wired_nocheck(struct ipipe_domain *head, unsigned irq);

void __ipipe_dispatch_wired(struct ipipe_domain *head, unsigned irq);

void __ipipe_sync_stage(int dovirt);

void __ipipe_set_irq_pending(struct ipipe_domain *ipd, unsigned irq);

void __ipipe_lock_irq(struct ipipe_domain *ipd, int cpu, unsigned irq);

void __ipipe_unlock_irq(struct ipipe_domain *ipd, unsigned irq);

void __ipipe_pin_range_globally(unsigned long start, unsigned long end);

/* Must be called hw IRQs off. */
static inline void ipipe_irq_lock(unsigned irq)
{
	__ipipe_lock_irq(__ipipe_current_domain, ipipe_processor_id(), irq);
}

/* Must be called hw IRQs off. */
static inline void ipipe_irq_unlock(unsigned irq)
{
	__ipipe_unlock_irq(__ipipe_current_domain, irq);
}

#ifndef __ipipe_sync_pipeline
#define __ipipe_sync_pipeline(dovirt) __ipipe_sync_stage(dovirt)
#endif

#ifndef __ipipe_run_irqtail
#define __ipipe_run_irqtail() do { } while(0)
#endif

#define __ipipe_pipeline_head_p(ipd) (&(ipd)->p_link == __ipipe_pipeline.next)

#define __ipipe_ipending_p(p)	((p)->irqpend_himap != 0)

/*
 * Keep the following as a macro, so that client code could check for
 * the support of the invariant pipeline head optimization.
 */
#define __ipipe_pipeline_head() \
	list_entry(__ipipe_pipeline.next, struct ipipe_domain, p_link)

#define local_irq_enable_hw_cond()		local_irq_enable_hw()
#define local_irq_disable_hw_cond()		local_irq_disable_hw()
#define local_irq_save_hw_cond(flags)		local_irq_save_hw(flags)
#define local_irq_restore_hw_cond(flags)	local_irq_restore_hw(flags)

#ifdef CONFIG_SMP
cpumask_t __ipipe_set_irq_affinity(unsigned irq, cpumask_t cpumask);
int __ipipe_send_ipi(unsigned ipi, cpumask_t cpumask);
#define local_irq_save_hw_smp(flags)		local_irq_save_hw(flags)
#define local_irq_restore_hw_smp(flags)		local_irq_restore_hw(flags)
#else /* !CONFIG_SMP */
#define local_irq_save_hw_smp(flags)		do { (void)(flags); } while(0)
#define local_irq_restore_hw_smp(flags)		do { } while(0)
#endif /* CONFIG_SMP */

#define local_irq_save_full(vflags, rflags)		\
	do {						\
		local_irq_save(vflags);			\
		local_irq_save_hw(rflags);		\
	} while(0)

#define local_irq_restore_full(vflags, rflags)		\
	do {						\
		local_irq_restore_hw(rflags);		\
		local_irq_restore(vflags);		\
	} while(0)

static inline void __local_irq_restore_nosync(unsigned long x)
{
	struct ipipe_percpu_domain_data *p = ipipe_root_cpudom_ptr();

	if (raw_irqs_disabled_flags(x)) {
		set_bit(IPIPE_STALL_FLAG, &p->status);
		trace_hardirqs_off();
	} else {
		trace_hardirqs_on();
		clear_bit(IPIPE_STALL_FLAG, &p->status);
	}
}

static inline void local_irq_restore_nosync(unsigned long x)
{
	unsigned long flags;
	local_irq_save_hw_smp(flags);
	__local_irq_restore_nosync(x);
	local_irq_restore_hw_smp(flags);
}

#define __ipipe_root_domain_p	(__ipipe_current_domain == ipipe_root_domain)
#define ipipe_root_domain_p	(ipipe_current_domain == ipipe_root_domain)

static inline int __ipipe_event_monitored_p(int ev)
{
	if (__ipipe_event_monitors[ev] > 0)
		return 1;

	return (ipipe_current_domain->evself & (1LL << ev)) != 0;
}

#define ipipe_sigwake_notify(p)	\
do {					\
	if (((p)->flags & PF_EVNOTIFY) && __ipipe_event_monitored_p(IPIPE_EVENT_SIGWAKE)) \
		__ipipe_dispatch_event(IPIPE_EVENT_SIGWAKE, p);		\
} while(0)

#define ipipe_exit_notify(p)	\
do {				\
	if (((p)->flags & PF_EVNOTIFY) && __ipipe_event_monitored_p(IPIPE_EVENT_EXIT)) \
		__ipipe_dispatch_event(IPIPE_EVENT_EXIT, p);		\
} while(0)

#define ipipe_setsched_notify(p)	\
do {					\
	if (((p)->flags & PF_EVNOTIFY) && __ipipe_event_monitored_p(IPIPE_EVENT_SETSCHED)) \
		__ipipe_dispatch_event(IPIPE_EVENT_SETSCHED, p);	\
} while(0)

#define ipipe_schedule_notify(prev, next)				\
do {									\
	if ((((prev)->flags|(next)->flags) & PF_EVNOTIFY) &&		\
	    __ipipe_event_monitored_p(IPIPE_EVENT_SCHEDULE))		\
		__ipipe_dispatch_event(IPIPE_EVENT_SCHEDULE,next);	\
} while(0)

#define ipipe_trap_notify(ex, regs)					\
({									\
	unsigned long __flags__;					\
	int __ret__ = 0;						\
	local_irq_save_hw_smp(__flags__);				\
	if ((test_bit(IPIPE_NOSTACK_FLAG, &ipipe_this_cpudom_var(status)) || \
	     ((current)->flags & PF_EVNOTIFY)) &&			\
	    __ipipe_event_monitored_p(ex)) {				\
		local_irq_restore_hw_smp(__flags__);			\
		__ret__ = __ipipe_dispatch_event(ex, regs);		\
	} else								\
		local_irq_restore_hw_smp(__flags__);			\
	__ret__;							\
})

static inline void ipipe_init_notify(struct task_struct *p)
{
	if (__ipipe_event_monitored_p(IPIPE_EVENT_INIT))
		__ipipe_dispatch_event(IPIPE_EVENT_INIT, p);
}

struct mm_struct;

static inline void ipipe_cleanup_notify(struct mm_struct *mm)
{
	if (__ipipe_event_monitored_p(IPIPE_EVENT_CLEANUP))
		__ipipe_dispatch_event(IPIPE_EVENT_CLEANUP, mm);
}

/* Public interface */

int ipipe_register_domain(struct ipipe_domain *ipd,
			  struct ipipe_domain_attr *attr);

int ipipe_unregister_domain(struct ipipe_domain *ipd);

void ipipe_suspend_domain(void);

int ipipe_virtualize_irq(struct ipipe_domain *ipd,
			 unsigned irq,
			 ipipe_irq_handler_t handler,
			 void *cookie,
			 ipipe_irq_ackfn_t acknowledge,
			 unsigned modemask);

int ipipe_control_irq(unsigned irq,
		      unsigned clrmask,
		      unsigned setmask);

unsigned ipipe_alloc_virq(void);

int ipipe_free_virq(unsigned virq);

int ipipe_trigger_irq(unsigned irq);

static inline void __ipipe_propagate_irq(unsigned irq)
{
	struct list_head *next = __ipipe_current_domain->p_link.next;
	if (next == &ipipe_root.p_link) {
		/* Fast path: root must handle all interrupts. */
		__ipipe_set_irq_pending(&ipipe_root, irq);
		return;
	}
	__ipipe_pend_irq(irq, next);
}

static inline void __ipipe_schedule_irq(unsigned irq)
{
	__ipipe_pend_irq(irq, &__ipipe_current_domain->p_link);
}

static inline void __ipipe_schedule_irq_head(unsigned irq)
{
	__ipipe_set_irq_pending(__ipipe_pipeline_head(), irq);
}

static inline void __ipipe_schedule_irq_root(unsigned irq)
{
	__ipipe_set_irq_pending(&ipipe_root, irq);
}

static inline void ipipe_propagate_irq(unsigned irq)
{
	unsigned long flags;

	local_irq_save_hw(flags);
	__ipipe_propagate_irq(irq);
	local_irq_restore_hw(flags);
}

static inline void ipipe_schedule_irq(unsigned irq)
{
	unsigned long flags;

	local_irq_save_hw(flags);
	__ipipe_schedule_irq(irq);
	local_irq_restore_hw(flags);
}

static inline void ipipe_schedule_irq_head(unsigned irq)
{
	unsigned long flags;

	local_irq_save_hw(flags);
	__ipipe_schedule_irq_head(irq);
	local_irq_restore_hw(flags);
}

static inline void ipipe_schedule_irq_root(unsigned irq)
{
	unsigned long flags;

	local_irq_save_hw(flags);
	__ipipe_schedule_irq_root(irq);
	local_irq_restore_hw(flags);
}

void ipipe_stall_pipeline_from(struct ipipe_domain *ipd);

unsigned long ipipe_test_and_stall_pipeline_from(struct ipipe_domain *ipd);

unsigned long ipipe_test_and_unstall_pipeline_from(struct ipipe_domain *ipd);

static inline void ipipe_unstall_pipeline_from(struct ipipe_domain *ipd)
{
	ipipe_test_and_unstall_pipeline_from(ipd);
}

void ipipe_restore_pipeline_from(struct ipipe_domain *ipd,
					  unsigned long x);

static inline unsigned long ipipe_test_pipeline_from(struct ipipe_domain *ipd)
{
	return test_bit(IPIPE_STALL_FLAG, &ipipe_cpudom_var(ipd, status));
}

static inline void ipipe_stall_pipeline_head(void)
{
	local_irq_disable_hw();
	__set_bit(IPIPE_STALL_FLAG, &ipipe_head_cpudom_var(status));
}

static inline unsigned long ipipe_test_and_stall_pipeline_head(void)
{
	local_irq_disable_hw();
	return __test_and_set_bit(IPIPE_STALL_FLAG, &ipipe_head_cpudom_var(status));
}

void ipipe_unstall_pipeline_head(void);

void __ipipe_restore_pipeline_head(unsigned long x);

static inline void ipipe_restore_pipeline_head(unsigned long x)
{
	/* On some archs, __test_and_set_bit() might return different
	 * truth value than test_bit(), so we test the exclusive OR of
	 * both statuses, assuming that the lowest bit is always set in
	 * the truth value (if this is wrong, the failed optimization will
	 * be caught in __ipipe_restore_pipeline_head() if
	 * CONFIG_DEBUG_KERNEL is set). */
	if ((x ^ test_bit(IPIPE_STALL_FLAG, &ipipe_head_cpudom_var(status))) & 1)
		__ipipe_restore_pipeline_head(x);
}

#define ipipe_unstall_pipeline() \
	ipipe_unstall_pipeline_from(ipipe_current_domain)

#define ipipe_test_and_unstall_pipeline() \
	ipipe_test_and_unstall_pipeline_from(ipipe_current_domain)

#define ipipe_test_pipeline() \
	ipipe_test_pipeline_from(ipipe_current_domain)

#define ipipe_test_and_stall_pipeline() \
	ipipe_test_and_stall_pipeline_from(ipipe_current_domain)

#define ipipe_stall_pipeline() \
	ipipe_stall_pipeline_from(ipipe_current_domain)

#define ipipe_restore_pipeline(x) \
	ipipe_restore_pipeline_from(ipipe_current_domain, (x))

void ipipe_init_attr(struct ipipe_domain_attr *attr);

int ipipe_get_sysinfo(struct ipipe_sysinfo *sysinfo);

unsigned long ipipe_critical_enter(void (*syncfn) (void));

void ipipe_critical_exit(unsigned long flags);

static inline void ipipe_set_printk_sync(struct ipipe_domain *ipd)
{
	set_bit(IPIPE_SPRINTK_FLAG, &ipd->flags);
}

static inline void ipipe_set_printk_async(struct ipipe_domain *ipd)
{
	clear_bit(IPIPE_SPRINTK_FLAG, &ipd->flags);
}

static inline void ipipe_set_foreign_stack(struct ipipe_domain *ipd)
{
	/* Must be called hw interrupts off. */
	__set_bit(IPIPE_NOSTACK_FLAG, &ipipe_cpudom_var(ipd, status));
}

static inline void ipipe_clear_foreign_stack(struct ipipe_domain *ipd)
{
	/* Must be called hw interrupts off. */
	__clear_bit(IPIPE_NOSTACK_FLAG, &ipipe_cpudom_var(ipd, status));
}

static inline int ipipe_test_foreign_stack(void)
{
	/* Must be called hw interrupts off. */
	return test_bit(IPIPE_NOSTACK_FLAG, &ipipe_this_cpudom_var(status));
}

#ifndef ipipe_safe_current
#define ipipe_safe_current()					\
({								\
	struct task_struct *p;					\
	unsigned long flags;					\
	local_irq_save_hw_smp(flags);				\
	p = ipipe_test_foreign_stack() ? &init_task : current;	\
	local_irq_restore_hw_smp(flags);			\
	p; \
})
#endif

ipipe_event_handler_t ipipe_catch_event(struct ipipe_domain *ipd,
					unsigned event,
					ipipe_event_handler_t handler);

cpumask_t ipipe_set_irq_affinity(unsigned irq,
				 cpumask_t cpumask);

int ipipe_send_ipi(unsigned ipi,
		   cpumask_t cpumask);

int ipipe_setscheduler_root(struct task_struct *p,
			    int policy,
			    int prio);

int ipipe_reenter_root(struct task_struct *prev,
		       int policy,
		       int prio);

int ipipe_alloc_ptdkey(void);

int ipipe_free_ptdkey(int key);

int ipipe_set_ptd(int key,
		  void *value);

void *ipipe_get_ptd(int key);

int ipipe_disable_ondemand_mappings(struct task_struct *tsk);

static inline void ipipe_nmi_enter(void)
{
	int cpu = ipipe_processor_id();

	per_cpu(ipipe_nmi_saved_root, cpu) = ipipe_root_cpudom_var(status);
	__set_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT
	per_cpu(ipipe_saved_context_check_state, cpu) =
		ipipe_disable_context_check(cpu);
#endif /* CONFIG_IPIPE_DEBUG_CONTEXT */
}

static inline void ipipe_nmi_exit(void)
{
	int cpu = ipipe_processor_id();

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT
	ipipe_restore_context_check
		(cpu, per_cpu(ipipe_saved_context_check_state, cpu));
#endif /* CONFIG_IPIPE_DEBUG_CONTEXT */

	if (!test_bit(IPIPE_STALL_FLAG, &per_cpu(ipipe_nmi_saved_root, cpu)))
		__clear_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
}

#else	/* !CONFIG_IPIPE */

#define ipipe_init_early()		do { } while(0)
#define ipipe_init()			do { } while(0)
#define ipipe_suspend_domain()		do { } while(0)
#define ipipe_sigwake_notify(p)		do { } while(0)
#define ipipe_setsched_notify(p)	do { } while(0)
#define ipipe_init_notify(p)		do { } while(0)
#define ipipe_exit_notify(p)		do { } while(0)
#define ipipe_cleanup_notify(mm)	do { } while(0)
#define ipipe_trap_notify(t,r)		0
#define ipipe_init_proc()		do { } while(0)

static inline void __ipipe_pin_range_globally(unsigned long start,
					      unsigned long end)
{
}

static inline int ipipe_test_foreign_stack(void)
{
	return 0;
}

#define local_irq_enable_hw_cond()		do { } while(0)
#define local_irq_disable_hw_cond()		do { } while(0)
#define local_irq_save_hw_cond(flags)		do { (void)(flags); } while(0)
#define local_irq_restore_hw_cond(flags)	do { } while(0)
#define local_irq_save_hw_smp(flags)		do { (void)(flags); } while(0)
#define local_irq_restore_hw_smp(flags)		do { } while(0)

#define ipipe_irq_lock(irq)		do { } while(0)
#define ipipe_irq_unlock(irq)		do { } while(0)

#define __ipipe_root_domain_p		1
#define ipipe_root_domain_p		1
#define ipipe_safe_current		current
#define ipipe_processor_id()		smp_processor_id()

#define ipipe_nmi_enter()		do { } while (0)
#define ipipe_nmi_exit()		do { } while (0)

#define local_irq_disable_head()	local_irq_disable()

#define local_irq_save_full(vflags, rflags)	do { (void)(vflags); local_irq_save(rflags); } while(0)
#define local_irq_restore_full(vflags, rflags)	do { (void)(vflags); local_irq_restore(rflags); } while(0)
#define local_irq_restore_nosync(vflags)	local_irq_restore(vflags)

#define __ipipe_pipeline_head_p(ipd)	1

#endif	/* CONFIG_IPIPE */

#endif	/* !__LINUX_IPIPE_H */
