/*
 * linux/arch/arm/plat-omap/i2c.c
 *
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Jarkko Nikula <jhnikula@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <mach/irqs.h>
#include <plat/mux.h>
#include <plat/i2c.h>
#include <plat/omap_device.h>

#define OMAP_I2C_SIZE		0x3f
#define OMAP1_I2C_BASE		0xfffb3800

static const char name[] = "omap_i2c";

#define I2C_RESOURCE_BUILDER(base, irq)			\
	{						\
		.start	= (base),			\
		.end	= (base) + OMAP_I2C_SIZE,	\
		.flags	= IORESOURCE_MEM,		\
	},						\
	{						\
		.start	= (irq),			\
		.flags	= IORESOURCE_IRQ,		\
	},

static struct resource i2c_resources[][2] = {
	{ I2C_RESOURCE_BUILDER(0, 0) },
};

#define I2C_DEV_BUILDER(bus_id, res, data)		\
	{						\
		.id	= (bus_id),			\
		.name	= name,				\
		.num_resources	= ARRAY_SIZE(res),	\
		.resource	= (res),		\
		.dev		= {			\
			.platform_data	= (data),	\
		},					\
	}

#define MAX_OMAP_I2C_HWMOD_NAME_LEN	16
#define OMAP_I2C_MAX_CONTROLLERS 4
static struct omap_i2c_bus_platform_data i2c_pdata[OMAP_I2C_MAX_CONTROLLERS];
static struct platform_device omap_i2c_devices[] = {
	I2C_DEV_BUILDER(1, i2c_resources[0], &i2c_pdata[0]),
};

#define OMAP_I2C_CMDLINE_SETUP	(BIT(31))

static int __init omap_i2c_nr_ports(void)
{
	int ports = 0;

	if (cpu_class_is_omap1())
		ports = 1;
	else if (cpu_is_omap24xx())
		ports = 2;
	else if (cpu_is_omap34xx())
		ports = 3;
	else if (cpu_is_omap44xx())
		ports = 4;

	return ports;
}

static inline int omap1_i2c_add_bus(int bus_id)
{
	struct platform_device *pdev;
	struct omap_i2c_bus_platform_data *pdata;
	struct resource *res;

	omap1_i2c_mux_pins(bus_id);

	pdev = &omap_i2c_devices[bus_id - 1];
	res = pdev->resource;
	res[0].start = OMAP1_I2C_BASE;
	res[0].end = res[0].start + OMAP_I2C_SIZE;
	res[1].start = INT_I2C;
	pdata = &i2c_pdata[bus_id - 1];

	/* all OMAP1 have IP version 1 register set */
	pdata->rev = OMAP_I2C_IP_VERSION_1;

	/* all OMAP1 I2C are implemented like this */
	pdata->flags = OMAP_I2C_FLAG_NO_FIFO |
		       OMAP_I2C_FLAG_SIMPLE_CLOCK |
		       OMAP_I2C_FLAG_16BIT_DATA_REG |
		       OMAP_I2C_FLAG_ALWAYS_ARMXOR_CLK;

	/* how the cpu bus is wired up differs for 7xx only */

	if (cpu_is_omap7xx())
		pdata->flags |= OMAP_I2C_FLAG_BUS_SHIFT_1;
	else
		pdata->flags |= OMAP_I2C_FLAG_BUS_SHIFT_2;

	return platform_device_register(pdev);
}


