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
#include "hw/arm/stm32fxxx.h"

#define TYPE_STM32FXXX_PWR "stm32fxxx-pwr"

#define PWR_CR_UDEN     (3 << 18)
#define PWR_CR_ODSWEN   (1 << 17)
#define PWR_CR_ODEN     (1 << 16)
#define PWR_CR_VOS      (3 << 14)
#define PWR_CR_ADCDC1   (1 << 13)
#define PWR_CR_MRUDS    (1 << 11)
#define PWR_CR_LPUDS    (1 << 10)
#define PWR_CR_FPDS     (1 << 9)
#define PWR_CR_DBP      (1 << 8)
#define PWR_CR_PLS      (7 << 5)
#define PWR_CR_PVDE     (1 << 4)
#define PWR_CR_CSBF     (1 << 3)
#define PWR_CR_CWUF     (1 << 2)
#define PWR_CR_PDDS     (1 << 1)
#define PWR_CR_LPDS     (1 << 0)

#define PWR_CSR_UDRDY   (3 << 18)
#define PWR_CSR_ODSWRDY (1 << 17)
#define PWR_CSR_ODRDY   (1 << 16)
#define PWR_CSR_VOSRDY  (1 << 14)
#define PWR_CSR_BRE     (1 << 9)
#define PWR_CSR_EWUP    (1 << 8)
#define PWR_CSR_BRR     (1 << 3)
#define PWR_CSR_PVDO    (1 << 2)
#define PWR_CSR_SBF     (1 << 1)
#define PWR_CSR_WUF     (1 << 0)

#define PWR_TRACE(fmt, ...) fprintf(stderr, "stm32fxxx_pwr: " fmt, ##__VA_ARGS__)
#define PWR_ERROR(fmt, ...) fprintf(stderr, "stm32fxxx_pwr: ERROR: " fmt, ##__VA_ARGS__)

struct stm32fxxx_pwr {
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;

    struct stm32fxxx_state *state;
};

static uint64_t stm32fxxx_pwr_read(void *opaque, hwaddr addr, unsigned int size){
    struct stm32fxxx_pwr *self = (struct stm32fxxx_pwr*)opaque;
    switch(addr){
        case 0x00: {
            return self->state->PWR_CR;
        } break;
        case 0x04: {
            return self->state->PWR_CSR;
        } break;
        default: {
            fprintf(stderr, "PWR: accessing unknown register at offset %08x\n", (uint32_t)addr);
        }
    }
    return 0;
}

