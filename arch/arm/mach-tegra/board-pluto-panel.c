/*
 * arch/arm/mach-tegra/board-pluto-panel.c
 *
 * Copyright (C) 2012-2013 NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/ioport.h>
#include <linux/fb.h>
#include <linux/nvmap.h>
#include <linux/nvhost.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/tegra_pwm_bl.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm_backlight.h>
#include <linux/i2c/pca953x.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dc.h>

#include "board.h"
#include "devices.h"
#include "gpio-names.h"
#include "board-pluto.h"
#include "board-panel.h"
#include "common.h"

#include "tegra11_host1x_devices.h"

#if CONFIG_ESD_READ_TE
#define S_TO_MS(x)			(1000 * (x))
static int s_timeoutcount = 0;
static struct timer_list te_watchdog_timer;
volatile static int g_SuspendFlag = -1;
#endif

struct platform_device * __init pluto_host1x_init(void)
{
	struct platform_device *pdev = NULL;
#ifdef CONFIG_TEGRA_GRHOST
	pdev = tegra11_register_host1x_devices();
	if (!pdev) {
		pr_err("host1x devices registration failed\n");
		return NULL;
	}
#endif
	return pdev;
}

#ifdef CONFIG_BACKLIGHT_1_WIRE_MODE
extern void tps61165_pin_init(void);
#endif
extern int prop_add( char *devname, char *item, char *value);
#define LCD_DEV_NAME "LCDScreen"

#ifdef CONFIG_TEGRA_DC

/* hdmi pins for hotplug */
#define pluto_hdmi_hpd		TEGRA_GPIO_PN7
/* hdmi related regulators */


static struct regulator *pluto_hdmi_vddio;
static struct regulator *pluto_hdmi_reg;
static struct regulator *pluto_hdmi_pll;

static struct resource pluto_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.start	= 0, /* Filled in by pluto_panel_init() */
		.end	= 0, /* Filled in by pluto_panel_init() */
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "dsi_regs",
		.start	= 0, /* Filled in the panel file by init_resources() */
		.end	= 0, /* Filled in the panel file by init_resources() */
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "mipi_cal",
		.start	= TEGRA_MIPI_CAL_BASE,
		.end	= TEGRA_MIPI_CAL_BASE + TEGRA_MIPI_CAL_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource pluto_disp2_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_B_GENERAL,
		.end	= INT_DISPLAY_B_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY2_BASE,
		.end	= TEGRA_DISPLAY2_BASE + TEGRA_DISPLAY2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.start	= 0, /* Filled in by pluto_panel_init() */
		.end	= 0, /* Filled in by pluto_panel_init() */
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_dc_sd_settings sd_settings;

static struct tegra_dc_out pluto_disp1_out = {
	.type		= TEGRA_DC_OUT_DSI,
	.sd_settings	= NULL,//&sd_settings,//disable didim backlight
};

static int pluto_hdmi_enable(struct device *dev)
{
	int ret;
	if (!pluto_hdmi_reg) {
		pluto_hdmi_reg = regulator_get(dev, "avdd_hdmi");
		if (IS_ERR_OR_NULL(pluto_hdmi_reg)) {
			pr_err("hdmi: couldn't get regulator avdd_hdmi\n");
			pluto_hdmi_reg = NULL;
			return PTR_ERR(pluto_hdmi_reg);
		}
	}
	ret = regulator_enable(pluto_hdmi_reg);
	if (ret < 0) {
		pr_err("hdmi: couldn't enable regulator avdd_hdmi\n");
		return ret;
	}
	if (!pluto_hdmi_pll) {
		pluto_hdmi_pll = regulator_get(dev, "avdd_hdmi_pll");
		if (IS_ERR_OR_NULL(pluto_hdmi_pll)) {
			pr_err("hdmi: couldn't get regulator avdd_hdmi_pll\n");
			pluto_hdmi_pll = NULL;
			regulator_put(pluto_hdmi_reg);
			pluto_hdmi_reg = NULL;
			return PTR_ERR(pluto_hdmi_pll);
		}
	}
	ret = regulator_enable(pluto_hdmi_pll);
	if (ret < 0) {
		pr_err("hdmi: couldn't enable regulator avdd_hdmi_pll\n");
		return ret;
	}
	return 0;
}

static int pluto_hdmi_disable(void)
{
	if (pluto_hdmi_reg) {
		regulator_disable(pluto_hdmi_reg);
		regulator_put(pluto_hdmi_reg);
		pluto_hdmi_reg = NULL;
	}

	if (pluto_hdmi_pll) {
		regulator_disable(pluto_hdmi_pll);
		regulator_put(pluto_hdmi_pll);
		pluto_hdmi_pll = NULL;
	}

	return 0;
}

