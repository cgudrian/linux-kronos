/* -*- linux-c -*-
 * include/linux/ipipe_compat.h
 *
 * Copyright (C) 2007 Philippe Gerum.
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

#ifndef __LINUX_IPIPE_COMPAT_H
#define __LINUX_IPIPE_COMPAT_H

#ifdef CONFIG_IPIPE_COMPAT
/*
 * OBSOLETE: defined only for backward compatibility. Will be removed
 * in future releases, please update client code accordingly.
 */

#ifdef CONFIG_SMP
#define ipipe_declare_cpuid	int cpuid
#define ipipe_load_cpuid()	do { \
					cpuid = ipipe_processor_id();	\
				} while(0)
#define ipipe_lock_cpu(flags)	do { \
					local_irq_save_hw(flags); \
					cpuid = ipipe_processor_id(); \
				} while(0)
#define ipipe_unlock_cpu(flags)	local_irq_restore_hw(flags)
#define ipipe_get_cpu(flags)	ipipe_lock_cpu(flags)
#define ipipe_put_cpu(flags)	ipipe_unlock_cpu(flags)
#else /* !CONFIG_SMP */
#define ipipe_declare_cpuid	const int cpuid = 0
#define ipipe_load_cpuid()	do { } while(0)
#define ipipe_lock_cpu(flags)	local_irq_save_hw(flags)
#define ipipe_unlock_cpu(flags)	local_irq_restore_hw(flags)
#define ipipe_get_cpu(flags)	do { (void)(flags); } while(0)
#define ipipe_put_cpu(flags)	do { } while(0)
#endif /* CONFIG_SMP */

#endif /* CONFIG_IPIPE_COMPAT */

#endif	/* !__LINUX_IPIPE_COMPAT_H */
