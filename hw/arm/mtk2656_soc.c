/*
 * Mediatek MTK2656 SoC (Quectel BC66)
 *
 * Copyright (c) 2021 Reini Urban <reinhard.urban@nubix.de>
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
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/arm/mtk2656_soc.h"
#include "hw/misc/unimp.h"

// FIXME
#define SYSCFG_ADD                     0x80013800
static const uint32_t usart_addr[] = { 0x80011000, 0x80004400, 0x80004800 };
/* At the moment only Timer 2 to 5 are modelled */
static const uint32_t timer_addr[] = { 0x80000000, 0x80000400,
                                       0x80000800, 0x80000C00 };
static const uint32_t adc_addr[]   = { 0x80012000 };
#define EXTI_ADDR                      0x80013C00

#define SYSCFG_IRQ               0
static const int usart_irq[] = { 26, 27, 28, 29 };
static const int timer_irq[] = { 53, 54, 55, 56, 57 };
#define ADC_IRQ                  51
// 13 eint pins, see ql_gpio.h: 4xSPI, NETLIGHT, RI, DCD, CTS, RTS, GPIO1, ...
static const int exti_irq[] =  { 40, 40, 40, 40, 40,
				 40, 40, 40, 40, 40,
				 40, 40, 40 } ;


static void mtk2656_soc_initfn(Object *obj)
{
    MTK2656State *s = MTK2656_SOC(obj);
    int i;

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    object_initialize_child(obj, "syscfg", &s->syscfg, TYPE_STM32F4XX_SYSCFG);

    for (i = 0; i < STM_NUM_USARTS; i++) {
        object_initialize_child(obj, "usart[*]", &s->usart[i],
                                TYPE_STM32F2XX_USART);
    }

    for (i = 0; i < STM_NUM_TIMERS; i++) {
        object_initialize_child(obj, "timer[*]", &s->timer[i],
                                TYPE_STM32F2XX_TIMER);
    }

    for (i = 0; i < STM_NUM_ADCS; i++) {
        object_initialize_child(obj, "adc[*]", &s->adc[i], TYPE_STM32F2XX_ADC);
    }

    object_initialize_child(obj, "exti", &s->exti, TYPE_STM32F4XX_EXTI);
}