#ifdef CONFIG_ARCH_OMAP2PLUS
static inline int omap2_i2c_add_bus(int bus_id)
{
	int l;
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	char oh_name[MAX_OMAP_I2C_HWMOD_NAME_LEN];
	struct omap_i2c_bus_platform_data *pdata;
	struct omap_i2c_dev_attr *dev_attr;

	omap2_i2c_mux_pins(bus_id);

	l = snprintf(oh_name, MAX_OMAP_I2C_HWMOD_NAME_LEN, "i2c%d", bus_id);
	WARN(l >= MAX_OMAP_I2C_HWMOD_NAME_LEN,
		"String buffer overflow in I2C%d device setup\n", bus_id);
	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
			pr_err("Could not look up %s\n", oh_name);
			return -EEXIST;
	}

	pdata = &i2c_pdata[bus_id - 1];
	/*
	 * pass the hwmod class's CPU-specific knowledge of I2C IP revision in
	 * use, and functionality implementation flags, up to the OMAP I2C
	 * driver via platform data
	 */
	pdata->rev = oh->class->rev;

	dev_attr = (struct omap_i2c_dev_attr *)oh->dev_attr;
	pdata->flags = dev_attr->flags;

	pdev = omap_device_build(name, bus_id, oh, pdata,
			sizeof(struct omap_i2c_bus_platform_data),
			NULL, 0, 0);
	WARN(IS_ERR(pdev), "Could not build omap_device for %s\n", name);

	return PTR_RET(pdev);
}
#else
static inline int omap2_i2c_add_bus(int bus_id)
{
	return 0;
}
#endif

static int __init omap_i2c_add_bus(int bus_id)
{
	if (cpu_class_is_omap1())
		return omap1_i2c_add_bus(bus_id);
	else
		return omap2_i2c_add_bus(bus_id);
}

/**
 * omap_i2c_bus_setup - Process command line options for the I2C bus speed
 * @str: String of options
 *
 * This function allow to override the default I2C bus speed for given I2C
 * bus with a command line option.
 *
 * Format: i2c_bus=bus_id,clkrate (in kHz)
 *
 * Returns 1 on success, 0 otherwise.
 */
static int __init omap_i2c_bus_setup(char *str)
{
	int ports;
	int ints[3];

	ports = omap_i2c_nr_ports();
	get_options(str, 3, ints);
	if (ints[0] < 2 || ints[1] < 1 || ints[1] > ports)
		return 0;
	i2c_pdata[ints[1] - 1].clkrate = ints[2];
	i2c_pdata[ints[1] - 1].clkrate |= OMAP_I2C_CMDLINE_SETUP;

	return 1;
}
__setup("i2c_bus=", omap_i2c_bus_setup);

/*
 * Register busses defined in command line but that are not registered with
 * omap_register_i2c_bus from board initialization code.
 */
static int __init omap_register_i2c_bus_cmdline(void)
{
	int i, err = 0;

	for (i = 0; i < ARRAY_SIZE(i2c_pdata); i++)
		if (i2c_pdata[i].clkrate & OMAP_I2C_CMDLINE_SETUP) {
			i2c_pdata[i].clkrate &= ~OMAP_I2C_CMDLINE_SETUP;
			err = omap_i2c_add_bus(i + 1);
			if (err)
				goto out;
		}

out:
	return err;
}
subsys_initcall(omap_register_i2c_bus_cmdline);

/**
 * omap_register_i2c_bus - register I2C bus with device descriptors
 * @bus_id: bus id counting from number 1
 * @clkrate: clock rate of the bus in kHz
 * @info: pointer into I2C device descriptor table or NULL
 * @len: number of descriptors in the table
 *
 * Returns 0 on success or an error code.
 */
int __init omap_register_i2c_bus(int bus_id, u32 clkrate,
			  struct i2c_board_info const *info,
			  unsigned len)
{
	int err;

	BUG_ON(bus_id < 1 || bus_id > omap_i2c_nr_ports());

	if (info) {
		err = i2c_register_board_info(bus_id, info, len);
		if (err)
			return err;
	}

	if (!i2c_pdata[bus_id - 1].clkrate)
		i2c_pdata[bus_id - 1].clkrate = clkrate;

	i2c_pdata[bus_id - 1].clkrate &= ~OMAP_I2C_CMDLINE_SETUP;

	return omap_i2c_add_bus(bus_id);
}
