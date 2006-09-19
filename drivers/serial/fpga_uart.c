/*
 * drivers/serial/fpga_uart.c
 *
 * Driver for the PSC of the Freescale MPC52xx PSCs configured as UARTs.
 *
 * FIXME According to the usermanual the status bits in the status register
 * are only updated when the peripherals access the FIFO and not when the
 * CPU access them. So since we use this bits to know when we stop writing
 * and reading, they may not be updated in-time and a race condition may
 * exists. But I haven't be able to prove this and I don't care. But if
 * any problem arises, it might worth checking. The TX/RX FIFO Stats
 * registers should be used in addition.
 * Update: Actually, they seem updated ... At least the bits we use.
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Some of the code has been inspired/copied from the 2.4 code written
 * by Dale Farnsworth <dfarnsworth@mvista.com>.
 *
 * Copyright (C) 2004-2005 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/* Platform device Usage :
 *
 * Since PSCs can have multiple function, the correct driver for each one
 * is selected by calling mpc52xx_match_psc_function(...). The function
 * handled by this driver is "uart".
 *
 * The driver init all necessary registers to place the PSC in uart mode without
 * DCD. However, the pin multiplexing aren't changed and should be set either
 * by the bootloader or in the platform init code.
 *
 * The idx field must be equal to the PSC index ( e.g. 0 for PSC1, 1 for PSC2,
 * and so on). So the PSC1 is mapped to /dev/ttyPSC0, PSC2 to /dev/ttyPSC1 and
 * so on. But be warned, it's an ABSOLUTE REQUIREMENT ! This is needed mainly
 * for the console code : without this 1:1 mapping, at early boot time, when we
 * are parsing the kernel args console=ttyPSC?, we wouldn't know which PSC it
 * will be mapped to.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/sysrq.h>
#include <linux/console.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/serial_core.h>

#define PORT_FPGA		111 // test-only: move to include/linux/serial_core.h

/* We've been assigned a range on the "Low-density serial ports" major */
#define SERIAL_FPGA_MAJOR	204
#define SERIAL_FPGA_MINOR	148

#define ISR_PASS_LIMIT 256	/* Max number of iteration in the interrupt */

#define FPGA_PORT_MAXNUM	3

static struct uart_port fpga_uart_ports[FPGA_PORT_MAXNUM];

/* Forward declaration of the interruption handling routine */
static irqreturn_t fpga_uart_int(int irq,void *dev_id,struct pt_regs *regs);

// test-only...

/* define for debug output */
#define DEBUG // test-only
#ifdef DEBUG
#define debug(fmt,args...)	printk(fmt ,##args)
#else
#define debug(fmt,args...)
#endif

#define FPGA_UART_PORT_SIZE	0x600

#define UART_CTRL_DIV		0x00000fff
#define UART_CTRL_PAR		0x00003000
#define UART_CTRL_PAR_NONE	0x00000000
#define UART_CTRL_PAR_ODD	0x00001000
#define UART_CTRL_PAR_EVEN	0x00002000
#define UART_CTRL_STOP		0x00004000
#define UART_CTRL_STOP_1	0x00000000
#define UART_CTRL_STOP_2	0x00004000
#define UART_CTRL_DATA		0x00018000
#define UART_CTRL_DATA_5	0x00000000
#define UART_CTRL_DATA_6	0x00008000
#define UART_CTRL_DATA_7	0x00010000
#define UART_CTRL_DATA_8	0x00018000
#define UART_CTRL_TXTH_LVL	0x00060000
#define UART_CTRL_TX_LVL	0x00180000
#define UART_CTRL_TX_EMPTY	0x00200000
#define UART_CTRL_RXTH_LVL	0x00C00000
#define UART_CTRL_RX_LVL	0x1F000000
#define UART_CTRL_PAR_ERR	0x20000000
#define UART_CTRL_TX_OVR	0x40000000
#define UART_CTRL_RX_OVR	0x80000000

struct fpga_uart_data {
	const char		*name;
	struct class_device	*class_dev;

	struct semaphore	lock;
	void __iomem		*vbar2;
	phys_addr_t		pbar2;

	int			open_count;
};

struct fpga_uart_data *data; // test-only

#define DRV_NAME		"fpga_uart"

static inline int fpga_uart_int_tx_chars(struct uart_port *port);