static int pluto_hdmi_postsuspend(void)
{
	if (pluto_hdmi_vddio) {
		regulator_disable(pluto_hdmi_vddio);
		regulator_put(pluto_hdmi_vddio);
		pluto_hdmi_vddio = NULL;
	}
	return 0;
}

static int pluto_hdmi_hotplug_init(struct device *dev)
{
	int ret = 0;
	if (!pluto_hdmi_vddio) {
		pluto_hdmi_vddio = regulator_get(dev, "vdd_hdmi_5v0");
		if (IS_ERR_OR_NULL(pluto_hdmi_vddio)) {
			ret = PTR_ERR(pluto_hdmi_vddio);
			pr_err("hdmi: couldn't get regulator vdd_hdmi_5v0\n");
			pluto_hdmi_vddio = NULL;
			return ret;
		}
	}
	ret = regulator_enable(pluto_hdmi_vddio);
	if (ret < 0) {
		pr_err("hdmi: couldn't enable regulator vdd_hdmi_5v0\n");
		regulator_put(pluto_hdmi_vddio);
		pluto_hdmi_vddio = NULL;
		return ret;
	}
	return ret;
}

static struct tegra_dc_out pluto_disp2_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_HIGH,
	.parent_clk	= "pll_d2_out0",

	.dcc_bus	= 3,
	.hotplug_gpio	= pluto_hdmi_hpd,

	.max_pixclock	= KHZ2PICOS(297000),

	.enable		= pluto_hdmi_enable,
	.disable	= pluto_hdmi_disable,
	.postsuspend	= pluto_hdmi_postsuspend,
	.hotplug_init	= pluto_hdmi_hotplug_init,
};

static struct tegra_fb_data pluto_disp1_fb_data = {
	.win		= 0,
	.bits_per_pixel = 32,
	.flags		= TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_platform_data pluto_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &pluto_disp1_out,
	.fb		= &pluto_disp1_fb_data,
	.emc_clk_rate	= 204000000,
#ifdef CONFIG_TEGRA_DC_CMU
	.cmu_enable	= 1,
#endif
};

static struct tegra_fb_data pluto_disp2_fb_data = {
	.win		= 0,
	.xres		= 1024,
	.yres		= 600,
	.bits_per_pixel = 32,
	.flags		= TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_platform_data pluto_disp2_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &pluto_disp2_out,
	.fb		= &pluto_disp2_fb_data,
	.emc_clk_rate	= 300000000,
};

static struct platform_device pluto_disp2_device = {
	.name		= "tegradc",
	.id		= 1,
	.resource	= pluto_disp2_resources,
	.num_resources	= ARRAY_SIZE(pluto_disp2_resources),
	.dev = {
		.platform_data = &pluto_disp2_pdata,
	},
};

static struct platform_device pluto_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= pluto_disp1_resources,
	.num_resources	= ARRAY_SIZE(pluto_disp1_resources),
	.dev = {
		.platform_data = &pluto_disp1_pdata,
	},
};

static struct nvmap_platform_carveout pluto_carveouts[] = {
	[0] = {
		.name		= "iram",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IRAM,
		.base		= TEGRA_IRAM_BASE + TEGRA_RESET_HANDLER_SIZE,
		.size		= TEGRA_IRAM_SIZE - TEGRA_RESET_HANDLER_SIZE,
		.buddy_size	= 0, /* no buddy allocation for IRAM */
	},
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.base		= 0, /* Filled in by pluto_panel_init() */
		.size		= 0, /* Filled in by pluto_panel_init() */
		.buddy_size	= SZ_32K,
	},
	[2] = {
		.name		= "vpr",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VPR,
		.base		= 0, /* Filled in by pluto_panel_init() */
		.size		= 0, /* Filled in by pluto_panel_init() */
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data pluto_nvmap_data = {
	.carveouts	= pluto_carveouts,
	.nr_carveouts	= ARRAY_SIZE(pluto_carveouts),
};

static struct platform_device pluto_nvmap_device __initdata = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &pluto_nvmap_data,
	},
};

