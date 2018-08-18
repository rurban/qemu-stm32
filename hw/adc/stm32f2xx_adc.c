/*
 * STM32F2XX ADC
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
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/adc/stm32f2xx_adc.h"

#ifndef STM_ADC_ERR_DEBUG
#define STM_ADC_ERR_DEBUG 0
#endif

#define  ADC_CR1_AWDCH                       ((uint32_t)0x0000001F)        /*!<AWDCH[4:0] bits (Analog watchdog channel select bits) */
#define  ADC_CR1_AWDCH_0                     ((uint32_t)0x00000001)        /*!<Bit 0 */
#define  ADC_CR1_AWDCH_1                     ((uint32_t)0x00000002)        /*!<Bit 1 */
#define  ADC_CR1_AWDCH_2                     ((uint32_t)0x00000004)        /*!<Bit 2 */
#define  ADC_CR1_AWDCH_3                     ((uint32_t)0x00000008)        /*!<Bit 3 */
#define  ADC_CR1_AWDCH_4                     ((uint32_t)0x00000010)        /*!<Bit 4 */
#define  ADC_CR1_EOCIE                       ((uint32_t)0x00000020)        /*!<Interrupt enable for EOC                              */
#define  ADC_CR1_AWDIE                       ((uint32_t)0x00000040)        /*!<AAnalog Watchdog interrupt enable                     */
#define  ADC_CR1_JEOCIE                      ((uint32_t)0x00000080)        /*!<Interrupt enable for injected channels                */
#define  ADC_CR1_SCAN                        ((uint32_t)0x00000100)        /*!<Scan mode                                             */
#define  ADC_CR1_AWDSGL                      ((uint32_t)0x00000200)        /*!<Enable the watchdog on a single channel in scan mode  */
#define  ADC_CR1_JAUTO                       ((uint32_t)0x00000400)        /*!<Automatic injected group conversion                   */
#define  ADC_CR1_DISCEN                      ((uint32_t)0x00000800)        /*!<Discontinuous mode on regular channels                */
#define  ADC_CR1_JDISCEN                     ((uint32_t)0x00001000)        /*!<Discontinuous mode on injected channels               */
#define  ADC_CR1_DISCNUM                     ((uint32_t)0x0000E000)        /*!<DISCNUM[2:0] bits (Discontinuous mode channel count)  */
#define  ADC_CR1_DISCNUM_0                   ((uint32_t)0x00002000)        /*!<Bit 0 */
#define  ADC_CR1_DISCNUM_1                   ((uint32_t)0x00004000)        /*!<Bit 1 */
#define  ADC_CR1_DISCNUM_2                   ((uint32_t)0x00008000)        /*!<Bit 2 */
#define  ADC_CR1_JAWDEN                      ((uint32_t)0x00400000)        /*!<Analog watchdog enable on injected channels           */
#define  ADC_CR1_AWDEN                       ((uint32_t)0x00800000)        /*!<Analog watchdog enable on regular channels            */
#define  ADC_CR1_RES                         ((uint32_t)0x03000000)        /*!<RES[2:0] bits (Resolution)                            */
#define  ADC_CR1_RES_0                       ((uint32_t)0x01000000)        /*!<Bit 0 */
#define  ADC_CR1_RES_1                       ((uint32_t)0x02000000)        /*!<Bit 1 */
#define  ADC_CR1_OVRIE                       ((uint32_t)0x04000000)         /*!<overrun interrupt enable                              */
  
