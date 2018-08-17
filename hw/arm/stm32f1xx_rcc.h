#include "hw/sysbus.h"
#include "hw/arm/stm32f10x_clktree.h"

/** RCC Base data structure */
struct Stm32Rcc {
    /* Inherited */
    SysBusDevice busdev;

    /* Properties */
    uint32_t osc_freq;
    uint32_t osc32_freq;

    /* Private */
    MemoryRegion iomem;
    qemu_irq irq;

    /* Peripheral clocks */
    Clk PERIPHCLK[];
};
