obj-$(CONFIG_44x)	:= misc_44x.o
obj-$(CONFIG_EBONY)	+= ebony.o
obj-$(CONFIG_BAMBOO)	+= bamboo.o
obj-$(CONFIG_SEQUOIA)	+= sequoia.o

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_440EPX)   += ppc4xx-pci.o
endif

ifeq ($(CONFIG_USB_SUPPORT),y)
obj-$(CONFIG_440EPX)   += ppc4xx-usb.o
endif

ifeq ($(CONFIG_MTD_NAND_NDFC),y)
obj-y+= ppc4xx-nand.o
endif
