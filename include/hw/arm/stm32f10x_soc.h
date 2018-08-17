#pragma once

#include "hw/misc/stm32f2xx_syscfg.h"
#include "hw/timer/stm32f2xx_timer.h"
#include "hw/char/stm32f2xx_usart.h"
#include "hw/adc/stm32f2xx_adc.h"
#include "hw/or-irq.h"
#include "hw/ssi/stm32f2xx_spi.h"
//#include "hw/arm/armv7m.h"
#include "stm32f10x.h"
#include "hw/arm/arm.h"

#define TYPE_STM32F10X_SOC "stm32f10x-soc"
#define STM32F10X_SOC(obj) \
    OBJECT_CHECK(struct stm32f10x_soc, (obj), TYPE_STM32F10X_SOC)

#define STM32F10X_NUM_UARTS 6
#define STM32F10X_NUM_TIMERS 4
#define STM32F10X_NUM_ADCS 3
#define STM32F10X_NUM_SPIS 3

struct stm32f10x_soc {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ARMCPU *cpu;

    char *cpu_model;

 //   ARMv7MState armv7m;

    STM32F2XXSyscfgState syscfg;
    STM32F2XXUsartState usart[STM32F10X_NUM_UARTS];
    STM32F2XXTimerState timer[STM32F10X_NUM_TIMERS];
    STM32F2XXADCState adc[STM32F10X_NUM_ADCS];
    STM32F2XXSPIState spi[STM32F10X_NUM_SPIS];

    qemu_or_irq *adc_irqs;
};

struct stm32f10x_soc *stm32f10x_soc_init(const char *kernel_filename);
