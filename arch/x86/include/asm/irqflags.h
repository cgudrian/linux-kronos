#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__

#include <linux/ipipe_base.h>
#include <linux/ipipe_trace.h>

/*
 * Interrupt control:
 */

static inline unsigned long native_save_fl(void)
{
	unsigned long flags;

#ifdef CONFIG_IPIPE
	flags = (!__ipipe_test_root()) << 9;
	barrier();
#else
	/*
	 * "=rm" is safe here, because "pop" adjusts the stack before
	 * it evaluates its effective address -- this is part of the
	 * documented behavior of the "pop" instruction.
	 */
	asm volatile("# __raw_save_flags\n\t"
		     "pushf ; pop %0"
		     : "=rm" (flags)
		     : /* no input */
		     : "memory");
#endif

	return flags;
}

static inline void native_restore_fl(unsigned long flags)
{
#ifdef CONFIG_IPIPE
	barrier();
	__ipipe_restore_root(!(flags & X86_EFLAGS_IF));
#else
	asm volatile("push %0 ; popf"
		     : /* no output */
		     :"g" (flags)
		     :"memory", "cc");
#endif
}

static inline void native_irq_disable(void)
{
#ifdef CONFIG_IPIPE
	ipipe_check_context(ipipe_root_domain);
	__ipipe_stall_root();
	barrier();
#else
	asm volatile("cli": : :"memory");
#endif
}

static inline void native_irq_enable(void)
{
#ifdef CONFIG_IPIPE
	barrier();
	__ipipe_unstall_root();
#else
	asm volatile("sti": : :"memory");
#endif
}

static inline void native_safe_halt(void)
{
#ifdef CONFIG_IPIPE
	barrier();
	__ipipe_halt_root();
#else
	asm volatile("sti; hlt": : :"memory");
#endif
}

static inline void native_halt(void)
{
	asm volatile("hlt": : :"memory");
}

#endif

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__

static inline unsigned long __raw_local_save_flags(void)
{
	return native_save_fl();
}

static inline void raw_local_irq_restore(unsigned long flags)
{
	native_restore_fl(flags);
}

static inline unsigned long raw_mangle_irq_bits(int virt, unsigned long real)
{
	/*
	 * Merge virtual and real interrupt mask bits into a single
	 * (32bit) word.
	 */
	return (real & ~(1L << 31)) | ((virt != 0) << 31);
}

static inline int raw_demangle_irq_bits(unsigned long *x)
{
	int virt = (*x & (1L << 31)) != 0;
	*x &= ~(1L << 31);
	return virt;
}

#define local_irq_save_hw_notrace(x) \
	__asm__ __volatile__("pushf ; pop %0 ; cli":"=g" (x): /* no input */ :"memory")
#define local_irq_restore_hw_notrace(x) \
	__asm__ __volatile__("push %0 ; popf": /* no output */ :"g" (x):"memory", "cc")

#define local_save_flags_hw(x)	__asm__ __volatile__("pushf ; pop %0":"=g" (x): /* no input */)

#define irqs_disabled_hw()		\
    ({					\
	unsigned long x;		\
	local_save_flags_hw(x);		\
	!((x) & X86_EFLAGS_IF);		\
    })

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
#define local_irq_disable_hw() do {			\
		if (!irqs_disabled_hw()) {		\
			local_irq_disable_hw_notrace();	\
			ipipe_trace_begin(0x80000000);	\
		}					\
	} while (0)
#define local_irq_enable_hw() do {			\
		if (irqs_disabled_hw()) {		\
			ipipe_trace_end(0x80000000);	\
			local_irq_enable_hw_notrace();	\
		}					\
	} while (0)
#define local_irq_save_hw(x) do {			\
		local_save_flags_hw(x);			\
		if ((x) & X86_EFLAGS_IF) {		\
			local_irq_disable_hw_notrace();	\
			ipipe_trace_begin(0x80000001);	\
		}					\
	} while (0)
#define local_irq_restore_hw(x) do {			\
		if ((x) & X86_EFLAGS_IF)		\
			ipipe_trace_end(0x80000001);	\
		local_irq_restore_hw_notrace(x);	\
	} while (0)
#else /* !CONFIG_IPIPE_TRACE_IRQSOFF */
#define local_irq_save_hw(x)		local_irq_save_hw_notrace(x)
#define local_irq_restore_hw(x)		local_irq_restore_hw_notrace(x)
#define local_irq_enable_hw()		local_irq_enable_hw_notrace()
#define local_irq_disable_hw()		local_irq_disable_hw_notrace()
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

