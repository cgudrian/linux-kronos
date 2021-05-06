/* -*- linux-c -*-
 * linux/kernel/ipipe/core.c
 *
 * Copyright (C) 2002-2005 Philippe Gerum.
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
 *
 * Architecture-independent I-PIPE core support.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/tick.h>
#include <linux/prefetch.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif	/* CONFIG_PROC_FS */
#include <linux/ipipe_trace.h>
#include <linux/ipipe_tickdev.h>
#include <linux/irq.h>

static int __ipipe_ptd_key_count;

static unsigned long __ipipe_ptd_key_map;

static unsigned long __ipipe_domain_slot_map;

struct ipipe_domain ipipe_root;

#ifndef CONFIG_SMP
/*
 * Create an alias to the unique root status, so that arch-dep code
 * may get simple and easy access to this percpu variable.  We also
 * create an array of pointers to the percpu domain data; this tends
 * to produce a better code when reaching non-root domains. We make
 * sure that the early boot code would be able to dereference the
 * pointer to the root domain data safely by statically initializing
 * its value (local_irq*() routines depend on this).
 */
#if __GNUC__ >= 4
extern unsigned long __ipipe_root_status
__attribute__((alias(__stringify(__raw_get_cpu_var(ipipe_percpu_darray)))));
EXPORT_SYMBOL(__ipipe_root_status);
#else /* __GNUC__ < 4 */
/*
 * Work around a GCC 3.x issue making alias symbols unusable as
 * constant initializers.
 */
unsigned long *const __ipipe_root_status_addr =
	&__raw_get_cpu_var(ipipe_percpu_darray)[IPIPE_ROOT_SLOT].status;
EXPORT_SYMBOL(__ipipe_root_status_addr);
#endif /* __GNUC__ < 4 */

DEFINE_PER_CPU(struct ipipe_percpu_domain_data *, ipipe_percpu_daddr[CONFIG_IPIPE_DOMAINS]) =
{ [IPIPE_ROOT_SLOT] = (struct ipipe_percpu_domain_data *)&__raw_get_cpu_var(ipipe_percpu_darray) };
EXPORT_PER_CPU_SYMBOL(ipipe_percpu_daddr);
#endif /* !CONFIG_SMP */

DEFINE_PER_CPU(struct ipipe_percpu_domain_data, ipipe_percpu_darray[CONFIG_IPIPE_DOMAINS]) =
{ [IPIPE_ROOT_SLOT] = { .status = IPIPE_STALL_MASK } }; /* Root domain stalled on each CPU at startup. */

DEFINE_PER_CPU(struct ipipe_domain *, ipipe_percpu_domain) = { &ipipe_root };

DEFINE_PER_CPU(unsigned long, ipipe_nmi_saved_root); /* Copy of root status during NMI */

static IPIPE_DEFINE_SPINLOCK(__ipipe_pipelock);

LIST_HEAD(__ipipe_pipeline);

unsigned long __ipipe_virtual_irq_map;

#ifdef CONFIG_PRINTK
unsigned __ipipe_printk_virq;
#endif /* CONFIG_PRINTK */

int __ipipe_event_monitors[IPIPE_NR_EVENTS];

#ifdef CONFIG_GENERIC_CLOCKEVENTS

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);

static DEFINE_PER_CPU(struct ipipe_tick_device, ipipe_tick_cpu_device);

int ipipe_request_tickdev(const char *devname,
			  void (*emumode)(enum clock_event_mode mode,
					  struct clock_event_device *cdev),
			  int (*emutick)(unsigned long delta,
					 struct clock_event_device *cdev),
			  int cpu, unsigned long *tmfreq)
{
	struct ipipe_tick_device *itd;
	struct tick_device *slave;
	struct clock_event_device *evtdev;
	unsigned long long freq;
	unsigned long flags;
	int status;

	flags = ipipe_critical_enter(NULL);

	itd = &per_cpu(ipipe_tick_cpu_device, cpu);

	if (itd->slave != NULL) {
		status = -EBUSY;
		goto out;
	}

	slave = &per_cpu(tick_cpu_device, cpu);

	if (strcmp(slave->evtdev->name, devname)) {
		/*
		 * No conflict so far with the current tick device,
		 * check whether the requested device is sane and has
		 * been blessed by the kernel.
		 */
		status = __ipipe_check_tickdev(devname) ?
			CLOCK_EVT_MODE_UNUSED : CLOCK_EVT_MODE_SHUTDOWN;
		goto out;
	}

	/*
	 * Our caller asks for using the same clock event device for
	 * ticking than we do, let's create a tick emulation device to
	 * interpose on the set_next_event() method, so that we may
	 * both manage the device in oneshot mode. Only the tick
	 * emulation code will actually program the clockchip hardware
	 * for the next shot, though.
	 *
	 * CAUTION: we still have to grab the tick device even when it
	 * current runs in periodic mode, since the kernel may switch
	 * to oneshot dynamically (highres/no_hz tick mode).
	 */

	evtdev = slave->evtdev;
	status = evtdev->mode;

        if (status == CLOCK_EVT_MODE_SHUTDOWN)
                goto out;

	itd->slave = slave;
	itd->emul_set_mode = emumode;
	itd->emul_set_tick = emutick;
	itd->real_set_mode = evtdev->set_mode;
	itd->real_set_tick = evtdev->set_next_event;
	itd->real_max_delta_ns = evtdev->max_delta_ns;
	itd->real_mult = evtdev->mult;
	itd->real_shift = evtdev->shift;
	freq = (1000000000ULL * evtdev->mult) >> evtdev->shift;
	*tmfreq = (unsigned long)freq;
	evtdev->set_mode = emumode;
	evtdev->set_next_event = emutick;
	evtdev->max_delta_ns = ULONG_MAX;
	evtdev->mult = 1;
	evtdev->shift = 0;
out:
	ipipe_critical_exit(flags);

	return status;
}

void ipipe_release_tickdev(int cpu)
{
	struct ipipe_tick_device *itd;
	struct tick_device *slave;
	struct clock_event_device *evtdev;
	unsigned long flags;

	flags = ipipe_critical_enter(NULL);

	itd = &per_cpu(ipipe_tick_cpu_device, cpu);

	if (itd->slave != NULL) {
		slave = &per_cpu(tick_cpu_device, cpu);
		evtdev = slave->evtdev;
		evtdev->set_mode = itd->real_set_mode;
		evtdev->set_next_event = itd->real_set_tick;
		evtdev->max_delta_ns = itd->real_max_delta_ns;
		evtdev->mult = itd->real_mult;
		evtdev->shift = itd->real_shift;
		itd->slave = NULL;
	}

	ipipe_critical_exit(flags);
}

#endif /* CONFIG_GENERIC_CLOCKEVENTS */

void __init ipipe_init_early(void)
{
	struct ipipe_domain *ipd = &ipipe_root;

	/*
	 * Do the early init stuff. At this point, the kernel does not
	 * provide much services yet: be careful.
	 */
	__ipipe_check_platform(); /* Do platform dependent checks first. */

	/*
	 * A lightweight registration code for the root domain. We are
	 * running on the boot CPU, hw interrupts are off, and
	 * secondary CPUs are still lost in space.
	 */

	/* Reserve percpu data slot #0 for the root domain. */
	ipd->slot = 0;
	set_bit(0, &__ipipe_domain_slot_map);

	ipd->name = "Linux";
	ipd->domid = IPIPE_ROOT_ID;
	ipd->priority = IPIPE_ROOT_PRIO;

	__ipipe_init_stage(ipd);

	list_add_tail(&ipd->p_link, &__ipipe_pipeline);

	__ipipe_init_platform();

#ifdef CONFIG_PRINTK
	__ipipe_printk_virq = ipipe_alloc_virq();	/* Cannot fail here. */
	ipd->irqs[__ipipe_printk_virq].handler = &__ipipe_flush_printk;
	ipd->irqs[__ipipe_printk_virq].cookie = NULL;
	ipd->irqs[__ipipe_printk_virq].acknowledge = NULL;
	ipd->irqs[__ipipe_printk_virq].control = IPIPE_HANDLE_MASK;
#endif /* CONFIG_PRINTK */
}

