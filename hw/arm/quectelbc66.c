/*
 * Quectel BC66
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
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "hw/arm/mtk2656_soc.h"
#include "hw/arm/boot.h"

/* Main SYSCLK frequency in Hz (78, 156MHz) */
#define BC66_SYSCLK_FRQ    78000000ULL
#define BC66NB_SYSCLK_FRQ 156000000ULL

static void quectelbc66_init(MachineState *machine)
{
    DeviceState *dev;

    system_clock_scale = NANOSECONDS_PER_SECOND / BC66_SYSCLK_FRQ;

    dev = qdev_new(TYPE_MTK2656_SOC);
    qdev_prop_set_string(dev, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    machine->enable_graphics = false;

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       FLASH_SIZE);
}

static void quectelbc66nb_init(MachineState *machine)
{
    DeviceState *dev;

    system_clock_scale = NANOSECONDS_PER_SECOND / BC66NB_SYSCLK_FRQ;

    dev = qdev_new(TYPE_MTK2656_SOC);
    qdev_prop_set_string(dev, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    machine->enable_graphics = false;

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       FLASH_SIZE);
}

static void quectelbc66_machine_init(MachineClass *mc)
{
    mc->desc = "Quectel BC66 (MTK2656 Cortex-M4)";
    mc->init = quectelbc66_init;
}

static void quectelbc66nb_machine_init(MachineClass *mc)
{
    mc->desc = "Quectel BC66NB (MTK2656 Cortex-M4)";
    mc->init = quectelbc66nb_init;
}

// TODO BC68, BC660 should be similar

DEFINE_MACHINE("quectel-bc66", quectelbc66_machine_init)
DEFINE_MACHINE("quectel-bc66nb", quectelbc66nb_machine_init)