/*******************  Bit definition for ADC_CR2 register  ********************/
#define  ADC_CR2_ADON                        ((uint32_t)0x00000001)        /*!<A/D Converter ON / OFF             */
#define  ADC_CR2_CONT                        ((uint32_t)0x00000002)        /*!<Continuous Conversion              */
#define  ADC_CR2_DMA                         ((uint32_t)0x00000100)        /*!<Direct Memory access mode          */
#define  ADC_CR2_DDS                         ((uint32_t)0x00000200)        /*!<DMA disable selection (Single ADC) */
#define  ADC_CR2_EOCS                        ((uint32_t)0x00000400)        /*!<End of conversion selection        */
#define  ADC_CR2_ALIGN                       ((uint32_t)0x00000800)        /*!<Data Alignment                     */
#define  ADC_CR2_JEXTSEL                     ((uint32_t)0x000F0000)        /*!<JEXTSEL[3:0] bits (External event select for injected group) */
#define  ADC_CR2_JEXTSEL_0                   ((uint32_t)0x00010000)        /*!<Bit 0 */
#define  ADC_CR2_JEXTSEL_1                   ((uint32_t)0x00020000)        /*!<Bit 1 */
#define  ADC_CR2_JEXTSEL_2                   ((uint32_t)0x00040000)        /*!<Bit 2 */
#define  ADC_CR2_JEXTSEL_3                   ((uint32_t)0x00080000)        /*!<Bit 3 */
#define  ADC_CR2_JEXTEN                      ((uint32_t)0x00300000)        /*!<JEXTEN[1:0] bits (External Trigger Conversion mode for injected channelsp) */
#define  ADC_CR2_JEXTEN_0                    ((uint32_t)0x00100000)        /*!<Bit 0 */
#define  ADC_CR2_JEXTEN_1                    ((uint32_t)0x00200000)        /*!<Bit 1 */
#define  ADC_CR2_JSWSTART                    ((uint32_t)0x00400000)        /*!<Start Conversion of injected channels */
#define  ADC_CR2_EXTSEL                      ((uint32_t)0x0F000000)        /*!<EXTSEL[3:0] bits (External Event Select for regular group) */
#define  ADC_CR2_EXTSEL_0                    ((uint32_t)0x01000000)        /*!<Bit 0 */
#define  ADC_CR2_EXTSEL_1                    ((uint32_t)0x02000000)        /*!<Bit 1 */
#define  ADC_CR2_EXTSEL_2                    ((uint32_t)0x04000000)        /*!<Bit 2 */
#define  ADC_CR2_EXTSEL_3                    ((uint32_t)0x08000000)        /*!<Bit 3 */
#define  ADC_CR2_EXTEN                       ((uint32_t)0x30000000)        /*!<EXTEN[1:0] bits (External Trigger Conversion mode for regular channelsp) */
#define  ADC_CR2_EXTEN_0                     ((uint32_t)0x10000000)        /*!<Bit 0 */
#define  ADC_CR2_EXTEN_1                     ((uint32_t)0x20000000)        /*!<Bit 1 */
#define  ADC_CR2_SWSTART                     ((uint32_t)0x40000000)        /*!<Start Conversion of regular channels */


#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_ADC_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void stm32f2xx_adc_reset(DeviceState *dev)
{
    STM32F2XXADCState *s = STM32F2XX_ADC(dev);

    s->adc_sr = 0x00000000;
    s->adc_cr1 = 0x00000000;
    s->adc_cr2 = 0x00000000;
    s->adc_smpr1 = 0x00000000;
    s->adc_smpr2 = 0x00000000;
    s->adc_jofr[0] = 0x00000000;
    s->adc_jofr[1] = 0x00000000;
    s->adc_jofr[2] = 0x00000000;
    s->adc_jofr[3] = 0x00000000;
    s->adc_htr = 0x00000FFF;
    s->adc_ltr = 0x00000000;
    s->adc_sqr1 = 0x00000000;
    s->adc_sqr2 = 0x00000000;
    s->adc_sqr3 = 0x00000000;
    s->adc_jsqr = 0x00000000;
    s->adc_jdr[0] = 0x00000000;
    s->adc_jdr[1] = 0x00000000;
    s->adc_jdr[2] = 0x00000000;
    s->adc_jdr[3] = 0x00000000;
    s->adc_dr = 0x00000000;
}

#define  ADC_CR1_RES                         ((uint32_t)0x03000000)        /*!<RES[2:0] bits (Resolution)                            */

static uint32_t stm32f2xx_adc_generate_value(STM32F2XXADCState *s)
{
    /* Attempts to fake some ADC values */
    s->adc_dr = s->adc_dr + 7;

    switch ((s->adc_cr1 & ADC_CR1_RES) >> 24) {
    case 0:
        /* 12-bit */
        s->adc_dr &= 0xFFF;
        break;
    case 1:
        /* 10-bit */
        s->adc_dr &= 0x3FF;
        break;
    case 2:
        /* 8-bit */
        s->adc_dr &= 0xFF;
        break;
    default:
        /* 6-bit */
        s->adc_dr &= 0x3F;
    }

    if (s->adc_cr2 & ADC_CR2_ALIGN) {
        return (s->adc_dr << 1) & 0xFFF0;
    } else {
        return s->adc_dr;
    }
}

static uint64_t stm32f2xx_adc_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    STM32F2XXADCState *s = opaque;

    DB_PRINT("Address: 0x%" HWADDR_PRIx "\n", addr);

    if (addr >= ADC_COMMON_ADDRESS) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: ADC Common Register Unsupported\n", __func__);
    }

    switch (addr) {
    case ADC_SR:
        return s->adc_sr;
    case ADC_CR1:
        return s->adc_cr1;
    case ADC_CR2:
        return s->adc_cr2 & 0xFFFFFFF;
    case ADC_SMPR1:
        return s->adc_smpr1;
    case ADC_SMPR2:
        return s->adc_smpr2;
    case ADC_JOFR1:
    case ADC_JOFR2:
    case ADC_JOFR3:
    case ADC_JOFR4:
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Injection ADC is not implemented, the registers are " \
                      "included for compatibility\n", __func__);
        return s->adc_jofr[(addr - ADC_JOFR1) / 4];
    case ADC_HTR:
        return s->adc_htr;
    case ADC_LTR:
        return s->adc_ltr;
    case ADC_SQR1:
        return s->adc_sqr1;
    case ADC_SQR2:
        return s->adc_sqr2;
    case ADC_SQR3:
        return s->adc_sqr3;
    case ADC_JSQR:
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Injection ADC is not implemented, the registers are " \
                      "included for compatibility\n", __func__);
        return s->adc_jsqr;
    case ADC_JDR1:
    case ADC_JDR2:
    case ADC_JDR3:
    case ADC_JDR4:
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Injection ADC is not implemented, the registers are " \
                      "included for compatibility\n", __func__);
        return s->adc_jdr[(addr - ADC_JDR1) / 4] -
               s->adc_jofr[(addr - ADC_JDR1) / 4];
    case ADC_DR:
        if ((s->adc_cr2 & ADC_CR2_ADON) && (s->adc_cr2 & ADC_CR2_SWSTART)) {
            s->adc_cr2 ^= ADC_CR2_SWSTART;
            return stm32f2xx_adc_generate_value(s);
        } else {
            return 0;
        }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }

    return 0;
}