/* ======================================================================== */
/* UART operations                                                          */
/* ======================================================================== */

static unsigned int fpga_uart_tx_empty(struct uart_port *port)
{
	unsigned int ctrl = readl((port)->membase);
	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	return (ctrl & UART_CTRL_TX_EMPTY) ? TIOCSER_TEMT : 0;
}

static void fpga_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* Not implemented */
}

static unsigned int fpga_uart_get_mctrl(struct uart_port *port)
{
	// test-only
	/* Not implemented */
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

#define INT_RX		((INT_UART0_RX) << (port->line << 1))
#define INT_TX		((INT_UART0_TX) << (port->line << 1))
#define INT_MASK	((INT_UART0_MASK) << (port->line << 1))

static void fpga_uart_stop_tx(struct uart_port *port)
{
	unsigned long val;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	/*
	 * Disable TX IRQ's in FPGA
	 */
	val = readl(data->vbar2 + IBUF_INT_ENABLE) & ~(INT_TX);
	writel(val, data->vbar2 + IBUF_INT_ENABLE);
}

static void fpga_uart_start_tx(struct uart_port *port)
{
	unsigned long val;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	/*
	 * Enable TX IRQ's in FPGA
	 */
	// test-only: right now only for UART2!!!! & something else could be missing here to start tx engine!!!
	val = readl(data->vbar2 + IBUF_INT_ENABLE) | (INT_TX);
	writel(val, data->vbar2 + IBUF_INT_ENABLE);
	fpga_uart_int_tx_chars(port); // test-only
}

static void fpga_uart_stop_rx(struct uart_port *port)
{
	unsigned long val;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	/*
	 * Disable TX IRQ's in FPGA
	 */
	// test-only: right now only for UART2!!!!
	val = readl(data->vbar2 + IBUF_INT_ENABLE) & ~(INT_RX);
	writel(val, data->vbar2 + IBUF_INT_ENABLE);
}

static void fpga_uart_enable_ms(struct uart_port *port)
{
	/* Not implemented */
}

static void fpga_uart_break_ctl(struct uart_port *port, int ctl)
{
	// test-only: remove function???
}

static int fpga_uart_startup(struct uart_port *port)
{
	int ret;
	unsigned int val;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	/* Request IRQ */
	ret = request_irq(port->irq, fpga_uart_int, SA_INTERRUPT|SA_SHIRQ,
			  "fpga_uart", port);
	if (ret)
		return ret;

	/* Setup for 9600 baud, 8 data, 1 stop, no parity */
	val = UART_CTRL_PAR_NONE | UART_CTRL_STOP_1 | UART_CTRL_DATA_8;
	val |= ((33333000 + ((2*16*9600) / 2)) / (2*16*9600)) - 1;
	writel(val, (port)->membase);

	/*
	 * Enable IBUF IRQ's in FPGA
	 */
	// test-only: only UART2 supported right now!!!
	val = readl(data->vbar2 + IBUF_INT_ENABLE) & ~(INT_MASK);
	val |= INT_GLOBAL | INT_MASK;
	writel(val, data->vbar2 + IBUF_INT_ENABLE);

	return 0;
}

static void fpga_uart_shutdown(struct uart_port *port)
{
	unsigned int val;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	/*
	 * Disable IBUF IRQ's in FPGA
	 */
	val = readl(data->vbar2 + IBUF_INT_ENABLE) & ~(INT_MASK);
	writel(val, data->vbar2 + IBUF_INT_ENABLE);

	/* Release interrupt */
	free_irq(port->irq, port);
}

static void fpga_uart_set_termios(struct uart_port *port, struct termios *new,
				  struct termios *old)
{
	unsigned long flags;
	unsigned int baud;
	unsigned int val = 0;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	switch (new->c_cflag & CSIZE) {
	case CS5:
		val |= UART_CTRL_DATA_5;
		break;
	case CS6:
		val |= UART_CTRL_DATA_6;
		break;
	case CS7:
		val |= UART_CTRL_DATA_7;
		break;
	case CS8:
	default:
		val |= UART_CTRL_DATA_8;
	}

	if (new->c_cflag & PARENB) {
		val |= (new->c_cflag & PARODD) ?
			UART_CTRL_PAR_ODD : UART_CTRL_PAR_EVEN;
	} else
		val |= UART_CTRL_PAR_NONE;

	if (new->c_cflag & CSTOPB)
		val |= UART_CTRL_STOP_2;
	else
		val |= UART_CTRL_STOP_1;

	baud = uart_get_baud_rate(port, new, old, 0, port->uartclk);
	debug("%s: baud=%d\n", __FUNCTION__, baud); // test-only

	/* Get the lock */
	spin_lock_irqsave(&port->lock, flags);

	/* Update the per-port timeout */
	uart_update_timeout(port, new->c_cflag, baud);

#if 0 // test-only
	/* Do our best to flush TX & RX, so we don't loose anything */
	/* But we don't wait indefinitly ! */
	j = 5000000;	/* Maximum wait */
	/* FIXME Can't receive chars since set_termios might be called at early
	 * boot for the console, all stuff is not yet ready to receive at that
	 * time and that just makes the kernel oops */
	/* while (j-- && mpc52xx_uart_int_rx_chars(port)); */
	while (!(in_be16(&psc->mpc52xx_psc_status) & MPC52xx_PSC_SR_TXEMP) &&
	       --j)
		udelay(1);

	if (!j)
		printk(	KERN_ERR "mpc52xx_uart.c: "
			"Unable to flush RX & TX fifos in-time in set_termios."
			"Some chars may have been lost.\n" );
#endif

	/* Setup control register */
	val |= ((33333000 + ((2*16*baud) / 2)) / (2*16*baud)) - 1;
	debug("%s: val=0x%x\n", __FUNCTION__, val); // test-only

	/* Reset all errors */
	val |= 0xe0000000;
	writel(val, (port)->membase);

	/* We're all set, release the lock */
	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *fpga_uart_type(struct uart_port *port)
{
	return port->type == PORT_FPGA ? "FPGA-UART" : NULL;
}

static void fpga_uart_release_port(struct uart_port *port)
{
	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	if (port->flags & UPF_IOREMAP) { /* remapped by us ? */
		iounmap(port->membase);
		port->membase = NULL;
	}

	release_mem_region(port->mapbase, FPGA_UART_PORT_SIZE);
}

static int fpga_uart_request_port(struct uart_port *port)
{
	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	if (port->flags & UPF_IOREMAP) /* Need to remap ? */
		port->membase = ioremap(port->mapbase, FPGA_UART_PORT_SIZE);

	if (!port->membase)
		return -EINVAL;

	return request_mem_region(port->mapbase, FPGA_UART_PORT_SIZE,
				  "fpga_uart") != NULL ? 0 : -EBUSY;
}

static void fpga_uart_config_port(struct uart_port *port, int flags)
{
	if ((flags & UART_CONFIG_TYPE) && (fpga_uart_request_port(port) == 0))
	     	port->type = PORT_FPGA;
}

static int fpga_uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_FPGA)
		return -EINVAL;

	if ((ser->irq != port->irq) ||
	     (ser->io_type != SERIAL_IO_MEM) ||
	     (ser->baud_base != port->uartclk)  ||
	     (ser->iomem_base != (void*)port->mapbase) ||
	     (ser->hub6 != 0))
		return -EINVAL;

	return 0;
}

