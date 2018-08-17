/*
 * STM32F1XX USART
 *
 * Copyright (c) 2018 Martin Schröder <mkschreder.uk@gmail.com>
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
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "hw/hw.h"

#define USART_SR   0x00
#define USART_DR   0x04
#define USART_BRR  0x08
#define USART_CR1  0x0C
#define USART_CR2  0x10
#define USART_CR3  0x14
#define USART_GTPR 0x18

enum {
    USART_SR_CTS       = (1 << 9),
    USART_SR_LBD       = (1 << 8),
    USART_SR_TXE       = (1 << 7),
    USART_SR_TC        = (1 << 6),
    USART_SR_RXNE      = (1 << 5),
    USART_SR_IDLE      = (1 << 4),
    USART_SR_ORE       = (1 << 3),
    USART_SR_NE        = (1 << 2),
    USART_SR_FE        = (1 << 1),
    USART_SR_PE        = (1 << 0)
};

enum {
    USART_CR1_UE = (1 << 13),
    USART_CR1_M = (1 << 12),
    USART_CR1_WAKE = (1 << 11),
    USART_CR1_PCE = (1 << 10),
    USART_CR1_PS = (1 << 9),
    USART_CR1_PSIE = (1 << 8),
    USART_CR1_TXEIE = (1 << 7),
    USART_CR1_TCIE = (1 << 6),
    USART_CR1_RXNEIE = (1 << 5),
    USART_CR1_IDLEIE = (1 << 4),
    USART_CR1_TE = (1 << 3),
    USART_CR1_RE = (1 << 2),
    USART_CR1_RWU = (1 << 1),
    USART_CR1_SBK = (1 << 0)
};

#define TYPE_STM32F2XX_USART "stm32f2xx-usart"
#define STM32F2XX_USART(obj) \
    OBJECT_CHECK(struct stm32f1xx_usart, (obj), TYPE_STM32F2XX_USART)

struct stm32f1xx_usart {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint32_t usart_sr;
    uint32_t usart_rdr, usart_tdr;
    uint32_t usart_brr;
    uint32_t usart_cr1;
    uint32_t usart_cr2;
    uint32_t usart_cr3;
    uint32_t usart_gtpr;

    CharBackend chr;
    qemu_irq irq;

    int irq_level;
};

#ifndef STM_USART_ERR_DEBUG
#define STM_USART_ERR_DEBUG 1
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_USART_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void _update_irq(struct stm32f1xx_usart *s){
    int level = 
        ((s->usart_cr1 & USART_CR1_RXNEIE) && (s->usart_sr & (USART_SR_RXNE | USART_SR_ORE))) ||
        ((s->usart_cr1 & USART_CR1_TXEIE) && (s->usart_sr & USART_SR_TXE)) ||
        ((s->usart_cr1 & USART_CR1_TCIE) && (s->usart_sr & USART_SR_TC));

    if(s->irq_level != level){
        if(level) {
            printf("irq on\n");
        } else {
            printf("irq off\n");
        }
        qemu_set_irq(s->irq, level);
        s->irq_level = level;
    }
}

static int stm32f2xx_usart_can_receive(void *opaque)
{
    struct stm32f1xx_usart *s = opaque;

    if (!(s->usart_sr & USART_SR_RXNE)) {
        return 1;
    }

    return 0;
}

static void stm32f2xx_usart_receive(void *opaque, const uint8_t *buf, int size)
{
    struct stm32f1xx_usart *s = opaque;

    s->usart_rdr = *buf;

    if (!(s->usart_cr1 & USART_CR1_UE && s->usart_cr1 & USART_CR1_RE)) {
        /* USART not enabled - drop the chars */
        DB_PRINT("Dropping the chars\n");
        return;
    }

    s->usart_sr |= USART_SR_RXNE;
    _update_irq(s);
}

static void stm32f2xx_usart_reset(DeviceState *dev)
{
    struct stm32f1xx_usart *s = STM32F2XX_USART(dev);

    s->usart_sr = 0x000000C0;
    s->usart_rdr = 0x00000000;
    s->usart_tdr = 0x00000000;
    s->usart_brr = 0x00000000;
    s->usart_cr1 = 0x00000000;
    s->usart_cr2 = 0x00000000;
    s->usart_cr3 = 0x00000000;
    s->usart_gtpr = 0x00000000;

    qemu_set_irq(s->irq, 0);
}

