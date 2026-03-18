# SPDX-License-Identifier: ISC
#
# Makefile fragment for Broadcom 802.11 Networking Device Driver
#
# Copyright (c) 2010 Broadcom Corporation
#
SHELL := /bin/sh
KVER ?= $(if $(KERNELRELEASE),$(KERNELRELEASE),$(shell uname -r))
KSRC ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(KVER)/build)

MODDESTDIR := /lib/modules/$(KVER)/kernel/drivers/net/wireless/broadcom/brcm80211/brcmfmac

ifneq ("$(INSTALL_MOD_PATH)", "")
DEPMOD_ARGS = -b $(INSTALL_MOD_PATH)
else
DEPMOD_ARGS =
endif

#Handle the compression option for modules in 3.18+
ifneq ("","$(wildcard $(MODDESTDIR)/*.ko.gz)")
COMPRESS_GZIP := y
endif
ifneq ("","$(wildcard $(MODDESTDIR)/*.ko.xz)")
COMPRESS_XZ := y
endif
ifneq ("","$(wildcard $(MODDESTDIR)/*.ko.zst)")
COMPRESS_ZSTD := y
endif

ifeq ("","$(wildcard MOK.der)")
NO_SKIP_SIGN := y
endif

ccflags-y += \
	-I $(src) \
	-I $(src)/include \
	-DDEBUG

obj-$(CONFIG_BRCMFMAC) += brcmfmac.o
brcmfmac-objs += \
		cfg80211.o \
		chip.o \
		fwil.o \
		fweh.o \
		p2p.o \
		proto.o \
		common.o \
		core.o \
		firmware.o \
		feature.o \
		btcoex.o \
		vendor.o \
		pno.o \
		xtlv.o
brcmfmac-$(CONFIG_BRCMFMAC_PROTO_BCDC) += \
		bcdc.o \
		fwsignal.o
brcmfmac-$(CONFIG_BRCMFMAC_PROTO_MSGBUF) += \
		commonring.o \
		flowring.o \
		msgbuf.o
brcmfmac-$(CONFIG_BRCMFMAC_SDIO) += \
		sdio.o \
		bcmsdh.o
brcmfmac-$(CONFIG_BRCMFMAC_USB) += \
		usb.o
brcmfmac-$(CONFIG_BRCMFMAC_PCIE) += \
		pcie.o
brcmfmac-$(CONFIG_BRCMDBG) += \
		debug.o
brcmfmac-$(CONFIG_BRCM_TRACING) += \
		tracepoint.o
brcmfmac-$(CONFIG_OF) += \
		of.o
brcmfmac-$(CONFIG_DMI) += \
		dmi.o

all:
	$(MAKE) -j`nproc` -C $(KSRC) M=$$PWD modules

install: all
	@install -D -m 644 -t $(MODDESTDIR) *.ko

ifeq ($(COMPRESS_GZIP), y)
	@gzip -f $(MODDESTDIR)/*.ko
endif
ifeq ($(COMPRESS_XZ), y)
	@xz -f -C crc32 $(MODDESTDIR)/*.ko
endif
ifeq ($(COMPRESS_ZSTD), y)
	@zstd -f -q --rm $(MODDESTDIR)/*.ko
endif

	@depmod $(DEPMOD_ARGS) -a $(KVER)

clean:
	$(MAKE) -C $(KSRC) M=$$PWD clean
	@rm -f MOK.*

sign:
ifeq ($(NO_SKIP_SIGN), y)
	@openssl req -new -x509 -newkey rsa:2048 -keyout MOK.priv -outform DER -out MOK.der -nodes -days 36500 -subj "/CN=Custom MOK/"
	@mokutil --import MOK.der
else
	echo "Skipping key creation"
endif
	@for mod in $(wildcard *.ko); do \
		$(KSRC)/scripts/sign-file sha256 MOK.priv MOK.der $$mod; \
	done

sign-install: all sign install
