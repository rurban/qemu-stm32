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

struct stm32f10x_duino {
    struct stm32f10x_soc *cpu;
    struct arm_boot_info boot_info;
};

static void stm32f10x_duino_init(MachineState *machine) {
    printf("stm32f10x_duino init\n");
    struct stm32f10x_duino *s = g_new0(struct stm32f10x_duino, 1);

    if (!machine->kernel_filename) {
        fprintf(stderr, "Guest image must be specified (using -kernel)\n");
        exit(1);
    }

    s->cpu = STM32F10X_SOC(object_new(TYPE_STM32F10X_SOC));
    object_property_set_str(OBJECT(s->cpu), machine->cpu_model, "cpu-model", &error_abort);
    object_property_set_str(OBJECT(s->cpu), machine->kernel_filename, "kernel-filename", &error_abort);
    object_property_set_bool(OBJECT(s->cpu), true, "realized", &error_fatal);

    memset(&s->boot_info, 0, sizeof(s->boot_info));
    s->boot_info.ram_size = 1024*20;
    s->boot_info.kernel_filename = machine->kernel_filename;
    s->boot_info.kernel_cmdline = NULL;
    s->boot_info.initrd_filename = NULL;

    arm_load_kernel(ARM_CPU(first_cpu), &s->boot_info);
}

static void stm32f10x_duino_machine_init(MachineClass *mc)
{
    mc->desc = "STM32F10X Duino Machine";
    mc->init = stm32f10x_duino_init;
}

DEFINE_MACHINE("stm32f10x-duino", stm32f10x_duino_machine_init)