#define local_irq_disable_hw_notrace()	__asm__ __volatile__("cli": : :"memory")
#define local_irq_enable_hw_notrace()	__asm__ __volatile__("sti": : :"memory")

static inline void raw_local_irq_disable(void)
{
	native_irq_disable();
}

static inline void raw_local_irq_enable(void)
{
	native_irq_enable();
}

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void raw_safe_halt(void)
{
	native_safe_halt();
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	native_halt();
}

/*
 * For spinlocks, etc:
 */
static inline unsigned long __raw_local_irq_save(void)
{
#ifdef CONFIG_IPIPE
	unsigned long flags = (!__ipipe_test_and_stall_root()) << 9;
	barrier();
#else
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_disable();
#endif

	return flags;
}
#else

#ifdef CONFIG_IPIPE
#ifdef CONFIG_X86_32
#define DISABLE_INTERRUPTS(clobbers)	PER_CPU(ipipe_percpu_darray, %eax); btsl $0,(%eax); sti
#define ENABLE_INTERRUPTS(clobbers)	call __ipipe_unstall_root
#else /* CONFIG_X86_64 */
/* Not worth virtualizing in x86_64 mode. */
#define DISABLE_INTERRUPTS(clobbers)	cli
#define ENABLE_INTERRUPTS(clobbers)	sti
#endif /* CONFIG_X86_64 */
#define ENABLE_INTERRUPTS_HW_COND	sti
#define DISABLE_INTERRUPTS_HW_COND	cli
#define DISABLE_INTERRUPTS_HW(clobbers)	cli
#define ENABLE_INTERRUPTS_HW(clobbers)	sti
#else /* !CONFIG_IPIPE */
#define ENABLE_INTERRUPTS(x)		sti
#define DISABLE_INTERRUPTS(x)		cli
#define ENABLE_INTERRUPTS_HW_COND
#define DISABLE_INTERRUPTS_HW_COND
#define DISABLE_INTERRUPTS_HW(clobbers)	DISABLE_INTERRUPTS(clobbers)
#define ENABLE_INTERRUPTS_HW(clobbers)	ENABLE_INTERRUPTS(clobbers)
#endif /* !CONFIG_IPIPE */

#ifdef CONFIG_X86_64
#define SWAPGS	swapgs
/*
 * Currently paravirt can't handle swapgs nicely when we
 * don't have a stack we can rely on (such as a user space
 * stack).  So we either find a way around these or just fault
 * and emulate if a guest tries to call swapgs directly.
 *
 * Either way, this is a good way to document that we don't
 * have a reliable stack. x86_64 only.
 */
#define SWAPGS_UNSAFE_STACK	swapgs

#define PARAVIRT_ADJUST_EXCEPTION_FRAME	/*  */

#define INTERRUPT_RETURN	iretq
#define USERGS_SYSRET64				\
	swapgs;					\
	sysretq;
#define USERGS_SYSRET32				\
	swapgs;					\
	sysretl
#define ENABLE_INTERRUPTS_SYSEXIT32		\
	swapgs;					\
	sti;					\
	sysexit

#else
#define INTERRUPT_RETURN		iret
#define ENABLE_INTERRUPTS_SYSEXIT	sti; sysexit
#define GET_CR0_INTO_EAX		movl %cr0, %eax
#endif


#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */

#ifndef __ASSEMBLY__
#define raw_local_save_flags(flags)				\
	do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_save(flags) do {			\
		ipipe_check_context(ipipe_root_domain);	\
		(flags) = __raw_local_irq_save();	\
	} while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF);
}

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
}

#else

#ifdef CONFIG_X86_64
#define ARCH_LOCKDEP_SYS_EXIT		call lockdep_sys_exit_thunk
#define ARCH_LOCKDEP_SYS_EXIT_IRQ	\
	TRACE_IRQS_ON; \
	sti; \
	SAVE_REST; \
	LOCKDEP_SYS_EXIT; \
	RESTORE_REST; \
	cli; \
	TRACE_IRQS_OFF;

#else
#define ARCH_LOCKDEP_SYS_EXIT			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call lockdep_sys_exit;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_LOCKDEP_SYS_EXIT_IRQ
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		call trace_hardirqs_on_thunk;
#  define TRACE_IRQS_OFF	call trace_hardirqs_off_thunk;
#else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#  define LOCKDEP_SYS_EXIT	ARCH_LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ	ARCH_LOCKDEP_SYS_EXIT_IRQ
# else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
# endif

#endif /* __ASSEMBLY__ */
#endif
