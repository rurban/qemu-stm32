/*
 * ARMv7M DWT registers
 *
 * Copyright (c) 2018 Martin Schröder <mkschreder.uk@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
#include "qemu-common.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "hw/arm/arm.h"

enum {
    DWT_REG_CTRL,
    DWT_REG_CYCNT,
    DWT_REG_CPICNT,
    DWT_REG_EXCCNT,
    DWT_REG_SLEEPCNT,
    DWT_REG_LSUCNT,
    DWT_REG_FOLDCNT,
    DWT_REG_PCSR,
    DWT_REG_COMP0,
    DWT_REG_MASK0, 
    DWT_REG_FUNCTION0,
    DWT_REG_COMP1,
    DWT_REG_MASK1,
    DWT_REG_FUNCTION1,
    DWT_REG_COMP2,
    DWT_REG_MASK2,
    DWT_REG_FUNCTION2,
    DWT_REG_COMP3,
    DWT_REG_MASK3,
    DWT_REG_FUNCTION3,
};

struct armv7m_dwt {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

	uint32_t CTRL;                    /*!< Offset: 0x000 (R/W)  Control Register                          */
	uint32_t CYCCNT;                  /*!< Offset: 0x004 (R/W)  Cycle Count Register                      */
	uint32_t CPICNT;                  /*!< Offset: 0x008 (R/W)  CPI Count Register                        */
	uint32_t EXCCNT;                  /*!< Offset: 0x00C (R/W)  Exception Overhead Count Register         */
	uint32_t SLEEPCNT;                /*!< Offset: 0x010 (R/W)  Sleep Count Register                      */
	uint32_t LSUCNT;                  /*!< Offset: 0x014 (R/W)  LSU Count Register                        */
	uint32_t FOLDCNT;                 /*!< Offset: 0x018 (R/W)  Folded-instruction Count Register         */
	uint32_t PCSR;                    /*!< Offset: 0x01C (R/ )  Program Counter Sample Register           */
	uint32_t COMP0;                   /*!< Offset: 0x020 (R/W)  Comparator Register 0                     */
	uint32_t MASK0;                   /*!< Offset: 0x024 (R/W)  Mask Register 0                           */
	uint32_t FUNCTION0;               /*!< Offset: 0x028 (R/W)  Function Register 0                       */
	uint32_t RESERVED0[1];           
	uint32_t COMP1;                   /*!< Offset: 0x030 (R/W)  Comparator Register 1                     */
	uint32_t MASK1;                   /*!< Offset: 0x034 (R/W)  Mask Register 1                           */
	uint32_t FUNCTION1;               /*!< Offset: 0x038 (R/W)  Function Register 1                       */
	uint32_t RESERVED1[1];           
	uint32_t COMP2;                   /*!< Offset: 0x040 (R/W)  Comparator Register 2                     */
	uint32_t MASK2;                   /*!< Offset: 0x044 (R/W)  Mask Register 2                           */
	uint32_t FUNCTION2;               /*!< Offset: 0x048 (R/W)  Function Register 2                       */
	uint32_t RESERVED2[1];           
	uint32_t COMP3;                   /*!< Offset: 0x050 (R/W)  Comparator Register 3                     */
	uint32_t MASK3;                   /*!< Offset: 0x054 (R/W)  Mask Register 3                           */
	uint32_t FUNCTION3;               /*!< Offset: 0x058 (R/W)  Function Register 3                       */
};

static uint64_t armv7m_dwt_read(void *opaque, hwaddr addr, unsigned size) {
	struct armv7m_dwt *self = (struct armv7m_dwt*)opaque;

	switch(addr >> 2){
		case DWT_REG_CYCNT: {
            int64_t t = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            self->CYCCNT = t / system_clock_scale;
            return self->CYCCNT;
        } break;
        default: 
            qemu_log_mask(LOG_GUEST_ERROR,
                "armv7m_dwt: Bad read offset 0x%" HWADDR_PRIx "\n", addr);
            return 0;
    }
    return 0;
}

static void armv7m_dwt_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned size) {
	qemu_log_mask(LOG_GUEST_ERROR,
           "armv7m_dwt: Bad write offset 0x%" HWADDR_PRIx "\n", addr);
}

static const MemoryRegionOps armv7m_dwt_ops = {
    .read = armv7m_dwt_read,
    .write = armv7m_dwt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void armv7m_dwt_reset(DeviceState *dev) {
//	struct armv7m_dwt *self = OBJECT_CHECK(struct armv7m_dwt, dev, "armv7m-dwt");
    
}

static void armv7m_dwt_instance_init(Object *obj)
{
    struct armv7m_dwt *self = OBJECT_CHECK(struct armv7m_dwt, obj, "armv7m-dwt");
    memory_region_init_io(&self->iomem, obj, &armv7m_dwt_ops, self, "armv7m_dwt", 0x5c);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &self->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &self->irq);
}

static const VMStateDescription vmstate_armv7m_dwt = {
    .name = "armv7m_dwt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void armv7m_dwt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->vmsd = &vmstate_armv7m_dwt;
    dc->reset = armv7m_dwt_reset;
}

static const TypeInfo armv7m_armv7m_dwt_info = {
    .name = "armv7m-dwt",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = armv7m_dwt_instance_init,
    .instance_size = sizeof(struct armv7m_dwt),
    .class_init = armv7m_dwt_class_init,
};

static void armv7m_dwt_register_types(void)
{
    type_register_static(&armv7m_armv7m_dwt_info);
}

type_init(armv7m_dwt_register_types)
