/*
 * STM32F10x SoC
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
#define FLASH_SIZE (1024 * 1024)
#define SRAM_BASE_ADDRESS 0x20000000
#define SRAM_SIZE (128 * 1024)

#include "hw/misc/stm32f2xx_syscfg.h"
#include "hw/timer/stm32f2xx_timer.h"
#include "hw/adc/stm32f2xx_adc.h"
#include "hw/or-irq.h"
#include "hw/ssi/stm32f2xx_spi.h"
//#include "hw/arm/armv7m.h"
#include "stm32f10x.h"
#include "hw/arm/arm.h"

#define TYPE_STM32F10X_SOC "stm32f10x-soc"
#define STM32F10X_SOC(obj) \
    OBJECT_CHECK(struct stm32f10x_soc, (obj), TYPE_STM32F10X_SOC)

#define STM32F10X_NUM_UARTS 5
#define STM32F10X_NUM_TIMERS 4
#define STM32F10X_NUM_ADCS 3
#define STM32F10X_NUM_SPIS 3

struct stm32f10x_soc {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ARMv7MState armv7m;

    STM32F2XXSyscfgState syscfg;
    SysBusDevice *usart[STM32F10X_NUM_UARTS];
    SysBusDevice *rcc;
    STM32F2XXTimerState timer[STM32F10X_NUM_TIMERS];
    STM32F2XXADCState adc[STM32F10X_NUM_ADCS];
    STM32F2XXSPIState spi[STM32F10X_NUM_SPIS];

    qemu_or_irq *adc_irqs;

    char *cpu_model;
    //char *kernel_filename;
};

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

static void stm32f10x_soc_initfn(Object *obj){
    struct stm32f10x_soc *s = STM32F10X_SOC(obj);
    int i;

    fprintf(stderr, "stm32f10x_soc init\n");

    object_initialize(&s->armv7m, sizeof(s->armv7m), TYPE_ARMV7M);
    qdev_set_parent_bus(DEVICE(&s->armv7m), sysbus_get_default());

    object_initialize(&s->syscfg, sizeof(s->syscfg), TYPE_STM32F2XX_SYSCFG);
    qdev_set_parent_bus(DEVICE(&s->syscfg), sysbus_get_default());

    DeviceState *rcc = qdev_create(NULL, "stm32f1xx_rcc");
    qdev_prop_set_uint32(rcc, "osc_freq", 8000000);
    qdev_prop_set_uint32(rcc, "osc32_freq", 32000);
    qdev_init_nofail(rcc);
    object_property_add_child(obj, "rcc", OBJECT(rcc), NULL);
    s->rcc = SYS_BUS_DEVICE(rcc);
/*
    DeviceState *syscfg_dev = qdev_create(NULL, "stm32f2xx_syscfg");
    qdev_prop_set_ptr(syscfg_dev, "stm32_rcc", rcc_dev);
    qdev_prop_set_ptr(syscfg_dev, "stm32_exti", exti_dev);
    qdev_prop_set_bit(syscfg_dev, "boot0", 0);
    qdev_prop_set_bit(syscfg_dev, "boot1", 0);
*/
    for (i = 0; i < STM32F10X_NUM_UARTS; i++) {
        s->usart[i] = SYS_BUS_DEVICE(object_new("stm32f1xx-usart"));
        char name[16];
        snprintf(name, sizeof(name), "uart%d", i);
        object_property_add_child(obj, name, OBJECT(s->usart[i]), NULL);
        qdev_set_parent_bus(DEVICE(s->usart[i]), sysbus_get_default());

        //object_initialize(&s->usart[i], sizeof(s->usart[i]), "stm32f10x-uart");
        //qdev_set_parent_bus(DEVICE(&s->usart[i]), sysbus_get_default());
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

}

