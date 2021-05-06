/*   -*- linux-c -*-
 *   include/linux/ipipe_lock.h
 *
 *   Copyright (C) 2009 Philippe Gerum.
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

#ifndef __LINUX_IPIPE_LOCK_H
#define __LINUX_IPIPE_LOCK_H

typedef struct {
	raw_spinlock_t bare_lock;
} __ipipe_spinlock_t;

#define ipipe_lock_p(lock)						\
	__builtin_types_compatible_p(typeof(lock), __ipipe_spinlock_t *)

#define common_lock_p(lock)						\
	__builtin_types_compatible_p(typeof(lock), spinlock_t *)

#define bare_lock(lock)	(&((__ipipe_spinlock_t *)(lock))->bare_lock)
#define std_lock(lock)	((spinlock_t *)(lock))

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)

extern int __bad_spinlock_type(void);
#define PICK_SPINLOCK_IRQSAVE(lock, flags)				\
	do {								\
		if (ipipe_lock_p(lock))					\
			(flags) = __ipipe_spin_lock_irqsave(bare_lock(lock)); \
		else if (common_lock_p(lock))				\
			(flags) = _spin_lock_irqsave(std_lock(lock));	\
		else __bad_spinlock_type();				\
	} while (0)

#else /* !(CONFIG_SMP || CONFIG_DEBUG_SPINLOCK) */

#define PICK_SPINLOCK_IRQSAVE(lock, flags)				\
	do {								\
		if (ipipe_lock_p(lock))					\
			(flags) = __ipipe_spin_lock_irqsave(bare_lock(lock)); \
		else if (common_lock_p(lock))				\
			_spin_lock_irqsave(std_lock(lock), flags);	\
	} while (0)

#endif /* !(CONFIG_SMP || CONFIG_DEBUG_SPINLOCK) */

#define PICK_SPINUNLOCK_IRQRESTORE(lock, flags)				\
	do {								\
		if (ipipe_lock_p(lock))					\
			__ipipe_spin_unlock_irqrestore(bare_lock(lock), flags); \
		else if (common_lock_p(lock))				\
			_spin_unlock_irqrestore(std_lock(lock), flags); \
	} while (0)

#define PICK_SPINOP(op, lock)						\
	do {								\
		if (ipipe_lock_p(lock))					\
			__raw_spin##op(bare_lock(lock));		\
		else if (common_lock_p(lock))				\
			_spin##op(std_lock(lock));			\
	} while (0)

#define PICK_SPINOP_IRQ(op, lock)					\
	do {								\
		if (ipipe_lock_p(lock))					\
			__ipipe_spin##op##_irq(bare_lock(lock));	\
		else if (common_lock_p(lock))				\
			_spin##op##_irq(std_lock(lock));		\
	} while (0)

#define __raw_spin_lock_init(lock)					\
	do {								\
		IPIPE_DEFINE_SPINLOCK(__lock__);			\
		*((ipipe_spinlock_t *)lock) = __lock__;			\
	} while (0)

#ifdef CONFIG_IPIPE

#define ipipe_spinlock_t		__ipipe_spinlock_t
#define IPIPE_DEFINE_SPINLOCK(x)	ipipe_spinlock_t x = IPIPE_SPIN_LOCK_UNLOCKED
#define IPIPE_DECLARE_SPINLOCK(x)	extern ipipe_spinlock_t x
#define IPIPE_SPIN_LOCK_UNLOCKED	\
	(__ipipe_spinlock_t) {	.bare_lock = __RAW_SPIN_LOCK_UNLOCKED }

#define spin_lock_irqsave_cond(lock, flags) \
	spin_lock_irqsave(lock, flags)

#define spin_unlock_irqrestore_cond(lock, flags) \
	spin_unlock_irqrestore(lock, flags)

void __ipipe_spin_lock_irq(raw_spinlock_t *lock);

void __ipipe_spin_unlock_irq(raw_spinlock_t *lock);

unsigned long __ipipe_spin_lock_irqsave(raw_spinlock_t *lock);

void __ipipe_spin_unlock_irqrestore(raw_spinlock_t *lock,
				    unsigned long x);

void __ipipe_spin_unlock_irqbegin(ipipe_spinlock_t *lock);

void __ipipe_spin_unlock_irqcomplete(unsigned long x);

#else /* !CONFIG_IPIPE */

#define ipipe_spinlock_t		spinlock_t
#define IPIPE_DEFINE_SPINLOCK(x)	DEFINE_SPINLOCK(x)
#define IPIPE_DECLARE_SPINLOCK(x)	extern spinlock_t x
#define IPIPE_SPIN_LOCK_UNLOCKED        SPIN_LOCK_UNLOCKED

#define spin_lock_irqsave_cond(lock, flags)		\
	do {						\
		(void)(flags);				\
		spin_lock(lock);			\
	} while(0)

#define spin_unlock_irqrestore_cond(lock, flags)	\
	spin_unlock(lock)

#define __ipipe_spin_lock_irq(lock)		do { } while (0)
#define __ipipe_spin_unlock_irq(lock)		do { } while (0)
#define __ipipe_spin_lock_irqsave(lock)		0
#define __ipipe_spin_unlock_irqrestore(lock, x)	do { (void)(x); } while (0)
#define __ipipe_spin_unlock_irqbegin(lock)	do { } while (0)
#define __ipipe_spin_unlock_irqcomplete(x)	do { (void)(x); } while (0)

#endif /* !CONFIG_IPIPE */

#endif /* !__LINUX_IPIPE_LOCK_H */