void __init ipipe_init(void)
{
	/* Now we may engage the pipeline. */
	__ipipe_enable_pipeline();

	printk(KERN_INFO "I-pipe %s: pipeline enabled.\n",
	       IPIPE_VERSION_STRING);
}

void __ipipe_init_stage(struct ipipe_domain *ipd)
{
	struct ipipe_percpu_domain_data *p;
	unsigned long status;
	int cpu, n;

	for_each_online_cpu(cpu) {
		p = ipipe_percpudom_ptr(ipd, cpu);
		status = p->status;
		memset(p, 0, sizeof(*p));
		p->status = status;
	}

	for (n = 0; n < IPIPE_NR_IRQS; n++) {
		ipd->irqs[n].acknowledge = NULL;
		ipd->irqs[n].handler = NULL;
		ipd->irqs[n].control = IPIPE_PASS_MASK;	/* Pass but don't handle */
	}

	for (n = 0; n < IPIPE_NR_EVENTS; n++)
		ipd->evhand[n] = NULL;

	ipd->evself = 0LL;
	mutex_init(&ipd->mutex);

	__ipipe_hook_critical_ipi(ipd);
}

void __ipipe_cleanup_domain(struct ipipe_domain *ipd)
{
	ipipe_unstall_pipeline_from(ipd);

#ifdef CONFIG_SMP
	{
		struct ipipe_percpu_domain_data *p;
		int cpu;

		for_each_online_cpu(cpu) {
			p = ipipe_percpudom_ptr(ipd, cpu);
			while (__ipipe_ipending_p(p))
				cpu_relax();
		}
	}
#else
	__raw_get_cpu_var(ipipe_percpu_daddr)[ipd->slot] = NULL;
#endif

	clear_bit(ipd->slot, &__ipipe_domain_slot_map);
}

void __ipipe_unstall_root(void)
{
	struct ipipe_percpu_domain_data *p;

        local_irq_disable_hw();

#ifdef CONFIG_IPIPE_DEBUG_INTERNAL
	/* This helps catching bad usage from assembly call sites. */
	BUG_ON(!__ipipe_root_domain_p);
#endif

	p = ipipe_root_cpudom_ptr();

        __clear_bit(IPIPE_STALL_FLAG, &p->status);

        if (unlikely(__ipipe_ipending_p(p)))
                __ipipe_sync_pipeline(IPIPE_IRQ_DOALL);

        local_irq_enable_hw();
}

void __ipipe_restore_root(unsigned long x)
{
#ifdef CONFIG_IPIPE_DEBUG_INTERNAL
	BUG_ON(!ipipe_root_domain_p);
#endif

	if (x)
		__ipipe_stall_root();
	else
		__ipipe_unstall_root();
}

void ipipe_stall_pipeline_from(struct ipipe_domain *ipd)
{
	unsigned long flags;
	/*
	 * We have to prevent against race on updating the status
	 * variable _and_ CPU migration at the same time, so disable
	 * hw IRQs here.
	 */
	local_irq_save_hw(flags);

	__set_bit(IPIPE_STALL_FLAG, &ipipe_cpudom_var(ipd, status));

	if (!__ipipe_pipeline_head_p(ipd))
		local_irq_restore_hw(flags);
}

unsigned long ipipe_test_and_stall_pipeline_from(struct ipipe_domain *ipd)
{
	unsigned long flags, x;

	/* See ipipe_stall_pipeline_from() */
	local_irq_save_hw(flags);

	x = __test_and_set_bit(IPIPE_STALL_FLAG, &ipipe_cpudom_var(ipd, status));

	if (!__ipipe_pipeline_head_p(ipd))
		local_irq_restore_hw(flags);

	return x;
}

unsigned long ipipe_test_and_unstall_pipeline_from(struct ipipe_domain *ipd)
{
	unsigned long flags, x;
	struct list_head *pos;

	local_irq_save_hw(flags);

	x = __test_and_clear_bit(IPIPE_STALL_FLAG, &ipipe_cpudom_var(ipd, status));

	if (ipd == __ipipe_current_domain)
		pos = &ipd->p_link;
	else
		pos = __ipipe_pipeline.next;

	__ipipe_walk_pipeline(pos);

	if (likely(__ipipe_pipeline_head_p(ipd)))
		local_irq_enable_hw();
	else
		local_irq_restore_hw(flags);

	return x;
}

void ipipe_restore_pipeline_from(struct ipipe_domain *ipd,
					  unsigned long x)
{
	if (x)
		ipipe_stall_pipeline_from(ipd);
	else
		ipipe_unstall_pipeline_from(ipd);
}

void ipipe_unstall_pipeline_head(void)
{
	struct ipipe_percpu_domain_data *p = ipipe_head_cpudom_ptr();
	struct ipipe_domain *head_domain;

	local_irq_disable_hw();

	__clear_bit(IPIPE_STALL_FLAG, &p->status);

	if (unlikely(__ipipe_ipending_p(p))) {
		head_domain = __ipipe_pipeline_head();
		if (likely(head_domain == __ipipe_current_domain))
			__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);
		else
			__ipipe_walk_pipeline(&head_domain->p_link);
        }

	local_irq_enable_hw();
}

void __ipipe_restore_pipeline_head(unsigned long x)
{
	struct ipipe_percpu_domain_data *p = ipipe_head_cpudom_ptr();
	struct ipipe_domain *head_domain;

	local_irq_disable_hw();

	if (x) {
#ifdef CONFIG_DEBUG_KERNEL
		static int warned;
		if (!warned && test_and_set_bit(IPIPE_STALL_FLAG, &p->status)) {
			/*
			 * Already stalled albeit ipipe_restore_pipeline_head()
			 * should have detected it? Send a warning once.
			 */
			warned = 1;
			printk(KERN_WARNING
				   "I-pipe: ipipe_restore_pipeline_head() optimization failed.\n");
			dump_stack();
		}
#else /* !CONFIG_DEBUG_KERNEL */
		set_bit(IPIPE_STALL_FLAG, &p->status);
#endif /* CONFIG_DEBUG_KERNEL */
	}
	else {
		__clear_bit(IPIPE_STALL_FLAG, &p->status);
		if (unlikely(__ipipe_ipending_p(p))) {
			head_domain = __ipipe_pipeline_head();
			if (likely(head_domain == __ipipe_current_domain))
				__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);
			else
				__ipipe_walk_pipeline(&head_domain->p_link);
		}
		local_irq_enable_hw();
	}
}

void __ipipe_spin_lock_irq(raw_spinlock_t *lock)
{
	local_irq_disable_hw();
	__raw_spin_lock(lock);
	__set_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
}

void __ipipe_spin_unlock_irq(raw_spinlock_t *lock)
{
	__raw_spin_unlock(lock);
	__clear_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
	local_irq_enable_hw();
}

unsigned long __ipipe_spin_lock_irqsave(raw_spinlock_t *lock)
{
	unsigned long flags;
	int s;

	local_irq_save_hw(flags);
	__raw_spin_lock(lock);
	s = __test_and_set_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));

	return raw_mangle_irq_bits(s, flags);
}

void __ipipe_spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long x)
{
	__raw_spin_unlock(lock);
	if (!raw_demangle_irq_bits(&x))
		__clear_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
	local_irq_restore_hw(x);
}

void __ipipe_spin_unlock_irqbegin(ipipe_spinlock_t *lock)
{
	__raw_spin_unlock(&lock->bare_lock);
}

void __ipipe_spin_unlock_irqcomplete(unsigned long x)
{
	if (!raw_demangle_irq_bits(&x))
		__clear_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
	local_irq_restore_hw(x);
}

#ifdef __IPIPE_3LEVEL_IRQMAP

/* Must be called hw IRQs off. */
static inline void __ipipe_set_irq_held(struct ipipe_percpu_domain_data *p,
					unsigned int irq)
{
	__set_bit(irq, p->irqheld_map);
	p->irqall[irq]++;
}

