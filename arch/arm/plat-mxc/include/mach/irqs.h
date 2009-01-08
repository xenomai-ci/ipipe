/*
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_IRQS_H__
#define __ASM_ARCH_MXC_IRQS_H__

#include <mach/hardware.h>

#ifdef CONFIG_IPIPE
#ifdef CONFIG_ARCH_MX3
#define __ipipe_mach_irq_mux_p(irq)				\
	((irq) == MXC_INT_GPIO1					\
	 || (irq) == MXC_INT_GPIO2 || (irq) == MXC_INT_GPIO3)
#elif CONFIG_ARCH_MX2
#define __ipipe_mach_irq_mux_p(irq) ((irq) == MXC_INT_GPIO)
#endif /* CONFIG_ARCH_MX2 */
#endif /* CONFIG_IPIPE */

#endif /* __ASM_ARCH_MXC_IRQS_H__ */
