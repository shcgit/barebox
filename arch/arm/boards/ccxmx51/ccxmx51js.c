/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <init.h>
#include <gpio.h>
#include <mci.h>
#include <mach/generic.h>
#include <mach/iomux-mx51.h>
#include <mach/devices-imx51.h>

#define CCXMX51JS_USBH1_RESET	IMX_GPIO_NR(3, 8)

static iomux_v3_cfg_t ccxmx51js_pads[] = {
	/* SD1 */
	MX51_PAD_SD1_CLK__SD1_CLK,
	MX51_PAD_SD1_CMD__SD1_CMD,
	MX51_PAD_SD1_DATA0__SD1_DATA0,
//	MX51_PAD_SD1_DATA1__SD1_DATA1,
//	MX51_PAD_SD1_DATA2__SD1_DATA2,
//	MX51_PAD_SD1_DATA3__SD1_DATA3,
};

static struct esdhc_platform_data sdhc1_pdata = {
	.cd_type	= ESDHC_CD_NONE,
	.wp_type	= ESDHC_WP_NONE,
//	.caps		= MMC_CAP_4_BIT_DATA,
};

static int ccxmx51js_init(void)
{
	if (!of_machine_is_compatible("digi,connectcore-ccxmx51-jsk"))
		return -ENOSYS;

	mxc_iomux_v3_setup_multiple_pads(ccxmx51js_pads, ARRAY_SIZE(ccxmx51js_pads));
	imx51_add_mmc0(&sdhc1_pdata);

	gpio_direction_output(CCXMX51JS_USBH1_RESET, 0);
	mdelay(10);
	gpio_set_value(CCXMX51JS_USBH1_RESET, 1);

	return 0;
}
late_initcall(ccxmx51js_init);