/* Must be called hw IRQs off. */
void __ipipe_set_irq_pending(struct ipipe_domain *ipd, unsigned int irq)
{
	struct ipipe_percpu_domain_data *p = ipipe_cpudom_ptr(ipd);
	int l0b, l1b;

	l0b = irq / (BITS_PER_LONG * BITS_PER_LONG);
	l1b = irq / BITS_PER_LONG;
	prefetchw(p);

	if (likely(!test_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))) {
		__set_bit(irq, p->irqpend_lomap);
		__set_bit(l1b, p->irqpend_mdmap);
		__set_bit(l0b, &p->irqpend_himap);
	} else
		__set_bit(irq, p->irqheld_map);

	p->irqall[irq]++;
}

/* Must be called hw IRQs off. */
void __ipipe_lock_irq(struct ipipe_domain *ipd, int cpu, unsigned int irq)
{
	struct ipipe_percpu_domain_data *p;
	int l0b, l1b;

	if (unlikely(test_and_set_bit(IPIPE_LOCK_FLAG,
				      &ipd->irqs[irq].control)))
		return;

	l0b = irq / (BITS_PER_LONG * BITS_PER_LONG);
	l1b = irq / BITS_PER_LONG;

	p = ipipe_percpudom_ptr(ipd, cpu);
	if (__test_and_clear_bit(irq, p->irqpend_lomap)) {
		__set_bit(irq, p->irqheld_map);
		if (p->irqpend_lomap[l1b] == 0) {
			__clear_bit(l1b, p->irqpend_mdmap);
			if (p->irqpend_mdmap[l0b] == 0)
				__clear_bit(l0b, &p->irqpend_himap);
		}
	}
}

/* Must be called hw IRQs off. */
void __ipipe_unlock_irq(struct ipipe_domain *ipd, unsigned int irq)
{
	struct ipipe_percpu_domain_data *p;
	int l0b, l1b, cpu;

	if (unlikely(!test_and_clear_bit(IPIPE_LOCK_FLAG,
					 &ipd->irqs[irq].control)))
		return;

	l0b = irq / (BITS_PER_LONG * BITS_PER_LONG);
	l1b = irq / BITS_PER_LONG;

	for_each_online_cpu(cpu) {
		p = ipipe_percpudom_ptr(ipd, cpu);
		if (test_and_clear_bit(irq, p->irqheld_map)) {
			/* We need atomic ops here: */
			set_bit(irq, p->irqpend_lomap);
			set_bit(l1b, p->irqpend_mdmap);
			set_bit(l0b, &p->irqpend_himap);
		}
	}
}

static inline int __ipipe_next_irq(struct ipipe_percpu_domain_data *p,
				   int dovirt)
{
	unsigned long l0m, l1m, l2m, himask, mdmask;
	int l0b, l1b, l2b, vl0b, vl1b;
	unsigned int irq;

	if (dovirt) {
		/*
		 * All virtual IRQs are mapped by a single long word.
		 * There is exactly BITS_PER_LONG virqs, and they are
		 * always last in the interrupt map, starting at
		 * IPIPE_VIRQ_BASE. Therefore, we only need to test a
		 * single bit within the high and middle maps to check
		 * whether a virtual IRQ is pending (the computations
		 * below are constant).
		 */
		vl0b = IPIPE_VIRQ_BASE / (BITS_PER_LONG * BITS_PER_LONG);
		himask = (1L << vl0b);
		vl1b = IPIPE_VIRQ_BASE / BITS_PER_LONG;
		mdmask = (1L << (vl1b & (BITS_PER_LONG-1)));
	} else
		himask = mdmask = ~0L;

	l0m = p->irqpend_himap & himask;
	if (unlikely(l0m == 0))
		return -1;

	l0b = __ipipe_ffnz(l0m);
	l1m = p->irqpend_mdmap[l0b] & mdmask;
	if (unlikely(l1m == 0))
		return -1;

	l1b = __ipipe_ffnz(l1m) + l0b * BITS_PER_LONG;
	l2m = p->irqpend_lomap[l1b];
	if (unlikely(l2m == 0))
		return -1;

	l2b = __ipipe_ffnz(l2m);
	irq = l1b * BITS_PER_LONG + l2b;

	__clear_bit(irq, p->irqpend_lomap);
	if (p->irqpend_lomap[l1b] == 0) {
		__clear_bit(l1b, p->irqpend_mdmap);
		if (p->irqpend_mdmap[l0b] == 0)
			__clear_bit(l0b, &p->irqpend_himap);
	}

	return irq;
}

#else /* __IPIPE_2LEVEL_IRQMAP */

/* Must be called hw IRQs off. */
static inline void __ipipe_set_irq_held(struct ipipe_percpu_domain_data *p,
					unsigned int irq)
{
	__set_bit(irq, p->irqheld_map);
	p->irqall[irq]++;
}

/* Must be called hw IRQs off. */
void __ipipe_set_irq_pending(struct ipipe_domain *ipd, unsigned irq)
{
	struct ipipe_percpu_domain_data *p = ipipe_cpudom_ptr(ipd);
	int l0b = irq / BITS_PER_LONG;

	prefetchw(p);
	
	if (likely(!test_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))) {
		__set_bit(irq, p->irqpend_lomap);
		__set_bit(l0b, &p->irqpend_himap);
	} else
		__set_bit(irq, p->irqheld_map);

	p->irqall[irq]++;
}

/* Must be called hw IRQs off. */
void __ipipe_lock_irq(struct ipipe_domain *ipd, int cpu, unsigned irq)
{
	struct ipipe_percpu_domain_data *p;
	int l0b = irq / BITS_PER_LONG;

	if (unlikely(test_and_set_bit(IPIPE_LOCK_FLAG,
				      &ipd->irqs[irq].control)))
		return;

	p = ipipe_percpudom_ptr(ipd, cpu);
	if (__test_and_clear_bit(irq, p->irqpend_lomap)) {
		__set_bit(irq, p->irqheld_map);
		if (p->irqpend_lomap[l0b] == 0)
			__clear_bit(l0b, &p->irqpend_himap);
	}
}

/* Must be called hw IRQs off. */
void __ipipe_unlock_irq(struct ipipe_domain *ipd, unsigned irq)
{
	struct ipipe_percpu_domain_data *p;
	int l0b = irq / BITS_PER_LONG, cpu;

	if (unlikely(!test_and_clear_bit(IPIPE_LOCK_FLAG,
					 &ipd->irqs[irq].control)))
		return;

	for_each_online_cpu(cpu) {
		p = ipipe_percpudom_ptr(ipd, cpu);
		if (test_and_clear_bit(irq, p->irqheld_map)) {
			/* We need atomic ops here: */
			set_bit(irq, p->irqpend_lomap);
			set_bit(l0b, &p->irqpend_himap);
		}
	}
}

static inline int __ipipe_next_irq(struct ipipe_percpu_domain_data *p,
				   int dovirt)
{
	unsigned long l0m, l1m, himask = ~0L;
	int l0b, l1b;

	himask <<= dovirt ? IPIPE_VIRQ_BASE/BITS_PER_LONG : 0;

	l0m = p->irqpend_himap & himask;
	if (unlikely(l0m == 0))
		return -1;

	l0b = __ipipe_ffnz(l0m);
	l1m = p->irqpend_lomap[l0b];
	if (unlikely(l1m == 0))
		return -1;

	l1b = __ipipe_ffnz(l1m);
	__clear_bit(l1b, &p->irqpend_lomap[l0b]);
	if (p->irqpend_lomap[l0b] == 0)
		__clear_bit(l0b, &p->irqpend_himap);

	return l0b * BITS_PER_LONG + l1b;
}

#endif /* __IPIPE_2LEVEL_IRQMAP */

/*
 * __ipipe_walk_pipeline(): Plays interrupts pending in the log. Must
 * be called with local hw interrupts disabled.
 */
