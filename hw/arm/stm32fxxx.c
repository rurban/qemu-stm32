/*
 * STM32F4xx SoC
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 * Copyright (c) 2018 Martin Schröder <mkschreder.uk@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"
#include "hw/arm/arm.h"
#include "hw/arm/armv7m.h"
#include "cpu.h"

#define FLASH_BASE_ADDRESS 0x08000000
#define FLASH_SIZE (2 * 1024 * 1024)
#define SRAM_BASE_ADDRESS 0x20000000
#define SRAM_SIZE (176 * 1024)

#include "hw/misc/stm32f2xx_syscfg.h"
#include "hw/timer/stm32f2xx_timer.h"
#include "hw/adc/stm32f2xx_adc.h"
#include "hw/ssi/stm32f2xx_spi.h"
#include "hw/or-irq.h"
#include "hw/arm/arm.h"
#include "hw/arm/armv7m.h"

#define STM32F429_439xx
#include "stm32f4xx.h"

#define TYPE_STM32F4XX_SOC "stm32f4xx-soc"
#define STM32F4XX_SOC(obj) \
    OBJECT_CHECK(struct stm32f4xx_soc, (obj), TYPE_STM32F4XX_SOC)

#define STM32F4XX_NUM_UARTS 6
#define STM32F4XX_NUM_TIMERS 4
#define STM32F4XX_NUM_ADCS 3
#define STM32F4XX_NUM_SPIS 3

#define NAME_SIZE 20

struct stm32f4xx_soc {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ARMv7MState armv7m;

    SysBusDevice *syscfg;

    SysBusDevice *usart[STM32F4XX_NUM_UARTS];
    SysBusDevice *tim[STM32F4XX_NUM_TIMERS];
    SysBusDevice *adc[STM32F4XX_NUM_ADCS];
    SysBusDevice *spi[STM32F4XX_NUM_SPIS];
    SysBusDevice *rcc;

    qemu_or_irq *adc_irqs;

    char *cpu_type;
    MemoryRegion mmio;
};

static void stm32f4xx_rogue_mem_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size) {
    printf("Rogue mem write to %08x\n", (uint32_t)addr);
}

static uint64_t stm32f4xx_rogue_mem_read(void *opaque, hwaddr addr,
                                       unsigned int size) {
    printf("Rogue mem read from %08x\n", (uint32_t)addr);
    return 0;
}

static const MemoryRegionOps _stm32_rogue_mem_ops = {
    .read = stm32f4xx_rogue_mem_read,
    .write = stm32f4xx_rogue_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int stm32_realize_peripheral(ARMv7MState *cpu, SysBusDevice *dev, hwaddr base, unsigned int irqnr, Error **errp){
    Error *err = NULL;

    object_property_set_bool(OBJECT(dev), true, "realized", &err);

    if (err != NULL) {
        error_propagate(errp, err);
        return -1;
    }

    sysbus_mmio_map(dev, 0, base);
    sysbus_connect_irq(dev, 0, qdev_get_gpio_in(DEVICE(cpu), irqnr));

    return 0;
}

static void stm32f4xx_soc_initfn(Object *obj){
    struct stm32f4xx_soc *s = STM32F4XX_SOC(obj);
    int i;
    char name[NAME_SIZE];

    // add memory handler that will catch all access outside of valid range
    memory_region_init_io(&s->mmio, obj, &_stm32_rogue_mem_ops, s,
                          "stm32f4xx-soc", 0xffffffff);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    object_initialize(&s->armv7m, sizeof(s->armv7m), TYPE_ARMV7M);
    qdev_set_parent_bus(DEVICE(&s->armv7m), sysbus_get_default());

    s->syscfg = sysbus_create_child_obj(obj, name, "stm32f2xx-syscfg");

    DeviceState *rcc = qdev_create(NULL, "stm32f1xx_rcc");
    qdev_prop_set_uint32(rcc, "osc_freq", 8000000);
    qdev_prop_set_uint32(rcc, "osc32_freq", 32000);
    qdev_init_nofail(rcc);
    object_property_add_child(obj, "rcc", OBJECT(rcc), NULL);
    s->rcc = SYS_BUS_DEVICE(rcc);

    for (i = 0; i < STM32F4XX_NUM_UARTS; i++) {
        snprintf(name, NAME_SIZE, "usart[%d]", i);
        s->usart[i] = sysbus_create_child_obj(obj, name, "stm32f1xx-usart");
    }

    for (i = 0; i < STM32F4XX_NUM_TIMERS; i++) {
        snprintf(name, NAME_SIZE, "tim[%d]", i);
        s->tim[i] = sysbus_create_child_obj(obj, name, "stm32f2xx-timer");
    }

    s->adc_irqs = OR_IRQ(object_new(TYPE_OR_IRQ));

    for (i = 0; i < STM32F4XX_NUM_ADCS; i++) {
        snprintf(name, NAME_SIZE, "adc[%d]", i);
        s->adc[i] = sysbus_create_child_obj(obj, name, "stm32f2xx-adc");
    }

    for (i = 0; i < STM32F4XX_NUM_SPIS; i++) {
        snprintf(name, NAME_SIZE, "spi[%d]", i);
        s->spi[i] = sysbus_create_child_obj(obj, name, "stm32f2xx-spi");
    }
}

static void stm32f4xx_soc_realize(DeviceState *dev_soc, Error **errp) {
    struct stm32f4xx_soc *s = STM32F4XX_SOC(dev_soc);
    Error *err = NULL;
    int i;

    DeviceState *armv7m = DEVICE(&s->armv7m);

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *flash_alias = g_new(MemoryRegion, 1);

    memory_region_init_ram(flash, NULL, "STM32F4xx.flash", FLASH_SIZE, &error_fatal);
    memory_region_init_alias(flash_alias, NULL, "STM32F4xx.flash.alias", flash, 0, FLASH_SIZE);

    vmstate_register_ram_global(flash);

    memory_region_set_readonly(flash, true);
    memory_region_set_readonly(flash_alias, true);

    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, flash);
    memory_region_add_subregion(system_memory, 0, flash_alias);

    memory_region_init_ram(sram, NULL, "STM32F4xx.sram", SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, sram);

    memory_region_add_subregion_overlap(system_memory, 0, &s->mmio, -1);

	// init the cpu on the soc
	qdev_prop_set_uint32(armv7m, "num-irq", 96);
    qdev_prop_set_string(armv7m, "cpu-type", s->cpu_type);
    object_property_set_link(OBJECT(&s->armv7m), OBJECT(get_system_memory()),
                                     "memory", &error_abort);

    object_property_set_bool(OBJECT(&s->armv7m), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    // map peripherals into memory of the cpu
    if(stm32_realize_peripheral(&s->armv7m, s->rcc, 0x40023800, 5, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->syscfg, 0x40013800, 91, errp) < 0) return;

    for (i = 0; i < STM32F4XX_NUM_UARTS; i++) {
        qdev_prop_set_chr(DEVICE(s->usart[i]), "chardev", serial_hd(i));
    }

    if(stm32_realize_peripheral(&s->armv7m, s->usart[0], 0x40011000, 37, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->usart[1], 0x40004400, 38, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->usart[2], 0x40004800, 39, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->usart[3], 0x40004C00, 52, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->usart[4], 0x40005000, 53, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->usart[5], 0x40011400, 71, errp) < 0) return;

    for(i = 0; i < STM32F4XX_NUM_TIMERS; i++){
        qdev_prop_set_uint64(DEVICE(s->tim[i]), "clock-frequency", 100000000);
    }

    if(stm32_realize_peripheral(&s->armv7m, s->tim[0], 0x40000000, 28, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->tim[1], 0x40000400, 29, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->tim[2], 0x40000800, 30, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->tim[3], 0x40000C00, 50, errp) < 0) return;

    /* ADC 1 to 3 */
    object_property_set_int(OBJECT(s->adc_irqs), STM32F4XX_NUM_ADCS,
                            "num-lines", &err);

    object_property_set_bool(OBJECT(s->adc_irqs), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return ;
    }
    qdev_connect_gpio_out(DEVICE(s->adc_irqs), 0, qdev_get_gpio_in(armv7m, 18));

    if(stm32_realize_peripheral(&s->armv7m, s->adc[0], 0x40012400, 18, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->adc[1], 0x40012800, 18, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->adc[2], 0x40013C00, 18, errp) < 0) return;

    if(stm32_realize_peripheral(&s->armv7m, s->spi[0], 0x40013000, 18, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->spi[1], 0x40003800, 18, errp) < 0) return;
    if(stm32_realize_peripheral(&s->armv7m, s->spi[2], 0x40003C00, 18, errp) < 0) return;
}

static Property stm32f4xx_soc_properties[] = {
    DEFINE_PROP_STRING("cpu-type", struct stm32f4xx_soc, cpu_type),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f4xx_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f4xx_soc_realize;
    dc->props = stm32f4xx_soc_properties;
}

static const TypeInfo stm32f4xx_soc_info = {
    .name          = TYPE_STM32F4XX_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32f4xx_soc),
    .instance_init = stm32f4xx_soc_initfn,
    .class_init    = stm32f4xx_soc_class_init,
};

static void stm32f4xx_soc_types(void)
{
    type_register_static(&stm32f4xx_soc_info);
}

type_init(stm32f4xx_soc_types)