static void stm32f2xx_adc_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    STM32F2XXADCState *s = opaque;
    uint32_t value = (uint32_t) val64;

    DB_PRINT("Address: 0x%" HWADDR_PRIx ", Value: 0x%x\n",
             addr, value);

    if (addr >= 0x100) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: ADC Common Register Unsupported\n", __func__);
    }

    switch (addr) {
    case ADC_SR:
        s->adc_sr &= (value & 0x3F);
        break;
    case ADC_CR1:
        s->adc_cr1 = value;
        break;
    case ADC_CR2:
        s->adc_cr2 = value;
        break;
    case ADC_SMPR1:
        s->adc_smpr1 = value;
        break;
    case ADC_SMPR2:
        s->adc_smpr2 = value;
        break;
    case ADC_JOFR1:
    case ADC_JOFR2:
    case ADC_JOFR3:
    case ADC_JOFR4:
        s->adc_jofr[(addr - ADC_JOFR1) / 4] = (value & 0xFFF);
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Injection ADC is not implemented, the registers are " \
                      "included for compatibility\n", __func__);
        break;
    case ADC_HTR:
        s->adc_htr = value;
        break;
    case ADC_LTR:
        s->adc_ltr = value;
        break;
    case ADC_SQR1:
        s->adc_sqr1 = value;
        break;
    case ADC_SQR2:
        s->adc_sqr2 = value;
        break;
    case ADC_SQR3:
        s->adc_sqr3 = value;
        break;
    case ADC_JSQR:
        s->adc_jsqr = value;
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Injection ADC is not implemented, the registers are " \
                      "included for compatibility\n", __func__);
        break;
    case ADC_JDR1:
    case ADC_JDR2:
    case ADC_JDR3:
    case ADC_JDR4:
        s->adc_jdr[(addr - ADC_JDR1) / 4] = value;
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Injection ADC is not implemented, the registers are " \
                      "included for compatibility\n", __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f2xx_adc_ops = {
    .read = stm32f2xx_adc_read,
    .write = stm32f2xx_adc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stm32f2xx_adc = {
    .name = TYPE_STM32F2XX_ADC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(adc_sr, STM32F2XXADCState),
        VMSTATE_UINT32(adc_cr1, STM32F2XXADCState),
        VMSTATE_UINT32(adc_cr2, STM32F2XXADCState),
        VMSTATE_UINT32(adc_smpr1, STM32F2XXADCState),
        VMSTATE_UINT32(adc_smpr2, STM32F2XXADCState),
        VMSTATE_UINT32_ARRAY(adc_jofr, STM32F2XXADCState, 4),
        VMSTATE_UINT32(adc_htr, STM32F2XXADCState),
        VMSTATE_UINT32(adc_ltr, STM32F2XXADCState),
        VMSTATE_UINT32(adc_sqr1, STM32F2XXADCState),
        VMSTATE_UINT32(adc_sqr2, STM32F2XXADCState),
        VMSTATE_UINT32(adc_sqr3, STM32F2XXADCState),
        VMSTATE_UINT32(adc_jsqr, STM32F2XXADCState),
        VMSTATE_UINT32_ARRAY(adc_jdr, STM32F2XXADCState, 4),
        VMSTATE_UINT32(adc_dr, STM32F2XXADCState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32f2xx_adc_init(Object *obj)
{
    STM32F2XXADCState *s = STM32F2XX_ADC(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32f2xx_adc_ops, s,
                          TYPE_STM32F2XX_ADC, 0xFF);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32f2xx_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f2xx_adc_reset;
    dc->vmsd = &vmstate_stm32f2xx_adc;
}

static const TypeInfo stm32f2xx_adc_info = {
    .name          = TYPE_STM32F2XX_ADC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F2XXADCState),
    .instance_init = stm32f2xx_adc_init,
    .class_init    = stm32f2xx_adc_class_init,
};

static void stm32f2xx_adc_register_types(void)
{
    type_register_static(&stm32f2xx_adc_info);
}

type_init(stm32f2xx_adc_register_types)