void __ipipe_walk_pipeline(struct list_head *pos)
{
	struct ipipe_domain *this_domain = __ipipe_current_domain, *next_domain;
	struct ipipe_percpu_domain_data *p, *np;

	p = ipipe_cpudom_ptr(this_domain);

	while (pos != &__ipipe_pipeline) {

		next_domain = list_entry(pos, struct ipipe_domain, p_link);
		np = ipipe_cpudom_ptr(next_domain);

		if (test_bit(IPIPE_STALL_FLAG, &np->status))
			break;	/* Stalled stage -- do not go further. */

		if (__ipipe_ipending_p(np)) {
			if (next_domain == this_domain)
				__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);
			else {

				p->evsync = 0;
				__ipipe_current_domain = next_domain;
				ipipe_suspend_domain();	/* Sync stage and propagate interrupts. */

				if (__ipipe_current_domain == next_domain)
					__ipipe_current_domain = this_domain;
				/*
				 * Otherwise, something changed the current domain under our
				 * feet recycling the register set; do not override the new
				 * domain.
				 */

				if (__ipipe_ipending_p(p) &&
				    !test_bit(IPIPE_STALL_FLAG, &p->status))
					__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);
			}
			break;
		} else if (next_domain == this_domain)
			break;

		pos = next_domain->p_link.next;
	}
}

/*
 * ipipe_suspend_domain() -- Suspend the current domain, switching to
 * the next one which has pending work down the pipeline.
 */
void ipipe_suspend_domain(void)
{
	struct ipipe_domain *this_domain, *next_domain;
	struct ipipe_percpu_domain_data *p;
	struct list_head *ln;
	unsigned long flags;

	local_irq_save_hw(flags);

	this_domain = next_domain = __ipipe_current_domain;
	p = ipipe_cpudom_ptr(this_domain);
	p->status &= ~(IPIPE_STALL_MASK|IPIPE_SYNC_MASK);

	if (__ipipe_ipending_p(p))
		goto sync_stage;

	for (;;) {
		ln = next_domain->p_link.next;

		if (ln == &__ipipe_pipeline)
			break;

		next_domain = list_entry(ln, struct ipipe_domain, p_link);
		p = ipipe_cpudom_ptr(next_domain);

		if (p->status & IPIPE_STALL_MASK)
			break;

		if (!__ipipe_ipending_p(p))
			continue;

		__ipipe_current_domain = next_domain;
sync_stage:
		__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);

		if (__ipipe_current_domain != next_domain)
			/*
			 * Something has changed the current domain under our
			 * feet, recycling the register set; take note.
			 */
			this_domain = __ipipe_current_domain;
	}

	__ipipe_current_domain = this_domain;

	local_irq_restore_hw(flags);
}


/* ipipe_alloc_virq() -- Allocate a pipelined virtual/soft interrupt.
 * Virtual interrupts are handled in exactly the same way than their
 * hw-generated counterparts wrt pipelining.
 */
unsigned ipipe_alloc_virq(void)
{
	unsigned long flags, irq = 0;
	int ipos;

	spin_lock_irqsave(&__ipipe_pipelock, flags);

	if (__ipipe_virtual_irq_map != ~0) {
		ipos = ffz(__ipipe_virtual_irq_map);
		set_bit(ipos, &__ipipe_virtual_irq_map);
		irq = ipos + IPIPE_VIRQ_BASE;
	}

	spin_unlock_irqrestore(&__ipipe_pipelock, flags);

	return irq;
}

/*
 * ipipe_control_irq() -- Change modes of a pipelined interrupt for
 * the current domain.
 */
int ipipe_virtualize_irq(struct ipipe_domain *ipd,
			 unsigned irq,
			 ipipe_irq_handler_t handler,
			 void *cookie,
			 ipipe_irq_ackfn_t acknowledge,
			 unsigned modemask)
{
	ipipe_irq_handler_t old_handler;
	struct irq_desc *desc;
	unsigned long flags;
	int err;

	if (irq >= IPIPE_NR_IRQS)
		return -EINVAL;

	if (ipd->irqs[irq].control & IPIPE_SYSTEM_MASK)
		return -EPERM;

	if (!test_bit(IPIPE_AHEAD_FLAG, &ipd->flags))
		/* Silently unwire interrupts for non-heading domains. */
		modemask &= ~IPIPE_WIRED_MASK;

	spin_lock_irqsave(&__ipipe_pipelock, flags);

	old_handler = ipd->irqs[irq].handler;

	if (handler != NULL) {
		if (handler == IPIPE_SAME_HANDLER) {
			handler = old_handler;
			cookie = ipd->irqs[irq].cookie;

			if (handler == NULL) {
				err = -EINVAL;
				goto unlock_and_exit;
			}
		} else if ((modemask & IPIPE_EXCLUSIVE_MASK) != 0 &&
			   old_handler != NULL) {
			err = -EBUSY;
			goto unlock_and_exit;
		}

		/* Wired interrupts can only be delivered to domains
		 * always heading the pipeline, and using dynamic
		 * propagation. */

		if ((modemask & IPIPE_WIRED_MASK) != 0) {
			if ((modemask & (IPIPE_PASS_MASK | IPIPE_STICKY_MASK)) != 0) {
				err = -EINVAL;
				goto unlock_and_exit;
			}
			modemask |= (IPIPE_HANDLE_MASK);
		}

		if ((modemask & IPIPE_STICKY_MASK) != 0)
			modemask |= IPIPE_HANDLE_MASK;
	} else
		modemask &=
		    ~(IPIPE_HANDLE_MASK | IPIPE_STICKY_MASK |
		      IPIPE_EXCLUSIVE_MASK | IPIPE_WIRED_MASK);

	if (acknowledge == NULL && !ipipe_virtual_irq_p(irq))
		/*
		 * Acknowledge handler unspecified for a hw interrupt:
		 * use the Linux-defined handler instead.
		 */
		acknowledge = ipipe_root_domain->irqs[irq].acknowledge;

	ipd->irqs[irq].handler = handler;
	ipd->irqs[irq].cookie = cookie;
	ipd->irqs[irq].acknowledge = acknowledge;
	ipd->irqs[irq].control = modemask;

	if (irq < NR_IRQS && !ipipe_virtual_irq_p(irq)) {
		desc = irq_to_desc(irq);
		if (handler != NULL) {
			if (desc)
				__ipipe_enable_irqdesc(ipd, irq);

			if ((modemask & IPIPE_ENABLE_MASK) != 0) {
				if (ipd != __ipipe_current_domain) {
		/*
		 * IRQ enable/disable state is domain-sensitive, so we
		 * may not change it for another domain. What is
		 * allowed however is forcing some domain to handle an
		 * interrupt source, by passing the proper 'ipd'
		 * descriptor which thus may be different from
		 * __ipipe_current_domain.
		 */
					err = -EPERM;
					goto unlock_and_exit;
				}
				if (desc)
					__ipipe_enable_irq(irq);
			}
		} else if (old_handler != NULL && desc)
				__ipipe_disable_irqdesc(ipd, irq);
	}

	err = 0;

      unlock_and_exit:

	spin_unlock_irqrestore(&__ipipe_pipelock, flags);

	return err;
}

/* ipipe_control_irq() -- Change modes of a pipelined interrupt for
 * the current domain. */

int ipipe_control_irq(unsigned irq, unsigned clrmask, unsigned setmask)
{
	struct ipipe_domain *ipd;
	unsigned long flags;

	if (irq >= IPIPE_NR_IRQS)
		return -EINVAL;

	spin_lock_irqsave(&__ipipe_pipelock, flags);

	ipd = __ipipe_current_domain;

	if (ipd->irqs[irq].control & IPIPE_SYSTEM_MASK) {
		spin_unlock_irqrestore(&__ipipe_pipelock, flags);
		return -EPERM;
	}

	if (ipd->irqs[irq].handler == NULL)
		setmask &= ~(IPIPE_HANDLE_MASK | IPIPE_STICKY_MASK);

	if ((setmask & IPIPE_STICKY_MASK) != 0)
		setmask |= IPIPE_HANDLE_MASK;

	if ((clrmask & (IPIPE_HANDLE_MASK | IPIPE_STICKY_MASK)) != 0)	/* If one goes, both go. */
		clrmask |= (IPIPE_HANDLE_MASK | IPIPE_STICKY_MASK);

	ipd->irqs[irq].control &= ~clrmask;
	ipd->irqs[irq].control |= setmask;

	if ((setmask & IPIPE_ENABLE_MASK) != 0)
		__ipipe_enable_irq(irq);
	else if ((clrmask & IPIPE_ENABLE_MASK) != 0)
		__ipipe_disable_irq(irq);

	spin_unlock_irqrestore(&__ipipe_pipelock, flags);

	return 0;
}

