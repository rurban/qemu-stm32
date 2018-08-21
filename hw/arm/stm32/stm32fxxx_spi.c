/*
 * STM32 Microcontroller RCC (Reset and Clock Control) module
 *
 * Copyright (c) 2018 Martin Schröder <mkschreder.uk@gmail.com>
 *
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/hw.h"
#include "hw/ssi/ssi.h"
#include "hw/arm/stm32fxxx.h"

#define TYPE_STM32FXXX_SPI "stm32fxxx-spi"

#define SPI_TRACE(fmt, ...) qemu_log_mask(LOG_TRACE, "stm32fxxx_spi: " fmt, ##__VA_ARGS__)
#define SPI_ERROR(fmt, ...) qemu_log_mask(LOG_TRACE, "stm32fxxx_spi: ERROR: " fmt, ##__VA_ARGS__)

enum {
    SPI_REG_CR1 = 0,
    SPI_REG_CR2,
    SPI_REG_SR,
    SPI_REG_DR,
    SPI_REG_CRCPR,
    SPI_REG_RXCRCR,
    SPI_REG_TXCRCR,
    SPI_REG_I2SCFGR,
    SPI_REG_I2SPR
};

enum {
    SPI_CR1_BIDIMODE    = (1 << 15),
    SPI_CR1_BIDIOE      = (1 << 14),
    SPI_CR1_CRCEN       = (1 << 13),
    SPI_CR1_CRCNEXT     = (1 << 12),
    SPI_CR1_DFF         = (1 << 11),
    SPI_CR1_RXONLY      = (1 << 10),
    SPI_CR1_SSM         = (1 << 9),
    SPI_CR1_SSI         = (1 << 8),
    SPI_CR1_LSBFIRST    = (1 << 7),
    SPI_CR1_SPE         = (1 << 6),
    SPI_CR1_BR          = (7 << 3),
    SPI_CR1_MSTR        = (1 << 2),
    SPI_CR1_CPOL        = (1 << 1),
    SPI_CR1_CPHA        = (1 << 0)
};

enum {
    SPI_CR2_TXEIE       = (1 << 7),
    SPI_CR2_RXNEIE      = (1 << 6),
    SPI_CR2_ERRIE       = (1 << 5),
    SPI_CR2_FRF         = (1 << 4),
    SPI_CR2_SSOE        = (1 << 2),
    SPI_CR2_TXDMAEN     = (1 << 1),
    SPI_CR2_RXDMAEN     = (1 << 0)
};

enum {
    SPI_SR_FRE          = (1 << 8),
    SPI_SR_BSY          = (1 << 7),
    SPI_SR_OVR          = (1 << 6),
    SPI_SR_MODF         = (1 << 5),
    SPI_SR_CRCERR       = (1 << 4),
    SPI_SR_UDR          = (1 << 3),
    SPI_SR_CHSIDE       = (1 << 2),
    SPI_SR_TXE          = (1 << 1),
    SPI_SR_RXNE         = (1 << 0)
};

struct stm32fxxx_spi {
    SysBusDevice parent;

    MemoryRegion mmio;

    struct stm32fxxx_spi_regs *regs;
    uint8_t device_id;

    qemu_irq irq;
    SSIBus *ssi;
};

static uint64_t stm32fxxx_spi_read(void *opaque, hwaddr addr, unsigned int size){
    struct stm32fxxx_spi *self = (struct stm32fxxx_spi*)opaque;

    switch(addr){
        case SPI_REG_CR1: {
            return self->regs->CR1;
        } break;
        case SPI_REG_CR2: {
            return self->regs->CR2;
        } break;
        case SPI_REG_SR: {
            return self->regs->SR;
        } break;
        case SPI_REG_DR: {
            return self->regs->DR;
        } break;
        case SPI_REG_CRCPR: {
            return self->regs->CRCPR;
        } break;
        case SPI_REG_RXCRCR: {
            return self->regs->RXCRCR;
        } break;
        case SPI_REG_TXCRCR: {
            return self->regs->TXCRCR;
        } break;
        case SPI_REG_I2SCFGR: {
            return self->regs->I2SCFGR;
        } break;
        case SPI_REG_I2SPR: {
            return self->regs->I2SPR;
        } break;
    }
    return 0;
}

static void stm32fxxx_spi_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size){
    struct stm32fxxx_spi *self = (struct stm32fxxx_spi*)opaque;
    uint16_t val = val64;

    static const struct {
        int bit;
        const char *dset;
        const char *dreset;
    } cr1_info[] = {
        { SPI_CR1_BIDIMODE, "bidirectional mode enabled", "bidirectional mode disabled" },
        { SPI_CR1_BIDIOE, "bidirectional mode: output enabled", "bidirectional mode: output disabled" },
        { SPI_CR1_CRCEN, "crc: enabled", "crc: disabled" },
        { SPI_CR1_CRCNEXT, "next transfer set to type CRC", "next transfer set to type DATA" },
        { SPI_CR1_DFF, "using 16-bit data format", "using 8-bit data format" },
        { SPI_CR1_RXONLY, "spi output disabled (rxonly)", "full duplex operation: enabled" },
        { SPI_CR1_SSM, "NSS pin controlled by software", "NSS pin controlled by hardware" },
        { SPI_CR1_SSI, "SSI bit set", "SSI bit reset" },
        { SPI_CR1_LSBFIRST, "mode: LSB first", "mode: MSB first" },
        { SPI_CR1_SPE, "enabled", "disabled" },
        { SPI_CR1_MSTR, "master mode", "slave mode" },
        { SPI_CR1_CPOL, "CK to 1 when idle", "CK to 0 when idle" },
        { SPI_CR1_CPHA, "first data capture edge on first clock transmission", "first data capture edge on second clock transition" }
    };

    if(size != 2){
        SPI_ERROR("writes of %d bytes not supported\n", size);
        return;
    }

    switch(addr){
        case SPI_REG_CR1: {
            uint16_t valx = val ^ self->regs->CR1;
            for(size_t c = 0; c < (sizeof(cr1_info) / sizeof(cr1_info[0])); c++){
                if(valx & cr1_info[c].bit){
                    if(val & cr1_info[c].bit) {
                        SPI_TRACE("spi%d: %s\n", self->device_id + 1, cr1_info[c].dset);
                    } else {
                        SPI_TRACE("spi%d: %s\n", self->device_id + 1, cr1_info[c].dreset);
                    }
                }
            }
            if(valx & SPI_CR1_BR){
                uint8_t baud = (val >> 3) & 7;
                SPI_TRACE("spi%d: baud rate set to fPCLK/%d\n", self->device_id +1, 1 << (baud + 1));
            }
            self->regs->CR1 = val;
        } break;
        case SPI_REG_CR2: {
            self->regs->CR2 = val;
        } break;
        case SPI_REG_SR: {
            self->regs->SR = val;
        } break;
        case SPI_REG_DR: {
            self->regs->DR = val;
            self->regs->SR |= SPI_SR_RXNE;
            self->regs->SR |= SPI_SR_TXE;
            //qemu_set_irq(self->irq, 0);
        } break;
        case SPI_REG_CRCPR: {
            self->regs->CRCPR = val;
        } break;
        case SPI_REG_RXCRCR: {
            self->regs->RXCRCR = val;
        } break;
        case SPI_REG_TXCRCR: {
            self->regs->TXCRCR = val;
        } break;
        case SPI_REG_I2SCFGR: {
            self->regs->I2SCFGR = val;
        } break;
        case SPI_REG_I2SPR: {
            self->regs->I2SPR = val;
        } break;
    }
}

static const MemoryRegionOps stm32fxxx_spi_ops = {
    .read = stm32fxxx_spi_read,
    .write = stm32fxxx_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void stm32fxxx_spi_init(Object *obj){
    struct stm32fxxx_spi *self = OBJECT_CHECK(struct stm32fxxx_spi, obj, TYPE_STM32FXXX_SPI);
    memory_region_init_io(&self->mmio, obj, &stm32fxxx_spi_ops, self, TYPE_STM32FXXX_SPI, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &self->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &self->irq);
    self->ssi = ssi_create_bus(DEVICE(obj), "ssi");
}

static void stm32fxxx_spi_realize(DeviceState *dev, Error **errp){
    struct stm32fxxx_spi *self = OBJECT_CHECK(struct stm32fxxx_spi, dev, TYPE_STM32FXXX_SPI);
    if(!self->regs){
        fprintf(stderr, "SPI must have .regs property set!\n");
        exit(1);
    }
}

static void stm32fxxx_spi_reset(DeviceState *dev){
    struct stm32fxxx_spi *self = OBJECT_CHECK(struct stm32fxxx_spi, dev, "stm32fxxx-spi");
    self->regs->CR1 = 0;
    self->regs->CR2 = 0;
    self->regs->SR = 0x2;
    self->regs->DR = 0x0;
    self->regs->CRCPR = 0x7;
    self->regs->RXCRCR = 0;
    self->regs->TXCRCR = 0;
    self->regs->I2SCFGR = 0;
    self->regs->I2SPR = 0x2;
}

static Property stm32fxxx_spi_properties[] = {
    DEFINE_PROP("regs", struct stm32fxxx_spi, regs, qdev_prop_ptr, struct stm32fxxx_spi_regs*),
    DEFINE_PROP_UINT8("device_id", struct stm32fxxx_spi, device_id, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32fxxx_spi_class_init(ObjectClass *klass, void *data){
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = stm32fxxx_spi_reset;
    dc->props = stm32fxxx_spi_properties;
    dc->realize = stm32fxxx_spi_realize;
}

static const TypeInfo stm32fxxx_spi_info = {
    .name          = TYPE_STM32FXXX_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32fxxx_spi),
    .instance_init = stm32fxxx_spi_init,
    .class_init    = stm32fxxx_spi_class_init
};

static void stm32fxxx_spi_register_types(void){
    type_register_static(&stm32fxxx_spi_info);
}

type_init(stm32fxxx_spi_register_types)

