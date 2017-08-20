dtbo-$(CONFIG_MACH_SUN8I) += \
	sun8i-h3-analog-codec.dtbo \
	sun8i-h3-cir.dtbo \
	sun8i-h3-i2c0.dtbo \
	sun8i-h3-i2c1.dtbo \
	sun8i-h3-i2c2.dtbo \
	sun8i-h3-pps-gpio.dtbo \
	sun8i-h3-pwm.dtbo \
	sun8i-h3-spdif-out.dtbo \
	sun8i-h3-spi-add-cs1.dtbo \
	sun8i-h3-spi-jedec-nor.dtbo \
	sun8i-h3-spi-spidev.dtbo \
	sun8i-h3-uart1.dtbo \
	sun8i-h3-uart2.dtbo \
	sun8i-h3-uart3.dtbo \
	sun8i-h3-usbhost0.dtbo \
	sun8i-h3-usbhost2.dtbo \
	sun8i-h3-usbhost3.dtbo \
	sun8i-h3-w1-gpio.dtbo

scr-$(CONFIG_MACH_SUN8I) += sun8i-h3-fixup.scr

dtbotxt-$(CONFIG_MACH_SUN8I) += README.sun8i-h3-overlays

targets += $(dtbo-y) $(scr-y) $(dtbotxt-y)

always		:= $(dtbo-y) $(scr-y) $(dtbotxt-y)
clean-files	:= *.dtbo *.scr
