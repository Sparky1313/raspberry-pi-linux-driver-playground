# About

This directory has datasheets and specs on the Raspberry Pi 3B.

The Raspberry Pi 3B uses a Broadcom BCM2837 chip, but [bcm2835-peripherals.pdf](bcm2835-peripherals.pdf) actually contains all of the information needed about peripherals. There is one critical exception though, the base address for peripherals has changed from 0x20000000 for the BCM2835 to 0x3F000000 for the BCM2837. This changes the address of all peripherals.