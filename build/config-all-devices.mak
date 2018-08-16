CONFIG_A15MPCORE:=$(findstring y,$(CONFIG_A15MPCORE)y)
CONFIG_A9_GTIMER:=$(findstring y,$(CONFIG_A9_GTIMER)y)
CONFIG_A9MPCORE:=$(findstring y,$(CONFIG_A9MPCORE)y)
CONFIG_A9SCU:=$(findstring y,$(CONFIG_A9SCU)y)
CONFIG_AC97:=$(findstring y,$(CONFIG_AC97)y)
CONFIG_ACPI:=$(findstring y,$(CONFIG_ACPI)y)
CONFIG_ADS7846:=$(findstring y,$(CONFIG_ADS7846)y)
CONFIG_AHCI:=$(findstring y,$(CONFIG_AHCI)y)
CONFIG_ALLWINNER_A10:=$(findstring y,$(CONFIG_ALLWINNER_A10)y)
CONFIG_ALLWINNER_A10_PIC:=$(findstring y,$(CONFIG_ALLWINNER_A10_PIC)y)
CONFIG_ALLWINNER_A10_PIT:=$(findstring y,$(CONFIG_ALLWINNER_A10_PIT)y)
CONFIG_ALLWINNER_EMAC:=$(findstring y,$(CONFIG_ALLWINNER_EMAC)y)
CONFIG_ARM11MPCORE:=$(findstring y,$(CONFIG_ARM11MPCORE)y)
CONFIG_ARM11SCU:=$(findstring y,$(CONFIG_ARM11SCU)y)
CONFIG_ARM_GIC:=$(findstring y,$(CONFIG_ARM_GIC)y)
CONFIG_ARM_GIC_KVM:=$(findstring y,$(CONFIG_ARM_GIC_KVM)$(CONFIG_KVM))
CONFIG_ARM_MPTIMER:=$(findstring y,$(CONFIG_ARM_MPTIMER)y)
CONFIG_ARM_TIMER:=$(findstring y,$(CONFIG_ARM_TIMER)y)
CONFIG_ARM_V7M:=$(findstring y,$(CONFIG_ARM_V7M)y)
CONFIG_ASPEED_SOC:=$(findstring y,$(CONFIG_ASPEED_SOC)y)
CONFIG_BITBANG_I2C:=$(findstring y,$(CONFIG_BITBANG_I2C)y)
CONFIG_BLIZZARD:=$(findstring y,$(CONFIG_BLIZZARD)y)
CONFIG_CADENCE:=$(findstring y,$(CONFIG_CADENCE)y)
CONFIG_DIGIC:=$(findstring y,$(CONFIG_DIGIC)y)
CONFIG_DS1338:=$(findstring y,$(CONFIG_DS1338)y)
CONFIG_E1000E_PCI:=$(findstring y,$(CONFIG_E1000E_PCI)y)
CONFIG_E1000_PCI:=$(findstring y,$(CONFIG_E1000_PCI)y)
CONFIG_ECC:=$(findstring y,$(CONFIG_ECC)y)
CONFIG_EDU:=$(findstring y,$(CONFIG_EDU)y)
CONFIG_EEPRO100_PCI:=$(findstring y,$(CONFIG_EEPRO100_PCI)y)
CONFIG_ES1370:=$(findstring y,$(CONFIG_ES1370)y)
CONFIG_ESP:=$(findstring y,$(CONFIG_ESP)y)
CONFIG_ESP_PCI:=$(findstring y,$(CONFIG_ESP_PCI)y)
CONFIG_EXYNOS4:=$(findstring y,$(CONFIG_EXYNOS4)y)
CONFIG_FRAMEBUFFER:=$(findstring y,$(CONFIG_FRAMEBUFFER)y)
CONFIG_FSL_IMX25:=$(findstring y,$(CONFIG_FSL_IMX25)y)
CONFIG_FSL_IMX31:=$(findstring y,$(CONFIG_FSL_IMX31)y)
CONFIG_FSL_IMX6:=$(findstring y,$(CONFIG_FSL_IMX6)y)
CONFIG_GPIO_KEY:=$(findstring y,$(CONFIG_GPIO_KEY)y)
CONFIG_HDA:=$(findstring y,$(CONFIG_HDA)y)
CONFIG_I82801B11:=$(findstring y,$(CONFIG_I82801B11)y)
CONFIG_IDE_CORE:=$(findstring y,$(CONFIG_IDE_CORE)y)
CONFIG_IDE_PCI:=$(findstring y,$(CONFIG_IDE_PCI)y)
CONFIG_IDE_QDEV:=$(findstring y,$(CONFIG_IDE_QDEV)y)
CONFIG_IMX_FEC:=$(findstring y,$(CONFIG_IMX_FEC)y)
CONFIG_IMX:=$(findstring y,$(CONFIG_IMX)y)
CONFIG_IMX_I2C:=$(findstring y,$(CONFIG_IMX_I2C)y)
CONFIG_INTEGRATOR_DEBUG:=$(findstring y,$(CONFIG_INTEGRATOR_DEBUG)y)
CONFIG_IOH3420:=$(findstring y,$(CONFIG_IOH3420)y)
CONFIG_IPACK:=$(findstring y,$(CONFIG_IPACK)y)
CONFIG_ISA_BUS:=$(findstring y,$(CONFIG_ISA_BUS)y)
CONFIG_IVSHMEM:=$(findstring y,$(CONFIG_IVSHMEM)$(CONFIG_EVENTFD))
CONFIG_LAN9118:=$(findstring y,$(CONFIG_LAN9118)y)
CONFIG_LM832X:=$(findstring y,$(CONFIG_LM832X)y)
CONFIG_LSI_SCSI_PCI:=$(findstring y,$(CONFIG_LSI_SCSI_PCI)y)
CONFIG_MAINSTONE:=$(findstring y,$(CONFIG_MAINSTONE)y)
CONFIG_MARVELL_88W8618:=$(findstring y,$(CONFIG_MARVELL_88W8618)y)
CONFIG_MAX111X:=$(findstring y,$(CONFIG_MAX111X)y)
CONFIG_MAX7310:=$(findstring y,$(CONFIG_MAX7310)y)
CONFIG_MEGASAS_SCSI_PCI:=$(findstring y,$(CONFIG_MEGASAS_SCSI_PCI)y)
CONFIG_MICRODRIVE:=$(findstring y,$(CONFIG_MICRODRIVE)y)
CONFIG_MPTSAS_SCSI_PCI:=$(findstring y,$(CONFIG_MPTSAS_SCSI_PCI)y)
CONFIG_NAND:=$(findstring y,$(CONFIG_NAND)y)
CONFIG_NE2000_PCI:=$(findstring y,$(CONFIG_NE2000_PCI)y)
CONFIG_NSERIES:=$(findstring y,$(CONFIG_NSERIES)y)
CONFIG_NVME_PCI:=$(findstring y,$(CONFIG_NVME_PCI)y)
CONFIG_OMAP:=$(findstring y,$(CONFIG_OMAP)y)
CONFIG_ONENAND:=$(findstring y,$(CONFIG_ONENAND)y)
CONFIG_PCIE_PORT:=$(findstring y,$(CONFIG_PCIE_PORT)y)
CONFIG_PCI:=$(findstring y,$(CONFIG_PCI)y)
CONFIG_PCI_GENERIC:=$(findstring y,$(CONFIG_PCI_GENERIC)y)
CONFIG_PCI_TESTDEV:=$(findstring y,$(CONFIG_PCI_TESTDEV)y)
CONFIG_PCNET_COMMON:=$(findstring y,$(CONFIG_PCNET_COMMON)y)
CONFIG_PCNET_PCI:=$(findstring y,$(CONFIG_PCNET_PCI)y)
CONFIG_PFLASH_CFI01:=$(findstring y,$(CONFIG_PFLASH_CFI01)y)
CONFIG_PFLASH_CFI02:=$(findstring y,$(CONFIG_PFLASH_CFI02)y)
CONFIG_PL011:=$(findstring y,$(CONFIG_PL011)y)
CONFIG_PL022:=$(findstring y,$(CONFIG_PL022)y)
CONFIG_PL031:=$(findstring y,$(CONFIG_PL031)y)
CONFIG_PL041:=$(findstring y,$(CONFIG_PL041)y)
CONFIG_PL050:=$(findstring y,$(CONFIG_PL050)y)
CONFIG_PL061:=$(findstring y,$(CONFIG_PL061)y)
CONFIG_PL080:=$(findstring y,$(CONFIG_PL080)y)
CONFIG_PL110:=$(findstring y,$(CONFIG_PL110)y)
CONFIG_PL181:=$(findstring y,$(CONFIG_PL181)y)
CONFIG_PL190:=$(findstring y,$(CONFIG_PL190)y)
CONFIG_PL310:=$(findstring y,$(CONFIG_PL310)y)
CONFIG_PL330:=$(findstring y,$(CONFIG_PL330)y)
CONFIG_PLATFORM_BUS:=$(findstring y,$(CONFIG_PLATFORM_BUS)y)
CONFIG_PTIMER:=$(findstring y,$(CONFIG_PTIMER)y)
CONFIG_PXA2XX:=$(findstring y,$(CONFIG_PXA2XX)y)
CONFIG_RASPI:=$(findstring y,$(CONFIG_RASPI)y)
CONFIG_REALVIEW:=$(findstring y,$(CONFIG_REALVIEW)y)
CONFIG_ROCKER:=$(findstring y,$(CONFIG_ROCKER)y)
CONFIG_RTL8139_PCI:=$(findstring y,$(CONFIG_RTL8139_PCI)y)
CONFIG_SD:=$(findstring y,$(CONFIG_SD)y)
CONFIG_SDHCI:=$(findstring y,$(CONFIG_SDHCI)y)
CONFIG_SERIAL:=$(findstring y,$(CONFIG_SERIAL)y)
CONFIG_SERIAL_ISA:=$(findstring y,$(CONFIG_SERIAL_ISA)y)
CONFIG_SERIAL_PCI:=$(findstring y,$(CONFIG_SERIAL_PCI)y)
CONFIG_SMBIOS:=$(findstring y,$(CONFIG_SMBIOS)y)
CONFIG_SMC91C111:=$(findstring y,$(CONFIG_SMC91C111)y)
CONFIG_SSD0303:=$(findstring y,$(CONFIG_SSD0303)y)
CONFIG_SSD0323:=$(findstring y,$(CONFIG_SSD0323)y)
CONFIG_SSI:=$(findstring y,$(CONFIG_SSI)y)
CONFIG_SSI_M25P80:=$(findstring y,$(CONFIG_SSI_M25P80)y)
CONFIG_SSI_SD:=$(findstring y,$(CONFIG_SSI_SD)y)
CONFIG_STELLARIS_ENET:=$(findstring y,$(CONFIG_STELLARIS_ENET)y)
CONFIG_STELLARIS:=$(findstring y,$(CONFIG_STELLARIS)y)
CONFIG_STELLARIS_INPUT:=$(findstring y,$(CONFIG_STELLARIS_INPUT)y)
CONFIG_STM32F205_SOC:=$(findstring y,$(CONFIG_STM32F205_SOC)y)
CONFIG_STM32F2XX_ADC:=$(findstring y,$(CONFIG_STM32F2XX_ADC)y)
CONFIG_STM32F2XX_SPI:=$(findstring y,$(CONFIG_STM32F2XX_SPI)y)
CONFIG_STM32F2XX_SYSCFG:=$(findstring y,$(CONFIG_STM32F2XX_SYSCFG)y)
CONFIG_STM32F2XX_TIMER:=$(findstring y,$(CONFIG_STM32F2XX_TIMER)y)
CONFIG_STM32F2XX_USART:=$(findstring y,$(CONFIG_STM32F2XX_USART)y)
CONFIG_STM32:=$(findstring y,$(CONFIG_STM32)y)
CONFIG_TMP105:=$(findstring y,$(CONFIG_TMP105)y)
CONFIG_TSC2005:=$(findstring y,$(CONFIG_TSC2005)y)
CONFIG_TSC210X:=$(findstring y,$(CONFIG_TSC210X)y)
CONFIG_TUSB6010:=$(findstring y,$(CONFIG_TUSB6010)y)
CONFIG_TWL92230:=$(findstring y,$(CONFIG_TWL92230)y)
CONFIG_USB_AUDIO:=$(findstring y,$(CONFIG_USB_AUDIO)y)
CONFIG_USB_BLUETOOTH:=$(findstring y,$(CONFIG_USB_BLUETOOTH)y)
CONFIG_USB_EHCI:=$(findstring y,$(CONFIG_USB_EHCI)y)
CONFIG_USB_EHCI_SYSBUS:=$(findstring y,$(CONFIG_USB_EHCI_SYSBUS)y)
CONFIG_USB:=$(findstring y,$(CONFIG_USB)y)
CONFIG_USB_MUSB:=$(findstring y,$(CONFIG_USB_MUSB)y)
CONFIG_USB_NETWORK:=$(findstring y,$(CONFIG_USB_NETWORK)y)
CONFIG_USB_OHCI:=$(findstring y,$(CONFIG_USB_OHCI)y)
CONFIG_USB_SERIAL:=$(findstring y,$(CONFIG_USB_SERIAL)y)
CONFIG_USB_SMARTCARD:=$(findstring y,$(CONFIG_USB_SMARTCARD)y)
CONFIG_USB_STORAGE_BOT:=$(findstring y,$(CONFIG_USB_STORAGE_BOT)y)
CONFIG_USB_STORAGE_MTP:=$(findstring y,$(CONFIG_USB_STORAGE_MTP)y)
CONFIG_USB_STORAGE_UAS:=$(findstring y,$(CONFIG_USB_STORAGE_UAS)y)
CONFIG_USB_TABLET_WACOM:=$(findstring y,$(CONFIG_USB_TABLET_WACOM)y)
CONFIG_USB_UHCI:=$(findstring y,$(CONFIG_USB_UHCI)y)
CONFIG_USB_XHCI:=$(findstring y,$(CONFIG_USB_XHCI)y)
CONFIG_VERSATILE_I2C:=$(findstring y,$(CONFIG_VERSATILE_I2C)y)
CONFIG_VERSATILE_PCI:=$(findstring y,$(CONFIG_VERSATILE_PCI)y)
CONFIG_VFIO_AMD_XGBE:=$(findstring y,$(CONFIG_VFIO_AMD_XGBE)y)
CONFIG_VFIO_XGMAC:=$(findstring y,$(CONFIG_VFIO_XGMAC)y)
CONFIG_VGA:=$(findstring y,$(CONFIG_VGA)y)
CONFIG_VGA_PCI:=$(findstring y,$(CONFIG_VGA_PCI)y)
CONFIG_VIRTIO:=$(findstring y,$(CONFIG_VIRTIO)y)
CONFIG_VIRTIO_PCI:=$(findstring y,$(CONFIG_VIRTIO_PCI)y)
CONFIG_VMW_PVSCSI_SCSI_PCI:=$(findstring y,$(CONFIG_VMW_PVSCSI_SCSI_PCI)y)
CONFIG_VMXNET3_PCI:=$(findstring y,$(CONFIG_VMXNET3_PCI)y)
CONFIG_WDT_IB6300ESB:=$(findstring y,$(CONFIG_WDT_IB6300ESB)y)
CONFIG_WM8750:=$(findstring y,$(CONFIG_WM8750)y)
CONFIG_XGMAC:=$(findstring y,$(CONFIG_XGMAC)y)
CONFIG_XILINX_SPIPS:=$(findstring y,$(CONFIG_XILINX_SPIPS)y)
CONFIG_XIO3130:=$(findstring y,$(CONFIG_XIO3130)y)
CONFIG_ZAURUS:=$(findstring y,$(CONFIG_ZAURUS)y)
CONFIG_ZYNQ_DEVCFG:=$(findstring y,$(CONFIG_ZYNQ_DEVCFG)y)
CONFIG_ZYNQ:=$(findstring y,$(CONFIG_ZYNQ)y)
