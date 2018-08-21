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

#define TYPE_STM32FXXX_FMC "stm32fxxx-fmc"

struct stm32fxxx_fmc {
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;
};

static uint64_t stm32fxxx_fmc_read(void *opaque, hwaddr addr, unsigned int size){
    printf("FMC read %08x\n", (int)addr);
    return 0;
}

static void stm32fxxx_fmc_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size){
    printf("FMC write %08x\n", (int)addr);
}

static const MemoryRegionOps stm32fxxx_fmc_ops = {
    .read = stm32fxxx_fmc_read,
    .write = stm32fxxx_fmc_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void stm32fxxx_fmc_init(Object *obj){
    struct stm32fxxx_fmc *self = OBJECT_CHECK(struct stm32fxxx_fmc, obj, TYPE_STM32FXXX_FMC);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &self->irq);
    memory_region_init_io(&self->mmio, obj, &stm32fxxx_fmc_ops, self, TYPE_STM32FXXX_FMC, 0x158);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &self->mmio);
}

static void stm32fxxx_fmc_realize(DeviceState *dev, Error **errp){
    //struct stm32fxxx_fmc *self = STM32FXXX_FMC(dev);

}

static void stm32fxxx_fmc_reset(DeviceState *dev){

}

static Property stm32fxxx_fmc_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

static void stm32fxxx_fmc_class_init(ObjectClass *klass, void *data){
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = stm32fxxx_fmc_reset;
    dc->props = stm32fxxx_fmc_properties;
    dc->realize = stm32fxxx_fmc_realize;
}

static const TypeInfo stm32fxxx_fmc_info = {
    .name          = TYPE_STM32FXXX_FMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32fxxx_fmc),
    .instance_init = stm32fxxx_fmc_init,
    .class_init    = stm32fxxx_fmc_class_init
};

static void stm32fxxx_fmc_register_types(void){
    type_register_static(&stm32fxxx_fmc_info);
}

type_init(stm32fxxx_fmc_register_types)