static struct uart_ops fpga_uart_ops = {
	.tx_empty	= fpga_uart_tx_empty,
	.set_mctrl	= fpga_uart_set_mctrl,
	.get_mctrl	= fpga_uart_get_mctrl,
	.stop_tx	= fpga_uart_stop_tx,
	.start_tx	= fpga_uart_start_tx,
	.stop_rx	= fpga_uart_stop_rx,
	.enable_ms	= fpga_uart_enable_ms,
	.break_ctl	= fpga_uart_break_ctl,
	.startup	= fpga_uart_startup,
	.shutdown	= fpga_uart_shutdown,
	.set_termios	= fpga_uart_set_termios,
	.type		= fpga_uart_type,
	.release_port	= fpga_uart_release_port,
	.request_port	= fpga_uart_request_port,
	.config_port	= fpga_uart_config_port,
	.verify_port	= fpga_uart_verify_port
};

/* ======================================================================== */
/* Interrupt handling                                                       */
/* ======================================================================== */

static inline int fpga_uart_int_rx_chars(struct uart_port *port, struct pt_regs *regs)
{
	struct tty_struct *tty = port->info->tty;
	unsigned char ch, flag;
	unsigned int status;
	unsigned int ctrl; // test-only

	/* While we can read, do so ! */
	status = readl((port)->membase);
	debug("ttyFPGA%d:%s: ctrl=%x\n", port->line, __FUNCTION__, status); // test-only
	debug("ttyFPGA%d:%s: lvl=%x\n", port->line, __FUNCTION__, status & UART_CTRL_RX_LVL); // test-only
	while ((status & UART_CTRL_RX_LVL) != 0) {
		/* Get the char */
		ch = (unsigned char)readl((port)->membase + 0x200);
		debug("ttyFPGA%d:%s: received char %02x from %p\n", port->line, __FUNCTION__, ch, (port)->membase + 0x200); // test-only

		/* Handle sysreq char */
#ifdef SUPPORT_SYSRQ
		if (uart_handle_sysrq_char(port, ch, regs)) {
			port->sysrq = 0;
			continue;
		}
#endif

		/* Store it */

		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(status & (UART_CTRL_PAR_ERR | UART_CTRL_RX_OVR))) {
			    ctrl = readl((port)->membase);
			    if (status & UART_CTRL_RX_OVR) {
				    debug("ttyFPGA%d:%s: RX-Overrun ERROR!\n", port->line, __FUNCTION__); // test-only
				    flag = TTY_FRAME;
				    port->icount.overrun++;
				    writel(UART_CTRL_RX_OVR | ctrl, (port)->membase);
			    } else if (status & UART_CTRL_PAR_ERR) {
				    debug("ttyFPGA%d:%s: RX-Parity ERROR!\n", port->line, __FUNCTION__); // test-only
				    flag = TTY_PARITY;
				    writel(UART_CTRL_PAR_ERR | ctrl, (port)->membase);
			    }
		    }

		    if (uart_handle_sysrq_char(port, ch, regs))
			    goto ignore_char;

		    uart_insert_char(port, status, UART_CTRL_RX_OVR, ch, flag);

	ignore_char:
		    status = readl((port)->membase);
	}

	tty_flip_buffer_push(tty);

	return readl((port)->membase) & UART_CTRL_RX_LVL;
}

