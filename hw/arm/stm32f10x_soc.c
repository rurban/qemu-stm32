/*
 * STM32F10x SoC
 *
 * Copyright (c) 2018 Martin Schröder <mskchreder.uk@gmail.com>
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
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
#include "hw/arm/stm32f10x_soc.h"
#include "hw/arm/arm.h"
#include "cpu.h"

#define FLASH_BASE_ADDRESS 0x08000000
#define FLASH_SIZE (1024 * 1024)
#define SRAM_BASE_ADDRESS 0x20000000
#define SRAM_SIZE (128 * 1024)

struct stm32_periph_def {
    uint32_t base;
    uint8_t irq;
};

static const struct stm32_periph_def _timer_def[STM32F10X_NUM_TIMERS] = {
    { .base = TIM2_BASE, .irq = TIM2_IRQn },
    { .base = TIM3_BASE, .irq = TIM3_IRQn },
    { .base = TIM4_BASE, .irq = TIM4_IRQn },
    { .base = TIM5_BASE, .irq = TIM5_IRQn }
};

static const struct stm32_periph_def _uart_def[STM32F10X_NUM_UARTS] = {
    { .base = USART1_BASE, .irq = USART1_IRQn },
    { .base = USART2_BASE, .irq = USART2_IRQn },
    { .base = USART3_BASE, .irq = USART3_IRQn },
    { .base = UART4_BASE, .irq = UART4_IRQn },
    { .base = UART5_BASE, .irq = UART5_IRQn }
};

static const struct stm32_periph_def _adc_def[STM32F10X_NUM_ADCS] = {
    { .base = ADC1_BASE, .irq = ADC1_IRQn },
    { .base = ADC2_BASE, .irq = ADC1_IRQn },
    { .base = ADC3_BASE, .irq = ADC1_IRQn }
};

static const struct stm32_periph_def _spi_def[STM32F10X_NUM_SPIS] = {
    { .base = SPI1_BASE, .irq = SPI1_IRQn },
    { .base = SPI2_BASE, .irq = SPI2_IRQn },
    { .base = SPI3_BASE, .irq = SPI3_IRQn }
};

struct stm32f10x_soc *stm32f10x_soc_init(const char *kernel_filename) {
    struct stm32f10x_soc *s = g_new0(struct stm32f10x_soc, 1);
    DeviceState *dev;
    SysBusDevice *busdev;
    Error *err = NULL;
    int i;

    object_initialize(&s->syscfg, sizeof(s->syscfg), TYPE_STM32F2XX_SYSCFG);
    qdev_set_parent_bus(DEVICE(&s->syscfg), sysbus_get_default());

    for (i = 0; i < STM32F10X_NUM_UARTS; i++) {
        object_initialize(&s->usart[i], sizeof(s->usart[i]),
                          TYPE_STM32F2XX_USART);
        qdev_set_parent_bus(DEVICE(&s->usart[i]), sysbus_get_default());
    }

    for (i = 0; i < STM32F10X_NUM_TIMERS; i++) {
        object_initialize(&s->timer[i], sizeof(s->timer[i]),
                          TYPE_STM32F2XX_TIMER);
        qdev_set_parent_bus(DEVICE(&s->timer[i]), sysbus_get_default());
    }

    s->adc_irqs = OR_IRQ(object_new(TYPE_OR_IRQ));

    for (i = 0; i < STM32F10X_NUM_ADCS; i++) {
        object_initialize(&s->adc[i], sizeof(s->adc[i]),
                          TYPE_STM32F2XX_ADC);
        qdev_set_parent_bus(DEVICE(&s->adc[i]), sysbus_get_default());
    }

    for (i = 0; i < STM32F10X_NUM_SPIS; i++) {
        object_initialize(&s->spi[i], sizeof(s->spi[i]),
                          TYPE_STM32F2XX_SPI);
        qdev_set_parent_bus(DEVICE(&s->spi[i]), sysbus_get_default());
    }

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *flash_alias = g_new(MemoryRegion, 1);

    memory_region_init_ram(flash, NULL, "STM32F10x.flash", FLASH_SIZE, &error_fatal);
    vmstate_register_ram_global(flash);
    memory_region_init_alias(flash_alias, NULL, "STM32F10x.flash.alias", flash, 0, FLASH_SIZE);

    memory_region_set_readonly(flash, true);
    memory_region_set_readonly(flash_alias, true);

    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, flash);
    memory_region_add_subregion(system_memory, 0, flash_alias);

    memory_region_init_ram(sram, NULL, "STM32F10x.sram", SRAM_SIZE, &error_fatal);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, sram);

    printf("armv7m init: %s\n", kernel_filename);
    s->cpu = armv7m_init(get_system_memory(), FLASH_BASE_ADDRESS, FLASH_SIZE, 96,
                       kernel_filename, s->cpu_model);
    DeviceState *nvic = s->cpu->nvic;
    //armv7m = DEVICE(&s->armv7m);
    /*
    qdev_prop_set_uint32(armv7m, "num-irq", 96);
    qdev_prop_set_string(armv7m, "cpu-model", s->cpu_model);
    object_property_set_link(OBJECT(armv7m), OBJECT(get_system_memory()),
                                     "memory", &error_abort);
    object_property_set_bool(OBJECT(armv7m), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    */

    /* System configuration controller */
    dev = DEVICE(&s->syscfg);
    object_property_set_bool(OBJECT(&s->syscfg), true, "realized", &err);
    if (err != NULL) {
        //error_propagate(errp, err);
        return 0;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0x40013800);
    //sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(s->nvic, 71));

    /* Attach UART (uses USART registers) and USART controllers */
    for (i = 0; i < STM32F10X_NUM_UARTS; i++) {
        dev = DEVICE(&(s->usart[i]));

        if (i < MAX_SERIAL_PORTS) {
            CharDriverState *chr;

            chr = serial_hds[i];

            if (!chr) {
                char label[20];
                snprintf(label, sizeof(label), "stm32.uart%d", i);
                chr = qemu_chr_new(label, "null");
            }

            qdev_prop_set_chr(dev, "chardev", chr);
        }

        object_property_set_bool(OBJECT(&s->usart[i]), true, "realized", &err);
        if (err != NULL) {
            //error_propagate(errp, err);
            return 0;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _uart_def[i].base);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(nvic, _uart_def[i].irq));
    }

    /* Timer 2 to 5 */
    for (i = 0; i < STM32F10X_NUM_TIMERS; i++) {
        dev = DEVICE(&(s->timer[i]));
        qdev_prop_set_uint64(dev, "clock-frequency", 1000000000);
        object_property_set_bool(OBJECT(&s->timer[i]), true, "realized", &err);
        if (err != NULL) {
            //error_propagate(errp, err);
            return 0;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _timer_def[i].base);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(nvic, _timer_def[i].irq));
    }

    /* ADC 1 to 3 */
    object_property_set_int(OBJECT(s->adc_irqs), STM32F10X_NUM_ADCS,
                            "num-lines", &err);
    object_property_set_bool(OBJECT(s->adc_irqs), true, "realized", &err);
    if (err != NULL) {
        //error_propagate(errp, err);
        return 0;
    }
    qdev_connect_gpio_out(DEVICE(s->adc_irqs), 0,
                          qdev_get_gpio_in(nvic, _adc_def[i].irq));

    for (i = 0; i < STM32F10X_NUM_ADCS; i++) {
        dev = DEVICE(&(s->adc[i]));
        object_property_set_bool(OBJECT(&s->adc[i]), true, "realized", &err);
        if (err != NULL) {
            //error_propagate(errp, err);
            return 0;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _adc_def[i].base);
        sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(DEVICE(s->adc_irqs), i));
    }

    /* SPI 1 and 2 */
    for (i = 0; i < STM32F10X_NUM_SPIS; i++) {
        dev = DEVICE(&(s->spi[i]));
        object_property_set_bool(OBJECT(&s->spi[i]), true, "realized", &err);
        if (err != NULL) {
            //error_propagate(errp, err);
            return 0;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _spi_def[i].base);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(nvic, _spi_def[i].irq));
    }

    return s;
}

/*
static Property stm32f205_soc_properties[] = {
    DEFINE_PROP_STRING("cpu-model", struct stm32f10x_soc, cpu_model),
    DEFINE_PROP_STRING("kernel-filename", struct stm32f10x_soc, kernel_filename),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f205_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f205_soc_realize;
    dc->props = stm32f205_soc_properties;
}

static const TypeInfo stm32f205_soc_info = {
    .name          = TYPE_STM32F10X_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32f10x_soc),
    .instance_init = stm32f205_soc_initfn,
    .class_init    = stm32f205_soc_class_init,
};

static void stm32f205_soc_types(void)
{
    type_register_static(&stm32f205_soc_info);
}

type_init(stm32f205_soc_types)
*/