static void stm32fxxx_pwr_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size){
    struct stm32fxxx_pwr *self = (struct stm32fxxx_pwr*)opaque;
    if(size > 4){
        PWR_TRACE("write: invalid write size of %d bytes\n", size);
    }
    uint32_t val = (uint32_t) val64;
    switch(addr){
        case 0x00: { // CR
            uint32_t valx = val ^ self->state->PWR_CR;
            if(valx & PWR_CR_UDEN){
                if((val & PWR_CR_UDEN) == PWR_CR_UDEN) {
                    PWR_TRACE("underdrive enable in stop mode: enabled\n");
                    self->state->PWR_CR |= PWR_CR_UDEN;
                    self->state->PWR_CSR |= PWR_CSR_UDRDY;
                } else if((val & PWR_CR_UDEN) == 0) {
                    PWR_TRACE("underdrive enable in stop mode: disabled\n");
                    self->state->PWR_CR &= ~PWR_CR_UDEN;
                    self->state->PWR_CSR &= ~PWR_CSR_UDRDY;
                } else {
                    PWR_TRACE("invalid UDEN value %08x!\n", val & PWR_CR_UDEN);
                }
            }
            if(valx & PWR_CR_ODSWEN) {
                if(!(self->state->PWR_CSR & PWR_CSR_ODRDY)){
                    PWR_TRACE("overdrive: can not set ODSWEN bit before overdrive has been enabled and ready!\n");
                } else {
                    if((val & PWR_CR_ODSWEN)){
                        PWR_TRACE("overdrive switching: enabled\n");
                        self->state->PWR_CR |= PWR_CR_ODSWEN;
                        self->state->PWR_CSR |= PWR_CSR_ODSWRDY;
                    } else {
                        PWR_TRACE("overdrive switching: disabled\n");
                        self->state->PWR_CR &= ~PWR_CR_ODSWEN;
                        self->state->PWR_CSR &= ~PWR_CSR_ODSWRDY;
                    }
                }
            }
            if(valx & PWR_CR_ODEN) {
                if((val & PWR_CR_ODEN)){
                    PWR_TRACE("overdrive: enabled\n");
                    self->state->PWR_CR |= PWR_CR_ODEN;
                    self->state->PWR_CSR |= PWR_CSR_ODRDY;
                } else {
                    PWR_TRACE("overdrive: disabled\n");
                    self->state->PWR_CR &= ~PWR_CR_ODEN;
                    self->state->PWR_CSR &= ~PWR_CSR_ODRDY;
                }
            }
            if(valx & PWR_CR_VOS) {
                uint32_t vos = (val & PWR_CR_VOS) >> 14;
                switch(vos){
                    case 0: PWR_TRACE("invalid value for VOS (0)\n"); break;
                    case 1: PWR_TRACE("voltage scale 3 selected\n"); break;
                    case 2: PWR_TRACE("voltage scale 2 selected\n"); break;
                    case 3: PWR_TRACE("voltage scale 1 selected\n"); break;
                }
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_VOS) | (val & PWR_CR_VOS);
            }
            if(valx & PWR_CR_ADCDC1) {
                if(!(val & PWR_CR_ADCDC1)) PWR_TRACE("setting ADCDC1 to 0 has no effect\n");
                else PWR_TRACE("ADCDC1 bit set to 1\n");
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_ADCDC1) | (val & PWR_CR_ADCDC1);
            }
            if(valx & PWR_CR_MRUDS) {
                if(val & PWR_CR_MRUDS) PWR_TRACE("Main Regulator: in under-drive mode and Flash memory in power-down when the device is in Stop under-drive mode.\n");
                else PWR_TRACE("Main regulator: will be ON when the device is in Stop mode\n");
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_MRUDS) | (val & PWR_CR_MRUDS);
            }
            if(valx & PWR_CR_LPUDS) {
                if(val & PWR_CR_LPUDS) PWR_TRACE("Low-power regulator: ON if LPDS bit is set when the device is in Stop mode\n");
                else PWR_TRACE("Low-power regulator: in under-drive mode if LPDS bit is set and Flash memory in powerdown when the device is in Stop under-drive mode.\n");
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_LPUDS) | (val & PWR_CR_LPUDS);
            }
            if(valx & PWR_CR_FPDS){
                if(val & PWR_CR_FPDS) PWR_TRACE("Flash memory not in power-down when the device is in Stop mode\n");
                else PWR_TRACE("Flash memory in power-down when the device is in Stop mode\n");
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_FPDS) | (val & PWR_CR_FPDS);
            }
            if(valx & PWR_CR_DBP) {
                if(val & PWR_CR_DBP) PWR_TRACE("Access to RTC and RTC Backup registers and backup SRAM disabled\n");
                else PWR_TRACE("Access to RTC and RTC Backup registers and backup SRAM enabled\n");
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_DBP) | (val & PWR_CR_DBP);
            }
            if(valx & PWR_CR_PLS) {
                const char *voltage[] = {
                    "2.0v",
                    "2.1v",
                    "2.3v",
                    "2.5v",
                    "2.6v",
                    "2.7v",
                    "2.8v",
                    "2.9v",
                };
                PWR_TRACE("power voltage detector level set to %s\n", voltage[(val & PWR_CR_PLS) >> 5]);
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_PLS) | (val & PWR_CR_PLS);
            }
            if(valx & PWR_CR_PVDE) {
                if(val & PWR_CR_PVDE) PWR_TRACE("power voltage detector: enabled\n");
                else PWR_TRACE("power voltage detector: disabled\n");
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_PVDE) | (val & PWR_CR_PVDE);
            }
            if(!(valx & PWR_CR_CSBF)) { // we check if written value and store value are both 1
                if(val & PWR_CR_CSBF) {
                    PWR_TRACE("standby flag cleared\n");
                    self->state->PWR_CR &= ~PWR_CR_CSBF;
                }
            }
            if(!(valx & PWR_CR_CWUF)){ // check if both values are 1
                if(val & PWR_CR_CWUF){
                    PWR_TRACE("wake up flag will be cleared after 2 clock cycles\n");
                    self->state->PWR_CR &= ~PWR_CR_CWUF;
                }
            }
            if(valx & PWR_CR_PDDS) {
                if(val & PWR_CR_PDDS) {
                    PWR_TRACE("configured cpu to enter stop mode when entering deep sleep\n");
                } else {
                    PWR_TRACE("configured cpu to enter standby mode when entering deep sleep\n");
                }
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_PDDS) | (val & PWR_CR_PDDS);
            }
            if(valx & PWR_CR_LPDS){
                if(val & PWR_CR_LPDS){
                    PWR_TRACE("main voltage regulator will be on when in stop mode\n");
                } else {
                    PWR_TRACE("low power voltage regulator will be ON when in stop mode\n");
                }
                self->state->PWR_CR = (self->state->PWR_CR & PWR_CR_LPDS) | (val & PWR_CR_LPDS);
            }
        } break;
        case 0x04: { // CSR
            uint32_t valx = val ^ self->state->PWR_CSR;
            if(val & PWR_CSR_UDRDY) { // check val here and not valx
                uint32_t udrdy = val & PWR_CSR_UDRDY;
                PWR_TRACE("resetting UDRDY bits\n");
                self->state->PWR_CSR &= ~udrdy;
            }
            if(valx & PWR_CSR_ODSWRDY){
                PWR_ERROR("ODSWRDY bit is readonly\n");
            }
            if(valx & PWR_CSR_ODRDY) {
                PWR_ERROR("ODRDY bit is readonly\n");
            }
            if(valx & PWR_CSR_VOSRDY) {
                PWR_ERROR("VOSRDY bit is readonly\n");
            }
            if(valx & PWR_CSR_BRE){
                if(val & PWR_CSR_BRE){
                    PWR_TRACE("backup regulator: enabled\n");
                } else {
                    PWR_TRACE("backup regulator: disabled\n");
                }
            }
            if(valx & PWR_CSR_EWUP) {
                if(val & PWR_CSR_EWUP){
                    PWR_TRACE("wakeup pin: used for wakeup from standby\n");
                } else {
                    PWR_TRACE("wakeup pin: not used (configured as GPIO)\n");
                }
            }
            if(valx & PWR_CSR_BRR){
                PWR_ERROR("BRR bit is readonly\n");
            }
            if(valx & PWR_CSR_PVDO){
                PWR_ERROR("PVDO bit is readonly\n");
            }
            if(valx & PWR_CSR_SBF){
                PWR_ERROR("SBF bit is readonly\n");
            }
            if(valx & PWR_CSR_WUF){
                PWR_ERROR("WUF bit is readonly\n");
            }
        } break;
    }
}

