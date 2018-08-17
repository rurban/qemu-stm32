/*
 * Olimex STM32 P103 Development Board
 *
 * Copyright (C) 2010 Andre Beckus
 *
 * Implementation based on
 * Olimex "STM-P103 Development Board Users Manual Rev. A, April 2008"
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"


#include "stm32f1xx.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"

struct stm32_duino {
    Stm32 *stm32;
    Stm32Gpio *stm32_gpio[STM32F1XX_GPIO_COUNT];
    Stm32Uart *stm32_uart[STM32_UART_COUNT];
};

static void led_irq_handler(void *opaque, int n, int level)
{
    /* There should only be one IRQ for the LED */
    assert(n == 0);

    /* Assume that the IRQ is only triggered if the LED has changed state.
     * If this is not correct, we may get multiple LED Offs or Ons in a row.
     */
    switch (level) {
        case 0:
            printf("LED Off\n");
            break;
        case 1:
            printf("LED On\n");
            break;
    }
}

static void stm32_duino_init(MachineState *machine) {
    qemu_irq *led_irq;
    struct stm32_duino *s;

    s = (struct stm32_duino *)g_malloc0(sizeof(struct stm32_duino));

    stm32f1xx_init(/*flash_size in bytes */ 128 * 1024,
               /*ram_size in bytes */ 20 * 1024,
               machine,
               s->stm32_gpio,
               s->stm32_uart,
               8000000,
               32768);

    /* Connect LED to GPIO C pin 13 */
    led_irq = qemu_allocate_irqs(led_irq_handler, NULL, 1);
    qdev_connect_gpio_out((DeviceState *)s->stm32_gpio[STM32_GPIOC_INDEX], 13, led_irq[0]);

    /* Connect RS232 to UART */
    stm32_uart_connect(
            s->stm32_uart[STM32_UART1_INDEX],
            serial_hds[0],
            STM32_USART1_NO_REMAP);
}

static void stm32_duino_machine_init(MachineClass *mc)
{
    mc->desc = "STM32 Mini Duino Board";
    mc->init = stm32_duino_init;
}

DEFINE_MACHINE("stm32-duino", stm32_duino_machine_init)