static uint64_t stm32f2xx_usart_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    struct stm32f1xx_usart *s = opaque;

    switch (addr) {
    case USART_SR:
        DB_PRINT("RD: SR: 0x%08x\n", s->usart_sr);
        qemu_chr_fe_accept_input(&s->chr);
        _update_irq(s);
        return s->usart_sr;
    case USART_DR:
        DB_PRINT("RD: DR: 0x%" PRIx32 ", %c\n", s->usart_rdr, (char) s->usart_rdr);
        s->usart_sr &= ~USART_SR_RXNE;
        s->usart_sr &= ~USART_SR_TC;
        qemu_chr_fe_accept_input(&s->chr);
        return s->usart_rdr & 0x3FF;
    case USART_BRR:
        DB_PRINT("RD: BRR: 0x%08x\n", s->usart_brr);
        return s->usart_brr;
    case USART_CR1:
        DB_PRINT("RD: CR1: 0x%08x\n", s->usart_cr1);
        return s->usart_cr1;
    case USART_CR2:
        DB_PRINT("RD: CR2: 0x%08x\n", s->usart_cr2);
        return s->usart_cr2;
    case USART_CR3:
        DB_PRINT("RD: CR3: 0x%08x\n", s->usart_cr3);
        return s->usart_cr3;
    case USART_GTPR:
        return s->usart_gtpr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    return 0;
}

static void stm32f2xx_usart_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    struct stm32f1xx_usart *s = opaque;
    uint32_t value = val64;
    unsigned char ch;

    switch (addr) {
    case USART_SR: {
        uint32_t mask = (USART_SR_CTS | USART_SR_LBD | USART_SR_TC | USART_SR_RXNE);
        s->usart_sr = (s->usart_sr & ~mask) | (s->usart_sr & value & mask);
        DB_PRINT("WR: SR 0x%08x -> 0x%08x\n", value, s->usart_sr);
        _update_irq(s);
        return;
    }
    case USART_DR:
        DB_PRINT("WR: DR 0x%08x\n", value);
        if (value < 0xF000) {
            s->usart_tdr = value;
            ch = value;
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);

            // set transmission complete and tx empty
            s->usart_sr |= USART_SR_TXE;
            s->usart_sr |= USART_SR_TC;
        }
        return;
    case USART_BRR:
        DB_PRINT("WR: BRR 0x%08x\n", value);
        s->usart_brr = value;
        return;
    case USART_CR1:
        DB_PRINT("WR: CR1 0x%08x\n", value);
        s->usart_cr1 = value;
        _update_irq(s);
        return;
    case USART_CR2:
        DB_PRINT("WR: CR2 0x%08x\n", value);
        s->usart_cr2 = value;
        return;
    case USART_CR3:
        DB_PRINT("WR: CR3 0x%08x\n", value);
        s->usart_cr3 = value;
        return;
    case USART_GTPR:
        DB_PRINT("WR: GTPRR 0x%08x\n", value);
        s->usart_gtpr = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f2xx_usart_ops = {
    .read = stm32f2xx_usart_read,
    .write = stm32f2xx_usart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static Property stm32f2xx_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", struct stm32f1xx_usart, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f2xx_usart_init(Object *obj)
{
    struct stm32f1xx_usart *s = STM32F2XX_USART(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32f2xx_usart_ops, s,
                          TYPE_STM32F2XX_USART, 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32f2xx_usart_realize(DeviceState *dev, Error **errp)
{
    struct stm32f1xx_usart *s = STM32F2XX_USART(dev);

    qemu_chr_fe_set_handlers(&s->chr, stm32f2xx_usart_can_receive,
                             stm32f2xx_usart_receive, NULL, s, NULL, true);
}

static void stm32f2xx_usart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f2xx_usart_reset;
    dc->props = stm32f2xx_usart_properties;
    dc->realize = stm32f2xx_usart_realize;
}

static const TypeInfo stm32f2xx_usart_info = {
    .name          = TYPE_STM32F2XX_USART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32f1xx_usart),
    .instance_init = stm32f2xx_usart_init,
    .class_init    = stm32f2xx_usart_class_init,
};

static void stm32f2xx_usart_register_types(void)
{
    type_register_static(&stm32f2xx_usart_info);
}

type_init(stm32f2xx_usart_register_types)