static const MemoryRegionOps stm32fxxx_pwr_ops = {
    .read = stm32fxxx_pwr_read,
    .write = stm32fxxx_pwr_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void stm32fxxx_pwr_init(Object *obj){
    struct stm32fxxx_pwr *self = OBJECT_CHECK(struct stm32fxxx_pwr, obj, TYPE_STM32FXXX_PWR);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &self->irq);
    memory_region_init_io(&self->mmio, obj, &stm32fxxx_pwr_ops, self, TYPE_STM32FXXX_PWR, 2 * sizeof(uint32_t));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &self->mmio);
}

static void stm32fxxx_pwr_reset(DeviceState *dev){
    struct stm32fxxx_pwr *self = OBJECT_CHECK(struct stm32fxxx_pwr, dev, TYPE_STM32FXXX_PWR);
    self->state->PWR_CR = 0x0000C000;
    self->state->PWR_CSR = 0;
}

static void stm32fxxx_pwr_realize(DeviceState *dev, Error **errp){
    stm32fxxx_pwr_reset(dev);
}

static Property stm32fxxx_pwr_properties[] = {
    DEFINE_PROP("state", struct stm32fxxx_pwr, state, qdev_prop_ptr, struct stm32fxxx_state*),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32fxxx_pwr_class_init(ObjectClass *klass, void *data){
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = stm32fxxx_pwr_reset;
    dc->props = stm32fxxx_pwr_properties;
    dc->realize = stm32fxxx_pwr_realize;
}

static const TypeInfo stm32fxxx_pwr_info = {
    .name          = TYPE_STM32FXXX_PWR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32fxxx_pwr),
    .instance_init = stm32fxxx_pwr_init,
    .class_init    = stm32fxxx_pwr_class_init
};

static void stm32fxxx_pwr_register_types(void){
    type_register_static(&stm32fxxx_pwr_info);
}

type_init(stm32fxxx_pwr_register_types)