static inline int fpga_uart_int_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	/* Process out of band chars */
	if (port->x_char) {
		writel(port->x_char, (port)->membase + 0x5f8);
		port->icount.tx++;
		port->x_char = 0;
		return 1;
	}

	/* Nothing to do ? */
	debug("ttyFPGA%d:%s head=%d tail=%d\n", port->line, __FUNCTION__, xmit->head, xmit->tail); // test-only
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		fpga_uart_stop_tx(port);
		return 0;
	}

	/* Send chars */
	while (readl((port)->membase) & UART_CTRL_TX_LVL) {
		writel(xmit->buf[xmit->tail], (port)->membase + 0x5f8);
		debug("ttyFPGA%d:%s: sending %02x\n", port->line, __FUNCTION__, xmit->buf[xmit->tail]); // test-only
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	/* Wake up */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	/* Maybe we're done after all */
	if (uart_circ_empty(xmit)) {
		fpga_uart_stop_tx(port);
		return 0;
	}

	return 1;
}

static irqreturn_t fpga_uart_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = (struct uart_port *) dev_id;
	unsigned long pass = ISR_PASS_LIMIT;
	unsigned int keepgoing;
	unsigned int status;

	debug("ttyFPGA%d:%s\n", port->line, __FUNCTION__); // test-only
	debug("ttyFPGA%d:%s int_enable=%x int_status=%x\n", port->line, __FUNCTION__, readl(data->vbar2 + IBUF_INT_ENABLE), readl(data->vbar2 + IBUF_INT_STATUS)); // test-only
	spin_lock(&port->lock);

	/* While we have stuff to do, we continue */
	do {
		/* If we don't find anything to do, we stop */
		keepgoing = 0;

		/* Read status */
		// test-only: only UART2 for now!!!
		status = readl(data->vbar2 + IBUF_INT_STATUS) & (INT_MASK);
		debug("%s: int_status=%x\n", __FUNCTION__, readl(data->vbar2 + IBUF_INT_STATUS)); // test-only
// test-only		status &= port->read_status_mask;

		/* Do we need to receive chars ? */
		/* For this RX interrupts must be on and some chars waiting */
		if (status & INT_RX)
			keepgoing |= fpga_uart_int_rx_chars(port, regs);

		/* Do we need to send chars ? */
		/* For this, TX must be ready and TX interrupt enabled */
		if (status & INT_TX)
			keepgoing |= fpga_uart_int_tx_chars(port);

		/* Limit number of iteration */
		if ( !(--pass) )
			keepgoing = 0;

	} while (keepgoing);

	writel(status, data->vbar2 + IBUF_INT_STATUS);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

