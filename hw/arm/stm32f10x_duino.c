/*
 * Netduino 2 Machine Model
 *
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
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "hw/arm/stm32f10x_soc.h"
#include "hw/arm/arm.h"

static void stm32f10x_duino_init(MachineState *machine)
{
    //object_property_set_bool(OBJECT(dev), true, "realized", &error_fatal);

    struct stm32f10x_soc *cpu = stm32f10x_soc_init(machine->kernel_filename);

    struct arm_boot_info binfo;
    binfo.ram_size = 1024*20;
    binfo.kernel_filename = machine->kernel_filename;
    binfo.kernel_cmdline = "";
    binfo.initrd_filename = "";
    binfo.loader_start = 0x08000000;
    arm_load_kernel(cpu->cpu, &binfo);
}

static void stm32f10x_duino_machine_init(MachineClass *mc)
{
    mc->desc = "STM32F10X Duino Machine";
    mc->init = stm32f10x_duino_init;
}

DEFINE_MACHINE("stm32f10x-duino", stm32f10x_duino_machine_init)
