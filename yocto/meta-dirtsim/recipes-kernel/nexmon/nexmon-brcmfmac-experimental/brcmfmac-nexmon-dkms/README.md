Nexmon's brcmfmac driver, dkmsified.

Why?
The Kali RaspberryPi images have for a very long time been stuck on older kernels due to the fact that we would patch in the nexmon driver, and that has held back a number of advancements.  Ideally we would keep in step with the RaspberryPi kernels, and this is a step in that direction.  By having the driver as a module installable via dkms, we can upgrade the kernels when needed and not have to keep our own fork that is really old.  This also allows for driver improvements without requiring kernel builds and kernel testing.

This is the [Seemoo Lab Nexmon Project](https://github.com/seemoo-lab/nexmon)'s brcmfmac driver for RaspberryPi Foundation's 6.6 kernel taken from commit ef25ce36700a and then lightly modified (Makefile edited, dkms.conf added and a small patch to common.c to add a MODULE_VERSION otherwise dkms will refuse to install the driver.)

NOTE: This is ONLY the driver, and you do still need the custom firmware which is NOT part of this package.

--