static struct tegra_dc_sd_settings pluto_sd_settings = {
	.enable = 1, /* enabled by default */
	.use_auto_pwm = false,
	.hw_update_delay = 0,
	.bin_width = -1,
	.aggressiveness = 5,
	.use_vid_luma = false,
	.phase_in_adjustments = 0,
	.k_limit_enable = true,
	.k_limit = 180,
	.sd_window_enable = false,
	.soft_clipping_enable = true,
	/* Low soft clipping threshold to compensate for aggressive k_limit */
	.soft_clipping_threshold = 128,
	.smooth_k_enable = true,
	.smooth_k_incr = 4,
	/* Default video coefficients */
	.coeff = {5, 9, 2},
	.fc = {0, 0},
	/* Immediate backlight changes */
	.blp = {1024, 255},
	/* Gammas: R: 2.2 G: 2.2 B: 2.2 */
	/* Default BL TF */
	.bltf = {
			{
				{57, 65, 73, 82},
				{92, 103, 114, 125},
				{138, 150, 164, 178},
				{193, 208, 224, 241},
			},
		},
	/* Default LUT */
	.lut = {
			{
				{255, 255, 255},
				{199, 199, 199},
				{153, 153, 153},
				{116, 116, 116},
				{85, 85, 85},
				{59, 59, 59},
				{36, 36, 36},
				{17, 17, 17},
				{0, 0, 0},
			},
		},
	.sd_brightness = &sd_brightness,
	.use_vpulse2 = true,
};

#if CONFIG_ESD_READ_TE
void te_set_SuspendFlag(int flag)
{
    g_SuspendFlag = flag;
}
void te_set_WatchdogTimer(int mSecs)
{
    mod_timer(&te_watchdog_timer, jiffies + msecs_to_jiffies(mSecs));
}
irqreturn_t te_signal_irq_handler(int irq, void *dev_id)
{
	s_timeoutcount = 0;
	return IRQ_HANDLED;
}

static void te_watchdog_timeout(unsigned long arg)
{
    u32 frame_period = DIV_ROUND_UP(S_TO_MS(1), 60);
    static int checkcount = 0;
    if(g_SuspendFlag == 0)
    {
        if(checkcount < 8)
        {
            checkcount++;
            if(checkcount == 1)
                enable_irq(gpio_to_irq(TEGRA_GPIO_PR6));
            //printk("[LCD] te_watchdog_timeout timeout_count = %d \n",s_timeoutcount);
            if(s_timeoutcount > 4)
            {
                printk("[yushun-1080] reset LCD \n");
                tegra_run_resetwork();
                s_timeoutcount = 0;
                mod_timer(&te_watchdog_timer, jiffies + msecs_to_jiffies(4000));
            }
            else
            {
                s_timeoutcount++;
                mod_timer(&te_watchdog_timer, jiffies + msecs_to_jiffies(20 * frame_period));
            }
        }
        else
        {
            disable_irq(gpio_to_irq(TEGRA_GPIO_PR6));
            checkcount = 0;
            mod_timer(&te_watchdog_timer, jiffies + msecs_to_jiffies(6000));
        }
    }
    else if(g_SuspendFlag == -1)
    {
        printk("[yushun-1080] LCD suspend is error! \n");
    }
}
#endif