/* __ipipe_dispatch_event() -- Low-level event dispatcher. */

int __ipipe_dispatch_event (unsigned event, void *data)
{
extern void *ipipe_irq_handler; void *handler; if (ipipe_irq_handler != __ipipe_handle_irq && (handler = ipipe_root_domain->evhand[event])) { return ((int (*)(unsigned long, void *))handler)(event, data); } else {
	struct ipipe_domain *start_domain, *this_domain, *next_domain;
	struct ipipe_percpu_domain_data *np;
	ipipe_event_handler_t evhand;
	struct list_head *pos, *npos;
	unsigned long flags;
	int propagate = 1;

	local_irq_save_hw(flags);

	start_domain = this_domain = __ipipe_current_domain;

	list_for_each_safe(pos, npos, &__ipipe_pipeline) {
		/*
		 * Note: Domain migration may occur while running
		 * event or interrupt handlers, in which case the
		 * current register set is going to be recycled for a
		 * different domain than the initiating one. We do
		 * care for that, always tracking the current domain
		 * descriptor upon return from those handlers.
		 */
		next_domain = list_entry(pos, struct ipipe_domain, p_link);
		np = ipipe_cpudom_ptr(next_domain);

		/*
		 * Keep a cached copy of the handler's address since
		 * ipipe_catch_event() may clear it under our feet.
		 */
		evhand = next_domain->evhand[event];

		if (evhand != NULL) {
			__ipipe_current_domain = next_domain;
			np->evsync |= (1LL << event);
			local_irq_restore_hw(flags);
			propagate = !evhand(event, start_domain, data);
			local_irq_save_hw(flags);
			/*
			 * We may have a migration issue here, if the
			 * current task is migrated to another CPU on
			 * behalf of the invoked handler, usually when
			 * a syscall event is processed. However,
			 * ipipe_catch_event() will make sure that a
			 * CPU that clears a handler for any given
			 * event will not attempt to wait for itself
			 * to clear the evsync bit for that event,
			 * which practically plugs the hole, without
			 * resorting to a much more complex strategy.
			 */
			np->evsync &= ~(1LL << event);
			if (__ipipe_current_domain != next_domain)
				this_domain = __ipipe_current_domain;
		}

		/* NEVER sync the root stage here. */
		if (next_domain != ipipe_root_domain &&
		    __ipipe_ipending_p(np) &&
		    !test_bit(IPIPE_STALL_FLAG, &np->status)) {
			__ipipe_current_domain = next_domain;
			__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);
			if (__ipipe_current_domain != next_domain)
				this_domain = __ipipe_current_domain;
		}

		__ipipe_current_domain = this_domain;

		if (next_domain == this_domain || !propagate)
			break;
	}

	local_irq_restore_hw(flags);

	return !propagate;
} }

/*
 * __ipipe_dispatch_wired -- Wired interrupt dispatcher. Wired
 * interrupts are immediately and unconditionally delivered to the
 * domain heading the pipeline upon receipt, and such domain must have
 * been registered as an invariant head for the system (priority ==
 * IPIPE_HEAD_PRIORITY). The motivation for using wired interrupts is
 * to get an extra-fast dispatching path for those IRQs, by relying on
 * a straightforward logic based on assumptions that must always be
 * true for invariant head domains.  The following assumptions are
 * made when dealing with such interrupts:
 *
 * 1- Wired interrupts are purely dynamic, i.e. the decision to
 * propagate them down the pipeline must be done from the head domain
 * ISR.
 * 2- Wired interrupts cannot be shared or sticky.
 * 3- The root domain cannot be an invariant pipeline head, in
 * consequence of what the root domain cannot handle wired
 * interrupts.
 * 4- Wired interrupts must have a valid acknowledge handler for the
 * head domain (if needed, see __ipipe_handle_irq).
 *
 * Called with hw interrupts off.
 */

void __ipipe_dispatch_wired(struct ipipe_domain *head, unsigned irq)
{
	struct ipipe_percpu_domain_data *p = ipipe_cpudom_ptr(head);

	prefetchw(p);

	if (unlikely(test_bit(IPIPE_LOCK_FLAG, &head->irqs[irq].control))) {
		/*
		 * If we can't process this IRQ right now, we must
		 * mark it as held, so that it will get played during
		 * normal log sync when the corresponding interrupt
		 * source is eventually unlocked.
		 */
		__ipipe_set_irq_held(p, irq);
		return;
	}

	if (test_bit(IPIPE_STALL_FLAG, &p->status)) {
		__ipipe_set_irq_pending(head, irq);
		return;
	}

	__ipipe_dispatch_wired_nocheck(head, irq);
}

void __ipipe_dispatch_wired_nocheck(struct ipipe_domain *head, unsigned irq) /* hw interrupts off */
{
	struct ipipe_percpu_domain_data *p = ipipe_cpudom_ptr(head);
	struct ipipe_domain *old;

	prefetchw(p);

	old = __ipipe_current_domain;
	__ipipe_current_domain = head; /* Switch to the head domain. */

	p->irqall[irq]++;
	__set_bit(IPIPE_STALL_FLAG, &p->status);
	head->irqs[irq].handler(irq, head->irqs[irq].cookie); /* Call the ISR. */
	__ipipe_run_irqtail();
	__clear_bit(IPIPE_STALL_FLAG, &p->status);

	if (__ipipe_current_domain == head) {
		__ipipe_current_domain = old;
		if (old == head) {
			if (__ipipe_ipending_p(p))
				__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);
			return;
		}
	}

	__ipipe_walk_pipeline(&head->p_link);
}

/*
 * __ipipe_sync_stage() -- Flush the pending IRQs for the current
 * domain (and processor). This routine flushes the interrupt log
 * (see "Optimistic interrupt protection" from D. Stodolsky et al. for
 * more on the deferred interrupt scheme). Every interrupt that
 * occurred while the pipeline was stalled gets played. WARNING:
 * callers on SMP boxen should always check for CPU migration on
 * return of this routine.
 *
 * This routine must be called with hw interrupts off.
 */
void __ipipe_sync_stage(int dovirt)
{
	struct ipipe_percpu_domain_data *p;
	struct ipipe_domain *ipd;
	int cpu, irq;

	ipd = __ipipe_current_domain;
	p = ipipe_cpudom_ptr(ipd);

	if (__test_and_set_bit(IPIPE_SYNC_FLAG, &p->status)) {
		/*
		 * Some questionable code in the root domain may enter
		 * busy waits for IRQs over interrupt context, so we
		 * unfortunately have to allow piling up IRQs for
		 * them. Non-root domains are not allowed to do this.
		 */
		if (ipd != ipipe_root_domain)
			return;
	}

	cpu = ipipe_processor_id();

	for (;;) {
		irq = __ipipe_next_irq(p, dovirt);
		if (irq < 0)
			break;
		/*
		 * Make sure the compiler does not reorder
		 * wrongly, so that all updates to maps are
		 * done before the handler gets called.
		 */
		barrier();

		if (test_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))
			continue;

		__set_bit(IPIPE_STALL_FLAG, &p->status);
		smp_wmb();

		if (ipd == ipipe_root_domain)
			trace_hardirqs_off();

		__ipipe_run_isr(ipd, irq);
		barrier();
		p = ipipe_cpudom_ptr(__ipipe_current_domain);
#ifdef CONFIG_SMP
		{
			int newcpu = ipipe_processor_id();

			if (newcpu != cpu) {	/* Handle CPU migration. */
				/*
				 * We expect any domain to clear the SYNC bit each
				 * time it switches in a new task, so that preemptions
				 * and/or CPU migrations (in the SMP case) over the
				 * ISR do not lock out the log syncer for some
				 * indefinite amount of time. In the Linux case,
				 * schedule() handles this (see kernel/sched.c). For
				 * this reason, we don't bother clearing it here for
				 * the source CPU in the migration handling case,
				 * since it must have scheduled another task in by
				 * now.
				 */
				__set_bit(IPIPE_SYNC_FLAG, &p->status);
				cpu = newcpu;
			}
		}
#endif	/* CONFIG_SMP */
#ifdef CONFIG_TRACE_IRQFLAGS
		if (__ipipe_root_domain_p &&
		    test_bit(IPIPE_STALL_FLAG, &p->status))
			trace_hardirqs_on();
#endif
		__clear_bit(IPIPE_STALL_FLAG, &p->status);
	}

	__clear_bit(IPIPE_SYNC_FLAG, &p->status);
}

