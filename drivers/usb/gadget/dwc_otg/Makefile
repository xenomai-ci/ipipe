#
# Makefile for DWC_otg Highspeed USB controller driver
#

# DFX TODO need KConfig defines to set the CCPFLAGS
CPPFLAGS	+= -DDEBUG

CPPFLAGS        += -DDWC_DEVICE_ONLY

# DFX TODO what is the -Dlinux actuall for?
#CPPFLAGS	+= -Dlinux -DDWC_HS_ELECT_TST original line
CPPFLAGS	+= -Dlinux

obj-$(CONFIG_USB_DWC_OTG)	+= dwc_otg.o

dwc_otg-objs	:= dwc_otg_driver.o dwc_otg_attr.o
dwc_otg-objs	+= dwc_otg_cil.o dwc_otg_cil_intr.o
dwc_otg-objs	+= dwc_otg_pcd.o dwc_otg_pcd_intr.o
dwc_otg-objs	+= dwc_otg_hcd.o dwc_otg_hcd_intr.o dwc_otg_hcd_queue.o