static void pluto_panel_select(void)
{
	struct tegra_panel *panel;
	int lcdid;

	lcdid = zte_get_lcd_id();
#ifdef CONFIG_ZTE_LCD_720_1280
	if(lcdid == 0x0)
	{
		printk("+++++++++++++++++++++++++++pluto_panel_select 720P LCD type cmi\n");
		panel = &dsi_cmi_720p_5;
        prop_add(LCD_DEV_NAME,"ic-type","NT35590");
        prop_add(LCD_DEV_NAME,"class-type","cmi/truly");
        prop_add(LCD_DEV_NAME,"resolution","1280*720");
	}
    else if(lcdid == 0x03)
    {
        printk("+++++++++++++++++++++++++++pluto_panel_select 720P LCD type AUO\n");
        panel = &dsi_auo_720p_5;
        prop_add(LCD_DEV_NAME,"ic-type","NT35590");
        prop_add(LCD_DEV_NAME,"class-type","auo/yushun");
        prop_add(LCD_DEV_NAME,"resolution","1280*720");
    }
#else
	if(lcdid == 0x01)
	{
        printk("+++++++++++++++++++++++++++pluto_panel_select 1080P LCD type YUSHUN\n");
		panel = &dsi_yushun_1080p_5;
        prop_add(LCD_DEV_NAME,"ic-type","NT35596");
        prop_add(LCD_DEV_NAME,"class-type","auo/yushun");
        prop_add(LCD_DEV_NAME,"resolution","1920*1080");
	}
#endif
    else
	{
		printk("+++++++++++++++++++++++++++pluto_panel_select LCD type default\n");
	}

	if (panel->init_sd_settings)
		panel->init_sd_settings(&sd_settings);

	if (panel->init_dc_out)
		panel->init_dc_out(&pluto_disp1_out);

	if (panel->init_fb_data)
		panel->init_fb_data(&pluto_disp1_fb_data);

	if (panel->init_cmu_data)
		panel->init_cmu_data(&pluto_disp1_pdata);

	if (panel->set_disp_device)
		panel->set_disp_device(&pluto_disp1_device);

	if (panel->init_resources)
		panel->init_resources(pluto_disp1_resources,
			ARRAY_SIZE(pluto_disp1_resources));

	if (panel->register_bl_dev)
		panel->register_bl_dev();

}
int __init pluto_panel_init(void)
{
	int err = 0;
	struct resource __maybe_unused *res;
	struct platform_device *phost1x;

	sd_settings = pluto_sd_settings;

#ifdef CONFIG_BACKLIGHT_1_WIRE_MODE
      tps61165_pin_init();
#endif

	pluto_panel_select();
    err = gpio_request(pluto_LCD_1V8_EN, "LCD_1V8EN");
    if(err < 0){
        pr_err("[shangzhi]panel LCD 1v8 enable gpio request failed\n");
    }
	gpio_direction_output(pluto_LCD_1V8_EN,1);

#ifdef CONFIG_TEGRA_NVMAP
	pluto_carveouts[1].base = tegra_carveout_start;
	pluto_carveouts[1].size = tegra_carveout_size;
	pluto_carveouts[2].base = tegra_vpr_start;
	pluto_carveouts[2].size = tegra_vpr_size;

	err = platform_device_register(&pluto_nvmap_device);
	if (err) {
		pr_err("nvmap device registration failed\n");
		return err;
	}
#endif
	gpio_request(pluto_hdmi_hpd, "hdmi_hpd");
	gpio_direction_input(pluto_hdmi_hpd);

	phost1x = pluto_host1x_init();
	if (!phost1x) {
		pr_err("host1x devices registration failed\n");
		return -EINVAL;
	}

	res = platform_get_resource_byname(&pluto_disp1_device,
		IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb_start;
	res->end = tegra_fb_start + tegra_fb_size - 1;

	/* Copy the bootloader fb to the fb. */
	__tegra_move_framebuffer(&pluto_nvmap_device,
		tegra_fb_start, tegra_bootloader_fb_start,
			min(tegra_fb_size, tegra_bootloader_fb_size));

	/*
	 * If the bootloader fb2 is valid, copy it to the fb2, or else
	 * clear fb2 to avoid garbage on dispaly2.
	 */
	if (tegra_bootloader_fb2_size)
		tegra_move_framebuffer(tegra_fb2_start,
			tegra_bootloader_fb2_start,
			min(tegra_fb2_size, tegra_bootloader_fb2_size));
	else
		tegra_clear_framebuffer(tegra_fb2_start, tegra_fb2_size);

	res = platform_get_resource_byname(&pluto_disp2_device,
		IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb2_start;
	res->end = tegra_fb2_start + tegra_fb2_size - 1;

	pluto_disp1_device.dev.parent = &phost1x->dev;
	err = platform_device_register(&pluto_disp1_device);
	if (err) {
		pr_err("disp1 device registration failed\n");
		return err;
	}

	pluto_disp2_device.dev.parent = &phost1x->dev;
#if 0
	err = platform_device_register(&pluto_disp2_device);
	if (err) {
		pr_err("disp2 device registration failed\n");
		return err;
	}
#endif
#if CONFIG_ESD_READ_TE
        gpio_request(TEGRA_GPIO_PR6, "TE-irq");
        gpio_direction_input(TEGRA_GPIO_PR6);

        if (request_irq(gpio_to_irq(TEGRA_GPIO_PR6), te_signal_irq_handler,
                    IRQF_TRIGGER_RISING, "TE-Signal", NULL) < 0)
        {
            printk("Failed to request TE IRQ!\n");
        }
        setup_timer(&te_watchdog_timer, te_watchdog_timeout, 0);
        mod_timer(&te_watchdog_timer,  jiffies + msecs_to_jiffies(30000));
#endif

#if defined(CONFIG_LEDS_PWM)	
	err = platform_device_register(&tegra_pwfm0_device);
	if (err) {
		pr_err("led pwm device registration failed");
		return err;
	}
#endif

#ifdef CONFIG_TEGRA_NVAVP
	nvavp_device.dev.parent = &phost1x->dev;
	err = platform_device_register(&nvavp_device);
	if (err) {
		pr_err("nvavp device registration failed\n");
		return err;
	}
#endif
	return err;
}
#else
int __init pluto_panel_init(void)
{
	if (pluto_host1x_init())
		return 0;
	else
		return -EINVAL;
}
#endif