/* ipipe_register_domain() -- Link a new domain to the pipeline. */

int ipipe_register_domain(struct ipipe_domain *ipd,
			  struct ipipe_domain_attr *attr)
{
	struct ipipe_percpu_domain_data *p;
	struct list_head *pos = NULL;
	struct ipipe_domain *_ipd;
	unsigned long flags;

	if (!ipipe_root_domain_p) {
		printk(KERN_WARNING
		       "I-pipe: Only the root domain may register a new domain.\n");
		return -EPERM;
	}

	flags = ipipe_critical_enter(NULL);

	if (attr->priority == IPIPE_HEAD_PRIORITY) {
		if (test_bit(IPIPE_HEAD_SLOT, &__ipipe_domain_slot_map)) {
			ipipe_critical_exit(flags);
			return -EAGAIN;	/* Cannot override current head. */
		}
		ipd->slot = IPIPE_HEAD_SLOT;
	} else
		ipd->slot = ffz(__ipipe_domain_slot_map);

	if (ipd->slot < CONFIG_IPIPE_DOMAINS) {
		set_bit(ipd->slot, &__ipipe_domain_slot_map);
		list_for_each(pos, &__ipipe_pipeline) {
			_ipd = list_entry(pos, struct ipipe_domain, p_link);
			if (_ipd->domid == attr->domid)
				break;
		}
	}

	ipipe_critical_exit(flags);

	if (pos != &__ipipe_pipeline) {
		if (ipd->slot < CONFIG_IPIPE_DOMAINS)
			clear_bit(ipd->slot, &__ipipe_domain_slot_map);
		return -EBUSY;
	}

#ifndef CONFIG_SMP
	/*
	 * Set up the perdomain pointers for direct access to the
	 * percpu domain data. This saves a costly multiply each time
	 * we need to refer to the contents of the percpu domain data
	 * array.
	 */
	__raw_get_cpu_var(ipipe_percpu_daddr)[ipd->slot] = &__raw_get_cpu_var(ipipe_percpu_darray)[ipd->slot];
#endif

	ipd->name = attr->name;
	ipd->domid = attr->domid;
	ipd->pdd = attr->pdd;
	ipd->flags = 0;

	if (attr->priority == IPIPE_HEAD_PRIORITY) {
		ipd->priority = INT_MAX;
		__set_bit(IPIPE_AHEAD_FLAG,&ipd->flags);
	}
	else
		ipd->priority = attr->priority;

	__ipipe_init_stage(ipd);

	INIT_LIST_HEAD(&ipd->p_link);

#ifdef CONFIG_PROC_FS
	__ipipe_add_domain_proc(ipd);
#endif /* CONFIG_PROC_FS */

	flags = ipipe_critical_enter(NULL);

	list_for_each(pos, &__ipipe_pipeline) {
		_ipd = list_entry(pos, struct ipipe_domain, p_link);
		if (ipd->priority > _ipd->priority)
			break;
	}

	list_add_tail(&ipd->p_link, pos);

	ipipe_critical_exit(flags);

	printk(KERN_INFO "I-pipe: Domain %s registered.\n", ipd->name);

	if (attr->entry == NULL)
		return 0;

	/*
	 * Finally, allow the new domain to perform its initialization
	 * duties.
	 */
	local_irq_save_hw_smp(flags);
	__ipipe_current_domain = ipd;
	local_irq_restore_hw_smp(flags);
	attr->entry();
	local_irq_save_hw(flags);
	__ipipe_current_domain = ipipe_root_domain;
	p = ipipe_root_cpudom_ptr();

	if (__ipipe_ipending_p(p) &&
	    !test_bit(IPIPE_STALL_FLAG, &p->status))
		__ipipe_sync_pipeline(IPIPE_IRQ_DOALL);

	local_irq_restore_hw(flags);

	return 0;
}

/* ipipe_unregister_domain() -- Remove a domain from the pipeline. */

int ipipe_unregister_domain(struct ipipe_domain *ipd)
{
	unsigned long flags;

	if (!ipipe_root_domain_p) {
		printk(KERN_WARNING
		       "I-pipe: Only the root domain may unregister a domain.\n");
		return -EPERM;
	}

	if (ipd == ipipe_root_domain) {
		printk(KERN_WARNING
		       "I-pipe: Cannot unregister the root domain.\n");
		return -EPERM;
	}
#ifdef CONFIG_SMP
	{
		struct ipipe_percpu_domain_data *p;
		unsigned int irq;
		int cpu;

		/*
		 * In the SMP case, wait for the logged events to drain on
		 * other processors before eventually removing the domain
		 * from the pipeline.
		 */

		ipipe_unstall_pipeline_from(ipd);

		flags = ipipe_critical_enter(NULL);

		for (irq = 0; irq < IPIPE_NR_IRQS; irq++) {
			clear_bit(IPIPE_HANDLE_FLAG, &ipd->irqs[irq].control);
			clear_bit(IPIPE_STICKY_FLAG, &ipd->irqs[irq].control);
			set_bit(IPIPE_PASS_FLAG, &ipd->irqs[irq].control);
		}

		ipipe_critical_exit(flags);

		for_each_online_cpu(cpu) {
			p = ipipe_percpudom_ptr(ipd, cpu);
			while (__ipipe_ipending_p(p))
				cpu_relax();
		}
	}
#endif	/* CONFIG_SMP */

	mutex_lock(&ipd->mutex);

#ifdef CONFIG_PROC_FS
	__ipipe_remove_domain_proc(ipd);
#endif /* CONFIG_PROC_FS */

	/*
	 * Simply remove the domain from the pipeline and we are almost done.
	 */

	flags = ipipe_critical_enter(NULL);
	list_del_init(&ipd->p_link);
	ipipe_critical_exit(flags);

	__ipipe_cleanup_domain(ipd);

	mutex_unlock(&ipd->mutex);

	printk(KERN_INFO "I-pipe: Domain %s unregistered.\n", ipd->name);

	return 0;
}

/*
 * ipipe_propagate_irq() -- Force a given IRQ propagation on behalf of
 * a running interrupt handler to the next domain down the pipeline.
 * ipipe_schedule_irq() -- Does almost the same as above, but attempts
 * to pend the interrupt for the current domain first.
 * Must be called hw IRQs off.
 */
void __ipipe_pend_irq(unsigned irq, struct list_head *head)
{
	struct ipipe_domain *ipd;
	struct list_head *ln;

#ifdef CONFIG_IPIPE_DEBUG
	BUG_ON(irq >= IPIPE_NR_IRQS ||
	       (ipipe_virtual_irq_p(irq)
		&& !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map)));
#endif
	for (ln = head; ln != &__ipipe_pipeline; ln = ipd->p_link.next) {
		ipd = list_entry(ln, struct ipipe_domain, p_link);
		if (test_bit(IPIPE_HANDLE_FLAG, &ipd->irqs[irq].control)) {
			__ipipe_set_irq_pending(ipd, irq);
			return;
		}
	}
}

/* ipipe_free_virq() -- Release a virtual/soft interrupt. */

int ipipe_free_virq(unsigned virq)
{
	if (!ipipe_virtual_irq_p(virq))
		return -EINVAL;

	clear_bit(virq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map);

	return 0;
}

void ipipe_init_attr(struct ipipe_domain_attr *attr)
{
	attr->name = "anon";
	attr->domid = 1;
	attr->entry = NULL;
	attr->priority = IPIPE_ROOT_PRIO;
	attr->pdd = NULL;
}

/*
 * ipipe_catch_event() -- Interpose or remove an event handler for a
 * given domain.
 */