static void mtk2656_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MTK2656State *s = MTK2656_SOC(dev_soc);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *dev, *armv7m;
    SysBusDevice *busdev;
    Error *err = NULL;
    int i;

    memory_region_init_rom(&s->flash, OBJECT(dev_soc), "MTK2656.flash",
                           FLASH_SIZE, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_init_alias(&s->flash_alias, OBJECT(dev_soc),
                             "MTK2656.flash.alias", &s->flash, 0,
                             FLASH_SIZE);

    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, &s->flash);
    memory_region_add_subregion(system_memory, 0, &s->flash_alias);

    memory_region_init_ram(&s->sram, NULL, "MTK2656.sram", SRAM_SIZE,
                           &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, &s->sram);

    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 96);
    qdev_prop_set_string(armv7m, "cpu-type", s->cpu_type);
    //qdev_prop_set_bit(armv7m, "enable-bitband", true);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    /* System configuration controller */
    dev = DEVICE(&s->syscfg);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->syscfg), errp)) {
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, SYSCFG_ADD);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, SYSCFG_IRQ));

    /* Attach UART (uses USART registers) and USART controllers */
    for (i = 0; i < STM_NUM_USARTS; i++) {
        dev = DEVICE(&(s->usart[i]));
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usart[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, usart_addr[i]);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, usart_irq[i]));
    }

    /* Timer 2 to 5 */
    for (i = 0; i < STM_NUM_TIMERS; i++) {
        dev = DEVICE(&(s->timer[i]));
        qdev_prop_set_uint64(dev, "clock-frequency", 1000000000);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, timer_addr[i]);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, timer_irq[i]));
    }

    /* ADC device, the IRQs are ORed together */
    if (!object_initialize_child_with_props(OBJECT(s), "adc-orirq",
                                            &s->adc_irqs, sizeof(s->adc_irqs),
                                            TYPE_OR_IRQ, errp, NULL)) {
        return;
    }
    object_property_set_int(OBJECT(&s->adc_irqs), "num-lines", STM_NUM_ADCS,
                            &error_abort);
    if (!qdev_realize(DEVICE(&s->adc_irqs), NULL, errp)) {
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->adc_irqs), 0,
                          qdev_get_gpio_in(armv7m, ADC_IRQ));

    for (i = 0; i < STM_NUM_ADCS; i++) {
        dev = DEVICE(&(s->adc[i]));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, adc_addr[i]);
        sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(DEVICE(&s->adc_irqs), i));
    }

    /* EXTI device */
    dev = DEVICE(&s->exti);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->exti), errp)) {
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, EXTI_ADDR);
    for (i = 0; i < 8; i++) {
        sysbus_connect_irq(busdev, i, qdev_get_gpio_in(armv7m, exti_irq[i]));
    }
    for (i = 0; i < 8; i++) {
        qdev_connect_gpio_out(DEVICE(&s->syscfg), i, qdev_get_gpio_in(dev, i));
    }

    //create_unimplemented_device("timer[7]",    0x80001400, 0x400);
    //create_unimplemented_device("timer[12]",   0x80001800, 0x400);
    //create_unimplemented_device("timer[6]",    0x80001000, 0x400);
    //create_unimplemented_device("timer[13]",   0x80001C00, 0x400);
    //create_unimplemented_device("timer[14]",   0x80002000, 0x400);
    create_unimplemented_device("RTC and BKP", 0x80002800, 0x400);
    create_unimplemented_device("WWDG",        0x80002C00, 0x400);
    create_unimplemented_device("IWDG",        0x80003000, 0x400);
    create_unimplemented_device("PWR",         0x80007000, 0x400);
    create_unimplemented_device("DAC",         0x80007400, 0x400);
    create_unimplemented_device("timer[1]",    0x80010000, 0x400);
    //create_unimplemented_device("timer[8]",    0x80010400, 0x400);
    create_unimplemented_device("SDIO",        0x80012C00, 0x400);
    //create_unimplemented_device("timer[9]",    0x80014000, 0x400);
    //create_unimplemented_device("timer[10]",   0x80014400, 0x400);
    //create_unimplemented_device("timer[11]",   0x80014800, 0x400);
    //create_unimplemented_device("GPIOA",       0x80020000, 0x400);
    //create_unimplemented_device("GPIOB",       0x80020400, 0x400);
    //create_unimplemented_device("GPIOC",       0x80020800, 0x400);
    //create_unimplemented_device("GPIOD",       0x80020C00, 0x400);
    //create_unimplemented_device("GPIOE",       0x80021000, 0x400);
    //create_unimplemented_device("GPIOF",       0x80021400, 0x400);
    //create_unimplemented_device("GPIOG",       0x80021800, 0x400);
    //create_unimplemented_device("GPIOH",       0x80021C00, 0x400);
    //create_unimplemented_device("GPIOI",       0x80022000, 0x400);
    create_unimplemented_device("CRC",         0x80023000, 0x400);
    create_unimplemented_device("RCC",         0x80023800, 0x400);
    create_unimplemented_device("Flash Int",   0x80023C00, 0x400);
    create_unimplemented_device("BKPSRAM",     0x80024000, 0x400);
    create_unimplemented_device("DMA1",        0x80026000, 0x400);
    create_unimplemented_device("DMA2",        0x80026400, 0x400);
    create_unimplemented_device("Ethernet",    0x80028000, 0x1400);
    //create_unimplemented_device("USB OTG HS",  0x80040000, 0x30000);
    //create_unimplemented_device("USB OTG FS",  0x90000000, 0x31000);
    //create_unimplemented_device("DCMI",        0x90050000, 0x400);
    //create_unimplemented_device("RNG",         0x90060800, 0x400);
}

static Property mtk2656_soc_properties[] = {
    DEFINE_PROP_STRING("cpu-type", MTK2656State, cpu_type),
    DEFINE_PROP_END_OF_LIST(),
};

static void mtk2656_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mtk2656_soc_realize;
    device_class_set_props(dc, mtk2656_soc_properties);
    /* No vmstate or reset required: device has no internal state */
}

static const TypeInfo mtk2656_soc_info = {
    .name          = TYPE_MTK2656_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MTK2656State),
    .instance_init = mtk2656_soc_initfn,
    .class_init    = mtk2656_soc_class_init,
};

static void mtk2656_soc_types(void)
{
    type_register_static(&mtk2656_soc_info);
}

type_init(mtk2656_soc_types)