/* ======================================================================== */
/* UART Driver                                                              */
/* ======================================================================== */

static struct uart_driver fpga_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "fpga_uart",
	.dev_name	= "ttyFPGA",
	.major		= SERIAL_FPGA_MAJOR,
	.minor		= SERIAL_FPGA_MINOR,
	.nr		= FPGA_PORT_MAXNUM,
};

/* ======================================================================== */
/* Platform Driver                                                          */
/* ======================================================================== */

static int __devinit fpga_uart_probe(struct platform_device *dev)
{
	struct resource *res = dev->resource;
	struct uart_port *port = NULL;
	int i, idx, ret;

	/* Check validity & presence */
	idx = dev->id;
	if (idx < 0 || idx >= FPGA_PORT_MAXNUM)
		return -EINVAL;

	/* Init the port structure */
	port = &fpga_uart_ports[idx];

	memset(port, 0x00, sizeof(struct uart_port));

	spin_lock_init(&port->lock);
	port->uartclk	= 33000000 / 2; // test-only
	port->fifosize	= 512;
	port->iotype	= UPIO_MEM;
	port->flags	= UPF_BOOT_AUTOCONF | UPF_IOREMAP; // test-only
	port->line	= idx;
	port->ops	= &fpga_uart_ops;

	/* Search for IRQ and mapbase */
	for (i=0; i<dev->num_resources; i++, res++) {
		if (res->flags & IORESOURCE_MEM)
			port->mapbase = res->start + data->pbar2;
		else if (res->flags & IORESOURCE_IRQ)
			port->irq = res->start;
	}
	if (!port->irq || !port->mapbase)
		return -EINVAL;

	/* Add the port to the uart sub-system */
	ret = uart_add_one_port(&fpga_uart_driver, port);
	if (!ret)
		platform_set_drvdata(dev, (void *)port);

	return ret;
}

static int fpga_uart_remove(struct platform_device *dev)
{
	struct uart_port *port = (struct uart_port *)platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if (port)
		uart_remove_one_port(&fpga_uart_driver, port);

	return 0;
}

static struct platform_driver fpga_uart_platform_driver = {
	.probe		= fpga_uart_probe,
	.remove		= fpga_uart_remove,
	.driver		= {
		.name	= "fpga-uart",
	},
};

/* ======================================================================== */
/* Module                                                                   */
/* ======================================================================== */

static int __init fpga_uart_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: FPGA driver\n");

	if (!(data = kzalloc(sizeof(struct fpga_uart_data), GFP_KERNEL))) {
		ret = -ENOMEM;
		printk(KERN_ERR DRV_NAME ": Out of memory\n");
		return ret; // test-only
	}

	data->name = "fpga-uart";
	init_MUTEX(&data->lock);

	ret = alpr_map_ibuf_fpga(NULL, NULL, &data->vbar2,
				 NULL, NULL, &data->pbar2,
				 NULL);
	if (ret) {
		ret = -ENODEV;
		printk(KERN_ERR DRV_NAME ": Problem accessing FPGA\n");
		return -1; // test-only
	}

	ret = uart_register_driver(&fpga_uart_driver);
	if (ret == 0) {
		ret = platform_driver_register(&fpga_uart_platform_driver);
		if (ret)
			uart_unregister_driver(&fpga_uart_driver);
	}

	return ret;
}

static void __exit fpga_uart_exit(void)
{
	platform_driver_unregister(&fpga_uart_platform_driver);
	uart_unregister_driver(&fpga_uart_driver);
}

module_init(fpga_uart_init);
module_exit(fpga_uart_exit);

MODULE_AUTHOR("Stefan Roese <sr@denx.de>");
MODULE_DESCRIPTION("Prodrive FPGA UART");
MODULE_LICENSE("GPL");