ipipe_event_handler_t ipipe_catch_event(struct ipipe_domain *ipd,
					unsigned event,
					ipipe_event_handler_t handler)
{
	ipipe_event_handler_t old_handler;
	unsigned long flags;
	int self = 0, cpu;

	if (event & IPIPE_EVENT_SELF) {
		event &= ~IPIPE_EVENT_SELF;
		self = 1;
	}

	if (event >= IPIPE_NR_EVENTS)
		return NULL;

	flags = ipipe_critical_enter(NULL);

	if (!(old_handler = xchg(&ipd->evhand[event],handler)))	{
		if (handler) {
			if (self)
				ipd->evself |= (1LL << event);
			else
				__ipipe_event_monitors[event]++;
		}
	}
	else if (!handler) {
		if (ipd->evself & (1LL << event))
			ipd->evself &= ~(1LL << event);
		else
			__ipipe_event_monitors[event]--;
	} else if ((ipd->evself & (1LL << event)) && !self) {
			__ipipe_event_monitors[event]++;
			ipd->evself &= ~(1LL << event);
	} else if (!(ipd->evself & (1LL << event)) && self) {
			__ipipe_event_monitors[event]--;
			ipd->evself |= (1LL << event);
	}

	ipipe_critical_exit(flags);

	if (!handler && ipipe_root_domain_p) {
		/*
		 * If we cleared a handler on behalf of the root
		 * domain, we have to wait for any current invocation
		 * to drain, since our caller might subsequently unmap
		 * the target domain. To this aim, this code
		 * synchronizes with __ipipe_dispatch_event(),
		 * guaranteeing that either the dispatcher sees a null
		 * handler in which case it discards the invocation
		 * (which also prevents from entering a livelock), or
		 * finds a valid handler and calls it. Symmetrically,
		 * ipipe_catch_event() ensures that the called code
		 * won't be unmapped under our feet until the event
		 * synchronization flag is cleared for the given event
		 * on all CPUs.
		 */
		preempt_disable();
		cpu = smp_processor_id();
		/*
		 * Hack: this solves the potential migration issue
		 * raised in __ipipe_dispatch_event(). This is a
		 * work-around which makes the assumption that other
		 * CPUs will subsequently, either process at least one
		 * interrupt for the target domain, or call
		 * __ipipe_dispatch_event() without going through a
		 * migration while running the handler at least once;
		 * practically, this is safe on any normally running
		 * system.
		 */
		ipipe_percpudom(ipd, evsync, cpu) &= ~(1LL << event);
		preempt_enable();

		for_each_online_cpu(cpu) {
			while (ipipe_percpudom(ipd, evsync, cpu) & (1LL << event))
				schedule_timeout_interruptible(HZ / 50);
		}
	}

	return old_handler;
}

cpumask_t ipipe_set_irq_affinity (unsigned irq, cpumask_t cpumask)
{
#ifdef CONFIG_SMP
	if (irq >= NR_IRQS) // if (irq >= IPIPE_NR_XIRQS)
		/* Allow changing affinity of external IRQs only. */
		return CPU_MASK_NONE;

	if (num_online_cpus() > 1)
		return __ipipe_set_irq_affinity(irq,cpumask);
#endif /* CONFIG_SMP */

	return CPU_MASK_NONE;
}

int ipipe_send_ipi (unsigned ipi, cpumask_t cpumask)

{
#ifdef CONFIG_SMP
	return __ipipe_send_ipi(ipi,cpumask);
#else /* !CONFIG_SMP */
	return -EINVAL;
#endif /* CONFIG_SMP */
}

int ipipe_alloc_ptdkey (void)
{
	unsigned long flags;
	int key = -1;

	spin_lock_irqsave(&__ipipe_pipelock,flags);

	if (__ipipe_ptd_key_count < IPIPE_ROOT_NPTDKEYS) {
		key = ffz(__ipipe_ptd_key_map);
		set_bit(key,&__ipipe_ptd_key_map);
		__ipipe_ptd_key_count++;
	}

	spin_unlock_irqrestore(&__ipipe_pipelock,flags);

	return key;
}

int ipipe_free_ptdkey (int key)
{
	unsigned long flags;

	if (key < 0 || key >= IPIPE_ROOT_NPTDKEYS)
		return -EINVAL;

	spin_lock_irqsave(&__ipipe_pipelock,flags);

	if (test_and_clear_bit(key,&__ipipe_ptd_key_map))
		__ipipe_ptd_key_count--;

	spin_unlock_irqrestore(&__ipipe_pipelock,flags);

	return 0;
}

int ipipe_set_ptd (int key, void *value)

{
	if (key < 0 || key >= IPIPE_ROOT_NPTDKEYS)
		return -EINVAL;

	current->ptd[key] = value;

	return 0;
}

void *ipipe_get_ptd (int key)

{
	if (key < 0 || key >= IPIPE_ROOT_NPTDKEYS)
		return NULL;

	return current->ptd[key];
}

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *ipipe_proc_root;

static int __ipipe_version_info_proc(char *page,
				     char **start,
				     off_t off, int count, int *eof, void *data)
{
	int len = sprintf(page, "%s\n", IPIPE_VERSION_STRING);

	len -= off;

	if (len <= off + count)
		*eof = 1;

	*start = page + off;

	if(len > count)
		len = count;

	if(len < 0)
		len = 0;

	return len;
}

static int __ipipe_common_info_show(struct seq_file *p, void *data)
{
	struct ipipe_domain *ipd = (struct ipipe_domain *)p->private;
	char handling, stickiness, lockbit, exclusive, virtuality;

	unsigned long ctlbits;
	unsigned irq;

	seq_printf(p, "       +----- Handling ([A]ccepted, [G]rabbed, [W]ired, [D]iscarded)\n");
	seq_printf(p, "       |+---- Sticky\n");
	seq_printf(p, "       ||+--- Locked\n");
	seq_printf(p, "       |||+-- Exclusive\n");
	seq_printf(p, "       ||||+- Virtual\n");
	seq_printf(p, "[IRQ]  |||||\n");

	mutex_lock(&ipd->mutex);

	for (irq = 0; irq < IPIPE_NR_IRQS; irq++) {
		/* Remember to protect against
		 * ipipe_virtual_irq/ipipe_control_irq if more fields
		 * get involved. */
		ctlbits = ipd->irqs[irq].control;

		if (irq >= IPIPE_NR_XIRQS && !ipipe_virtual_irq_p(irq))
			/*
			 * There might be a hole between the last external
			 * IRQ and the first virtual one; skip it.
			 */
			continue;

		if (ipipe_virtual_irq_p(irq)
		    && !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map))
			/* Non-allocated virtual IRQ; skip it. */
			continue;

		/*
		 * Statuses are as follows:
		 * o "accepted" means handled _and_ passed down the pipeline.
		 * o "grabbed" means handled, but the interrupt might be
		 * terminated _or_ passed down the pipeline depending on
		 * what the domain handler asks for to the I-pipe.
		 * o "wired" is basically the same as "grabbed", except that
		 * the interrupt is unconditionally delivered to an invariant
		 * pipeline head domain.
		 * o "passed" means unhandled by the domain but passed
		 * down the pipeline.
		 * o "discarded" means unhandled and _not_ passed down the
		 * pipeline. The interrupt merely disappears from the
		 * current domain down to the end of the pipeline.
		 */
		if (ctlbits & IPIPE_HANDLE_MASK) {
			if (ctlbits & IPIPE_PASS_MASK)
				handling = 'A';
			else if (ctlbits & IPIPE_WIRED_MASK)
				handling = 'W';
			else
				handling = 'G';
		} else if (ctlbits & IPIPE_PASS_MASK)
			/* Do not output if no major action is taken. */
			continue;
		else
			handling = 'D';

		if (ctlbits & IPIPE_STICKY_MASK)
			stickiness = 'S';
		else
			stickiness = '.';

		if (ctlbits & IPIPE_LOCK_MASK)
			lockbit = 'L';
		else
			lockbit = '.';

		if (ctlbits & IPIPE_EXCLUSIVE_MASK)
			exclusive = 'X';
		else
			exclusive = '.';

		if (ipipe_virtual_irq_p(irq))
			virtuality = 'V';
		else
			virtuality = '.';

		seq_printf(p, " %3u:  %c%c%c%c%c\n",
			     irq, handling, stickiness, lockbit, exclusive, virtuality);
	}

	seq_printf(p, "[Domain info]\n");

	seq_printf(p, "id=0x%.8x\n", ipd->domid);

	if (test_bit(IPIPE_AHEAD_FLAG,&ipd->flags))
		seq_printf(p, "priority=topmost\n");
	else
		seq_printf(p, "priority=%d\n", ipd->priority);

	mutex_unlock(&ipd->mutex);

	return 0;
}