static void stm32f10x_soc_realize(DeviceState *dev_soc, Error **errp) {
    struct stm32f10x_soc *s = STM32F10X_SOC(dev_soc);
    DeviceState *dev;
    SysBusDevice *busdev;
    Error *err = NULL;
    int i;

    printf("stm32f10x_soc realize\n");

    DeviceState *armv7m = DEVICE(&s->armv7m);

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *flash_alias = g_new(MemoryRegion, 1);

    memory_region_init_ram(flash, NULL, "STM32F10x.flash", FLASH_SIZE, &error_fatal);
    memory_region_init_alias(flash_alias, NULL, "STM32F10x.flash.alias", flash, 0, FLASH_SIZE);

    vmstate_register_ram_global(flash);

    memory_region_set_readonly(flash, true);
    memory_region_set_readonly(flash_alias, true);

    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, flash);
    memory_region_add_subregion(system_memory, 0, flash_alias);

    memory_region_init_ram(sram, NULL, "STM32F10x.sram", SRAM_SIZE, &error_fatal);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, sram);

	// init the cpu on the soc
	qdev_prop_set_uint32(armv7m, "num-irq", 96);
    qdev_prop_set_string(armv7m, "cpu-model", s->cpu_model);
    object_property_set_link(OBJECT(&s->armv7m), OBJECT(get_system_memory()),
                                     "memory", &error_abort);
    object_property_set_bool(OBJECT(&s->armv7m), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->syscfg), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return ;
    }
    sysbus_mmio_map(s->rcc, 0, 0x40023800);
    sysbus_connect_irq(s->rcc, 0, qdev_get_gpio_in(armv7m, RCC_IRQn));

    /* System configuration controller */
    dev = DEVICE(&s->syscfg);
    object_property_set_bool(OBJECT(&s->syscfg), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return ;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0x40013800);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, SYSCFG_IRQn));

    /* Attach UART (uses USART registers) and USART controllers */
    for (i = 0; i < STM32F10X_NUM_UARTS; i++) {
        if(!_uart_def[i].base) continue;

        dev = DEVICE(s->usart[i]);
        qdev_prop_set_chr(dev, "chardev",
                          i < MAX_SERIAL_PORTS ? serial_hds[i] : NULL);

        object_property_set_bool(OBJECT(s->usart[i]), true, "realized", &err);

        if (err != NULL) {
            error_propagate(errp, err);
            return ;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _uart_def[i].base);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, _uart_def[i].irq));
    }

    /* Timer 2 to 5 */
    for (i = 0; i < STM32F10X_NUM_TIMERS; i++) {
        if(!_timer_def[i].base) continue;

        dev = DEVICE(&(s->timer[i]));
        qdev_prop_set_uint64(dev, "clock-frequency", 1000000000);
        object_property_set_bool(OBJECT(&s->timer[i]), true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return ;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _timer_def[i].base);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, _timer_def[i].irq));
    }
    /* ADC 1 to 3 */
    object_property_set_int(OBJECT(s->adc_irqs), STM32F10X_NUM_ADCS,
                            "num-lines", &err);
    object_property_set_bool(OBJECT(s->adc_irqs), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return ;
    }
    qdev_connect_gpio_out(DEVICE(s->adc_irqs), 0,
                          qdev_get_gpio_in(armv7m, _adc_def[i].irq));

    for (i = 0; i < STM32F10X_NUM_ADCS; i++) {
        if(!_adc_def[i].base) continue;
        dev = DEVICE(&(s->adc[i]));
        object_property_set_bool(OBJECT(&s->adc[i]), true, "realized", &err);
        if (err != NULL) {
            //error_propagate(errp, err);
            return ;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _adc_def[i].base);
        sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(DEVICE(s->adc_irqs), i));
    }

    /* SPI 1 and 2 */
    for (i = 0; i < STM32F10X_NUM_SPIS; i++) {
        if(!_spi_def[i].base) continue;
        dev = DEVICE(&(s->spi[i]));
        object_property_set_bool(OBJECT(&s->spi[i]), true, "realized", &err);
        if (err != NULL) {
            //error_propagate(errp, err);
            return ;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, _spi_def[i].base);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, _spi_def[i].irq));
    }
}

static Property stm32f10x_soc_properties[] = {
    DEFINE_PROP_STRING("cpu-model", struct stm32f10x_soc, cpu_model),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f10x_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f10x_soc_realize;
    dc->props = stm32f10x_soc_properties;
}

static const TypeInfo stm32f10x_soc_info = {
    .name          = TYPE_STM32F10X_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32f10x_soc),
    .instance_init = stm32f10x_soc_initfn,
    .class_init    = stm32f10x_soc_class_init,
};

static void stm32f10x_soc_types(void)
{
    type_register_static(&stm32f10x_soc_info);
}

type_init(stm32f10x_soc_types)
