#
# Makefile for 52xx based boards
#
ifeq ($(CONFIG_PPC_MERGE),y)
obj-y				+= mpc52xx_pic.o mpc52xx_common.o
obj-$(CONFIG_PCI)		+= mpc52xx_pci.o
obj-$(CONFIG_PPC_BESTCOMM) 	+= bestcomm.o
obj-$(CONFIG_FEC_MPC52xx)	+= sdma_fec_rx_task.o sdma_fec_tx_task.o fec.o
obj-$(CONFIG_MPC52xx_AIC26)	+= sdma_gen_bd_rx_task.o \
				   sdma_gen_bd_tx_task.o gen_bd.o
endif

obj-$(CONFIG_PPC_EFIKA)		+= efika-setup.o efika-pci.o
obj-$(CONFIG_PPC_LITE5200)	+= lite5200.o