static int __ipipe_common_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, __ipipe_common_info_show, PROC_I(inode)->pde->data);
}

static struct file_operations __ipipe_info_proc_ops = {
	.owner		= THIS_MODULE,
	.open		= __ipipe_common_info_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void __ipipe_add_domain_proc(struct ipipe_domain *ipd)
{
	struct proc_dir_entry *e = create_proc_entry(ipd->name, 0444, ipipe_proc_root);
	if (e) {
		e->proc_fops = &__ipipe_info_proc_ops;
		e->data = (void*) ipd;
	}
}

void __ipipe_remove_domain_proc(struct ipipe_domain *ipd)
{
	remove_proc_entry(ipd->name,ipipe_proc_root);
}

void __init ipipe_init_proc(void)
{
	ipipe_proc_root = create_proc_entry("ipipe",S_IFDIR, 0);
	create_proc_read_entry("version",0444,ipipe_proc_root,&__ipipe_version_info_proc,NULL);
	__ipipe_add_domain_proc(ipipe_root_domain);

	__ipipe_init_tracer();
}

#endif	/* CONFIG_PROC_FS */

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT

DEFINE_PER_CPU(int, ipipe_percpu_context_check) = { 1 };
DEFINE_PER_CPU(int, ipipe_saved_context_check_state);

void ipipe_check_context(struct ipipe_domain *border_domain)
{
        struct ipipe_percpu_domain_data *p; 
        struct ipipe_domain *this_domain; 
        unsigned long flags;
	int cpu;
 
        local_irq_save_hw_smp(flags); 

        this_domain = __ipipe_current_domain; 
        p = ipipe_head_cpudom_ptr(); 
        if (likely(this_domain->priority <= border_domain->priority && 
		   !test_bit(IPIPE_STALL_FLAG, &p->status))) { 
                local_irq_restore_hw_smp(flags); 
                return; 
        } 
 
	cpu = ipipe_processor_id();
        if (!per_cpu(ipipe_percpu_context_check, cpu)) { 
                local_irq_restore_hw_smp(flags); 
                return; 
        } 
 
        local_irq_restore_hw_smp(flags); 

	ipipe_context_check_off();
	ipipe_trace_panic_freeze();
	ipipe_set_printk_sync(__ipipe_current_domain);

	if (this_domain->priority > border_domain->priority)
		printk(KERN_ERR "I-pipe: Detected illicit call from domain "
				"'%s'\n"
		       KERN_ERR "        into a service reserved for domain "
				"'%s' and below.\n",
		       this_domain->name, border_domain->name);
	else
		printk(KERN_ERR "I-pipe: Detected stalled topmost domain, "
				"probably caused by a bug.\n"
				"        A critical section may have been "
				"left unterminated.\n");
	dump_stack();
	ipipe_trace_panic_dump();
}

EXPORT_SYMBOL(ipipe_check_context);

#endif /* CONFIG_IPIPE_DEBUG_CONTEXT */

#if defined(CONFIG_IPIPE_DEBUG_INTERNAL) && defined(CONFIG_SMP)

int notrace __ipipe_check_percpu_access(void)
{
	struct ipipe_percpu_domain_data *p;
	struct ipipe_domain *this_domain;
	unsigned long flags;
	int ret = 0;

	local_irq_save_hw_notrace(flags);

	this_domain = __raw_get_cpu_var(ipipe_percpu_domain);

	/*
	 * Only the root domain may implement preemptive CPU migration
	 * of tasks, so anything above in the pipeline should be fine.
	 */
	if (this_domain->priority > IPIPE_ROOT_PRIO)
		goto out;

	if (raw_irqs_disabled_flags(flags))
		goto out;

	/*
	 * Last chance: hw interrupts were enabled on entry while
	 * running over the root domain, but the root stage might be
	 * currently stalled, in which case preemption would be
	 * disabled, and no migration could occur.
	 */
	if (this_domain == ipipe_root_domain) {
		p = ipipe_root_cpudom_ptr(); 
		if (test_bit(IPIPE_STALL_FLAG, &p->status))
			goto out;
	}
	/*
	 * Our caller may end up accessing the wrong per-cpu variable
	 * instance due to CPU migration; tell it to complain about
	 * this.
	 */
	ret = 1;
out:
	local_irq_restore_hw_notrace(flags);

	return ret;
}

#endif /* CONFIG_IPIPE_DEBUG_INTERNAL && CONFIG_SMP */

EXPORT_SYMBOL(ipipe_virtualize_irq);
EXPORT_SYMBOL(ipipe_control_irq);
EXPORT_SYMBOL(ipipe_suspend_domain);
EXPORT_SYMBOL(ipipe_alloc_virq);
EXPORT_PER_CPU_SYMBOL(ipipe_percpu_domain);
EXPORT_PER_CPU_SYMBOL(ipipe_percpu_darray);
EXPORT_SYMBOL(ipipe_root);
EXPORT_SYMBOL(ipipe_stall_pipeline_from);
EXPORT_SYMBOL(ipipe_test_and_stall_pipeline_from);
EXPORT_SYMBOL(ipipe_test_and_unstall_pipeline_from);
EXPORT_SYMBOL(ipipe_restore_pipeline_from);
EXPORT_SYMBOL(ipipe_unstall_pipeline_head);
EXPORT_SYMBOL(__ipipe_restore_pipeline_head);
EXPORT_SYMBOL(__ipipe_unstall_root);
EXPORT_SYMBOL(__ipipe_restore_root);
EXPORT_SYMBOL(__ipipe_spin_lock_irq);
EXPORT_SYMBOL(__ipipe_spin_unlock_irq);
EXPORT_SYMBOL(__ipipe_spin_lock_irqsave);
EXPORT_SYMBOL(__ipipe_spin_unlock_irqrestore);
EXPORT_SYMBOL(__ipipe_pipeline);
EXPORT_SYMBOL(__ipipe_lock_irq);
EXPORT_SYMBOL(__ipipe_unlock_irq);
EXPORT_SYMBOL(ipipe_register_domain);
EXPORT_SYMBOL(ipipe_unregister_domain);
EXPORT_SYMBOL(ipipe_free_virq);
EXPORT_SYMBOL(ipipe_init_attr);
EXPORT_SYMBOL(ipipe_catch_event);
EXPORT_SYMBOL(ipipe_alloc_ptdkey);
EXPORT_SYMBOL(ipipe_free_ptdkey);
EXPORT_SYMBOL(ipipe_set_ptd);
EXPORT_SYMBOL(ipipe_get_ptd);
EXPORT_SYMBOL(ipipe_set_irq_affinity);
EXPORT_SYMBOL(ipipe_send_ipi);
EXPORT_SYMBOL(__ipipe_pend_irq);
EXPORT_SYMBOL(__ipipe_set_irq_pending);
#if defined(CONFIG_IPIPE_DEBUG_INTERNAL) && defined(CONFIG_SMP)
EXPORT_SYMBOL(__ipipe_check_percpu_access);
#endif
#ifdef CONFIG_GENERIC_CLOCKEVENTS
EXPORT_SYMBOL(ipipe_request_tickdev);
EXPORT_SYMBOL(ipipe_release_tickdev);
#endif

EXPORT_SYMBOL(ipipe_critical_enter);
EXPORT_SYMBOL(ipipe_critical_exit);
EXPORT_SYMBOL(ipipe_trigger_irq);
EXPORT_SYMBOL(ipipe_get_sysinfo);
