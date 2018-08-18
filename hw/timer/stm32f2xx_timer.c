/*
 * STM32F2XX Timer
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
#include "hw/timer/stm32f2xx_timer.h"
#include "qemu/log.h"

#ifndef STM_TIMER_ERR_DEBUG
#define STM_TIMER_ERR_DEBUG 0
#endif

/*******************  Bit definition for TIM_CR1 register  ********************/
#define  TIM_CR1_CEN                         ((uint16_t)0x0001)            /*!<Counter enable        */
#define  TIM_CR1_UDIS                        ((uint16_t)0x0002)            /*!<Update disable        */
#define  TIM_CR1_URS                         ((uint16_t)0x0004)            /*!<Update request source */
#define  TIM_CR1_OPM                         ((uint16_t)0x0008)            /*!<One pulse mode        */
#define  TIM_CR1_DIR                         ((uint16_t)0x0010)            /*!<Direction             */

#define  TIM_CR1_CMS                         ((uint16_t)0x0060)            /*!<CMS[1:0] bits (Center-aligned mode selection) */
#define  TIM_CR1_CMS_0                       ((uint16_t)0x0020)            /*!<Bit 0 */
#define  TIM_CR1_CMS_1                       ((uint16_t)0x0040)            /*!<Bit 1 */

#define  TIM_CR1_ARPE                        ((uint16_t)0x0080)            /*!<Auto-reload preload enable     */

#define  TIM_CR1_CKD                         ((uint16_t)0x0300)            /*!<CKD[1:0] bits (clock division) */
#define  TIM_CR1_CKD_0                       ((uint16_t)0x0100)            /*!<Bit 0 */
#define  TIM_CR1_CKD_1                       ((uint16_t)0x0200)            /*!<Bit 1 */

/*******************  Bit definition for TIM_CR2 register  ********************/
#define  TIM_CR2_CCPC                        ((uint16_t)0x0001)            /*!<Capture/Compare Preloaded Control        */
#define  TIM_CR2_CCUS                        ((uint16_t)0x0004)            /*!<Capture/Compare Control Update Selection */
#define  TIM_CR2_CCDS                        ((uint16_t)0x0008)            /*!<Capture/Compare DMA Selection            */

#define  TIM_CR2_MMS                         ((uint16_t)0x0070)            /*!<MMS[2:0] bits (Master Mode Selection) */
#define  TIM_CR2_MMS_0                       ((uint16_t)0x0010)            /*!<Bit 0 */
#define  TIM_CR2_MMS_1                       ((uint16_t)0x0020)            /*!<Bit 1 */
#define  TIM_CR2_MMS_2                       ((uint16_t)0x0040)            /*!<Bit 2 */

#define  TIM_CR2_TI1S                        ((uint16_t)0x0080)            /*!<TI1 Selection */
#define  TIM_CR2_OIS1                        ((uint16_t)0x0100)            /*!<Output Idle state 1 (OC1 output)  */
#define  TIM_CR2_OIS1N                       ((uint16_t)0x0200)            /*!<Output Idle state 1 (OC1N output) */
#define  TIM_CR2_OIS2                        ((uint16_t)0x0400)            /*!<Output Idle state 2 (OC2 output)  */
#define  TIM_CR2_OIS2N                       ((uint16_t)0x0800)            /*!<Output Idle state 2 (OC2N output) */
#define  TIM_CR2_OIS3                        ((uint16_t)0x1000)            /*!<Output Idle state 3 (OC3 output)  */
#define  TIM_CR2_OIS3N                       ((uint16_t)0x2000)            /*!<Output Idle state 3 (OC3N output) */
#define  TIM_CR2_OIS4                        ((uint16_t)0x4000)            /*!<Output Idle state 4 (OC4 output)  */

/*******************  Bit definition for TIM_DIER register  *******************/
#define  TIM_DIER_UIE                        ((uint16_t)0x0001)            /*!<Update interrupt enable */
#define  TIM_DIER_CC1IE                      ((uint16_t)0x0002)            /*!<Capture/Compare 1 interrupt enable   */
#define  TIM_DIER_CC2IE                      ((uint16_t)0x0004)            /*!<Capture/Compare 2 interrupt enable   */
#define  TIM_DIER_CC3IE                      ((uint16_t)0x0008)            /*!<Capture/Compare 3 interrupt enable   */
#define  TIM_DIER_CC4IE                      ((uint16_t)0x0010)            /*!<Capture/Compare 4 interrupt enable   */
#define  TIM_DIER_COMIE                      ((uint16_t)0x0020)            /*!<COM interrupt enable                 */
#define  TIM_DIER_TIE                        ((uint16_t)0x0040)            /*!<Trigger interrupt enable             */
#define  TIM_DIER_BIE                        ((uint16_t)0x0080)            /*!<Break interrupt enable               */
#define  TIM_DIER_UDE                        ((uint16_t)0x0100)            /*!<Update DMA request enable            */
#define  TIM_DIER_CC1DE                      ((uint16_t)0x0200)            /*!<Capture/Compare 1 DMA request enable */
#define  TIM_DIER_CC2DE                      ((uint16_t)0x0400)            /*!<Capture/Compare 2 DMA request enable */
#define  TIM_DIER_CC3DE                      ((uint16_t)0x0800)            /*!<Capture/Compare 3 DMA request enable */
#define  TIM_DIER_CC4DE                      ((uint16_t)0x1000)            /*!<Capture/Compare 4 DMA request enable */
#define  TIM_DIER_COMDE                      ((uint16_t)0x2000)            /*!<COM DMA request enable               */
#define  TIM_DIER_TDE                        ((uint16_t)0x4000)            /*!<Trigger DMA request enable           */

/********************  Bit definition for TIM_SR register  ********************/
#define  TIM_SR_UIF                          ((uint16_t)0x0001)            /*!<Update interrupt Flag              */
#define  TIM_SR_CC1IF                        ((uint16_t)0x0002)            /*!<Capture/Compare 1 interrupt Flag   */
#define  TIM_SR_CC2IF                        ((uint16_t)0x0004)            /*!<Capture/Compare 2 interrupt Flag   */
#define  TIM_SR_CC3IF                        ((uint16_t)0x0008)            /*!<Capture/Compare 3 interrupt Flag   */
#define  TIM_SR_CC4IF                        ((uint16_t)0x0010)            /*!<Capture/Compare 4 interrupt Flag   */
#define  TIM_SR_COMIF                        ((uint16_t)0x0020)            /*!<COM interrupt Flag                 */
#define  TIM_SR_TIF                          ((uint16_t)0x0040)            /*!<Trigger interrupt Flag             */
#define  TIM_SR_BIF                          ((uint16_t)0x0080)            /*!<Break interrupt Flag               */
#define  TIM_SR_CC1OF                        ((uint16_t)0x0200)            /*!<Capture/Compare 1 Overcapture Flag */
#define  TIM_SR_CC2OF                        ((uint16_t)0x0400)            /*!<Capture/Compare 2 Overcapture Flag */
#define  TIM_SR_CC3OF                        ((uint16_t)0x0800)            /*!<Capture/Compare 3 Overcapture Flag */
#define  TIM_SR_CC4OF                        ((uint16_t)0x1000)            /*!<Capture/Compare 4 Overcapture Flag */

/*******************  Bit definition for TIM_EGR register  ********************/
#define  TIM_EGR_UG                          ((uint8_t)0x01)               /*!<Update Generation                         */
#define  TIM_EGR_CC1G                        ((uint8_t)0x02)               /*!<Capture/Compare 1 Generation              */
#define  TIM_EGR_CC2G                        ((uint8_t)0x04)               /*!<Capture/Compare 2 Generation              */
#define  TIM_EGR_CC3G                        ((uint8_t)0x08)               /*!<Capture/Compare 3 Generation              */
#define  TIM_EGR_CC4G                        ((uint8_t)0x10)               /*!<Capture/Compare 4 Generation              */
#define  TIM_EGR_COMG                        ((uint8_t)0x20)               /*!<Capture/Compare Control Update Generation */
#define  TIM_EGR_TG                          ((uint8_t)0x40)               /*!<Trigger Generation                        */
#define  TIM_EGR_BG                          ((uint8_t)0x80)               /*!<Break Generation                          */

/******************  Bit definition for TIM_CCMR1 register  *******************/
#define  TIM_CCMR1_CC1S                      ((uint16_t)0x0003)            /*!<CC1S[1:0] bits (Capture/Compare 1 Selection) */
#define  TIM_CCMR1_CC1S_0                    ((uint16_t)0x0001)            /*!<Bit 0 */
#define  TIM_CCMR1_CC1S_1                    ((uint16_t)0x0002)            /*!<Bit 1 */

#define  TIM_CCMR1_OC1FE                     ((uint16_t)0x0004)            /*!<Output Compare 1 Fast enable                 */
#define  TIM_CCMR1_OC1PE                     ((uint16_t)0x0008)            /*!<Output Compare 1 Preload enable              */

#define  TIM_CCMR1_OC1M                      ((uint16_t)0x0070)            /*!<OC1M[2:0] bits (Output Compare 1 Mode)       */
#define  TIM_CCMR1_OC1M_0                    ((uint16_t)0x0010)            /*!<Bit 0 */
#define  TIM_CCMR1_OC1M_1                    ((uint16_t)0x0020)            /*!<Bit 1 */
#define  TIM_CCMR1_OC1M_2                    ((uint16_t)0x0040)            /*!<Bit 2 */

#define  TIM_CCMR1_OC1CE                     ((uint16_t)0x0080)            /*!<Output Compare 1Clear Enable                 */

#define  TIM_CCMR1_CC2S                      ((uint16_t)0x0300)            /*!<CC2S[1:0] bits (Capture/Compare 2 Selection) */
#define  TIM_CCMR1_CC2S_0                    ((uint16_t)0x0100)            /*!<Bit 0 */
#define  TIM_CCMR1_CC2S_1                    ((uint16_t)0x0200)            /*!<Bit 1 */

#define  TIM_CCMR1_OC2FE                     ((uint16_t)0x0400)            /*!<Output Compare 2 Fast enable                 */
#define  TIM_CCMR1_OC2PE                     ((uint16_t)0x0800)            /*!<Output Compare 2 Preload enable              */

#define  TIM_CCMR1_OC2M                      ((uint16_t)0x7000)            /*!<OC2M[2:0] bits (Output Compare 2 Mode)       */
#define  TIM_CCMR1_OC2M_0                    ((uint16_t)0x1000)            /*!<Bit 0 */
#define  TIM_CCMR1_OC2M_1                    ((uint16_t)0x2000)            /*!<Bit 1 */
#define  TIM_CCMR1_OC2M_2                    ((uint16_t)0x4000)            /*!<Bit 2 */

#define  TIM_CCMR1_OC2CE                     ((uint16_t)0x8000)            /*!<Output Compare 2 Clear Enable */

/*----------------------------------------------------------------------------*/

#define  TIM_CCMR1_IC1PSC                    ((uint16_t)0x000C)            /*!<IC1PSC[1:0] bits (Input Capture 1 Prescaler) */
#define  TIM_CCMR1_IC1PSC_0                  ((uint16_t)0x0004)            /*!<Bit 0 */
#define  TIM_CCMR1_IC1PSC_1                  ((uint16_t)0x0008)            /*!<Bit 1 */

#define  TIM_CCMR1_IC1F                      ((uint16_t)0x00F0)            /*!<IC1F[3:0] bits (Input Capture 1 Filter)      */
#define  TIM_CCMR1_IC1F_0                    ((uint16_t)0x0010)            /*!<Bit 0 */
#define  TIM_CCMR1_IC1F_1                    ((uint16_t)0x0020)            /*!<Bit 1 */
#define  TIM_CCMR1_IC1F_2                    ((uint16_t)0x0040)            /*!<Bit 2 */
#define  TIM_CCMR1_IC1F_3                    ((uint16_t)0x0080)            /*!<Bit 3 */

#define  TIM_CCMR1_IC2PSC                    ((uint16_t)0x0C00)            /*!<IC2PSC[1:0] bits (Input Capture 2 Prescaler)  */
#define  TIM_CCMR1_IC2PSC_0                  ((uint16_t)0x0400)            /*!<Bit 0 */
#define  TIM_CCMR1_IC2PSC_1                  ((uint16_t)0x0800)            /*!<Bit 1 */

#define  TIM_CCMR1_IC2F                      ((uint16_t)0xF000)            /*!<IC2F[3:0] bits (Input Capture 2 Filter)       */
#define  TIM_CCMR1_IC2F_0                    ((uint16_t)0x1000)            /*!<Bit 0 */
#define  TIM_CCMR1_IC2F_1                    ((uint16_t)0x2000)            /*!<Bit 1 */
#define  TIM_CCMR1_IC2F_2                    ((uint16_t)0x4000)            /*!<Bit 2 */
#define  TIM_CCMR1_IC2F_3                    ((uint16_t)0x8000)            /*!<Bit 3 */

/******************  Bit definition for TIM_CCMR2 register  *******************/
#define  TIM_CCMR2_CC3S                      ((uint16_t)0x0003)            /*!<CC3S[1:0] bits (Capture/Compare 3 Selection)  */
#define  TIM_CCMR2_CC3S_0                    ((uint16_t)0x0001)            /*!<Bit 0 */
#define  TIM_CCMR2_CC3S_1                    ((uint16_t)0x0002)            /*!<Bit 1 */

#define  TIM_CCMR2_OC3FE                     ((uint16_t)0x0004)            /*!<Output Compare 3 Fast enable           */
#define  TIM_CCMR2_OC3PE                     ((uint16_t)0x0008)            /*!<Output Compare 3 Preload enable        */

#define  TIM_CCMR2_OC3M                      ((uint16_t)0x0070)            /*!<OC3M[2:0] bits (Output Compare 3 Mode) */
#define  TIM_CCMR2_OC3M_0                    ((uint16_t)0x0010)            /*!<Bit 0 */
#define  TIM_CCMR2_OC3M_1                    ((uint16_t)0x0020)            /*!<Bit 1 */
#define  TIM_CCMR2_OC3M_2                    ((uint16_t)0x0040)            /*!<Bit 2 */

#define  TIM_CCMR2_OC3CE                     ((uint16_t)0x0080)            /*!<Output Compare 3 Clear Enable */

#define  TIM_CCMR2_CC4S                      ((uint16_t)0x0300)            /*!<CC4S[1:0] bits (Capture/Compare 4 Selection) */
#define  TIM_CCMR2_CC4S_0                    ((uint16_t)0x0100)            /*!<Bit 0 */
#define  TIM_CCMR2_CC4S_1                    ((uint16_t)0x0200)            /*!<Bit 1 */

#define  TIM_CCMR2_OC4FE                     ((uint16_t)0x0400)            /*!<Output Compare 4 Fast enable    */
#define  TIM_CCMR2_OC4PE                     ((uint16_t)0x0800)            /*!<Output Compare 4 Preload enable */

#define  TIM_CCMR2_OC4M                      ((uint16_t)0x7000)            /*!<OC4M[2:0] bits (Output Compare 4 Mode) */
#define  TIM_CCMR2_OC4M_0                    ((uint16_t)0x1000)            /*!<Bit 0 */
#define  TIM_CCMR2_OC4M_1                    ((uint16_t)0x2000)            /*!<Bit 1 */
#define  TIM_CCMR2_OC4M_2                    ((uint16_t)0x4000)            /*!<Bit 2 */

#define  TIM_CCMR2_OC4CE                     ((uint16_t)0x8000)            /*!<Output Compare 4 Clear Enable */

/*----------------------------------------------------------------------------*/

#define  TIM_CCMR2_IC3PSC                    ((uint16_t)0x000C)            /*!<IC3PSC[1:0] bits (Input Capture 3 Prescaler) */
#define  TIM_CCMR2_IC3PSC_0                  ((uint16_t)0x0004)            /*!<Bit 0 */
#define  TIM_CCMR2_IC3PSC_1                  ((uint16_t)0x0008)            /*!<Bit 1 */

#define  TIM_CCMR2_IC3F                      ((uint16_t)0x00F0)            /*!<IC3F[3:0] bits (Input Capture 3 Filter) */
#define  TIM_CCMR2_IC3F_0                    ((uint16_t)0x0010)            /*!<Bit 0 */
#define  TIM_CCMR2_IC3F_1                    ((uint16_t)0x0020)            /*!<Bit 1 */
#define  TIM_CCMR2_IC3F_2                    ((uint16_t)0x0040)            /*!<Bit 2 */
#define  TIM_CCMR2_IC3F_3                    ((uint16_t)0x0080)            /*!<Bit 3 */

#define  TIM_CCMR2_IC4PSC                    ((uint16_t)0x0C00)            /*!<IC4PSC[1:0] bits (Input Capture 4 Prescaler) */
#define  TIM_CCMR2_IC4PSC_0                  ((uint16_t)0x0400)            /*!<Bit 0 */
#define  TIM_CCMR2_IC4PSC_1                  ((uint16_t)0x0800)            /*!<Bit 1 */

#define  TIM_CCMR2_IC4F                      ((uint16_t)0xF000)            /*!<IC4F[3:0] bits (Input Capture 4 Filter) */
#define  TIM_CCMR2_IC4F_0                    ((uint16_t)0x1000)            /*!<Bit 0 */
#define  TIM_CCMR2_IC4F_1                    ((uint16_t)0x2000)            /*!<Bit 1 */
#define  TIM_CCMR2_IC4F_2                    ((uint16_t)0x4000)            /*!<Bit 2 */
#define  TIM_CCMR2_IC4F_3                    ((uint16_t)0x8000)            /*!<Bit 3 */

/*******************  Bit definition for TIM_CCER register  *******************/
#define  TIM_CCER_CC1E                       ((uint16_t)0x0001)            /*!<Capture/Compare 1 output enable                 */
#define  TIM_CCER_CC1P                       ((uint16_t)0x0002)            /*!<Capture/Compare 1 output Polarity               */
#define  TIM_CCER_CC1NE                      ((uint16_t)0x0004)            /*!<Capture/Compare 1 Complementary output enable   */
#define  TIM_CCER_CC1NP                      ((uint16_t)0x0008)            /*!<Capture/Compare 1 Complementary output Polarity */
#define  TIM_CCER_CC2E                       ((uint16_t)0x0010)            /*!<Capture/Compare 2 output enable                 */
#define  TIM_CCER_CC2P                       ((uint16_t)0x0020)            /*!<Capture/Compare 2 output Polarity               */
#define  TIM_CCER_CC2NE                      ((uint16_t)0x0040)            /*!<Capture/Compare 2 Complementary output enable   */
#define  TIM_CCER_CC2NP                      ((uint16_t)0x0080)            /*!<Capture/Compare 2 Complementary output Polarity */
#define  TIM_CCER_CC3E                       ((uint16_t)0x0100)            /*!<Capture/Compare 3 output enable                 */
#define  TIM_CCER_CC3P                       ((uint16_t)0x0200)            /*!<Capture/Compare 3 output Polarity               */
#define  TIM_CCER_CC3NE                      ((uint16_t)0x0400)            /*!<Capture/Compare 3 Complementary output enable   */
#define  TIM_CCER_CC3NP                      ((uint16_t)0x0800)            /*!<Capture/Compare 3 Complementary output Polarity */
#define  TIM_CCER_CC4E                       ((uint16_t)0x1000)            /*!<Capture/Compare 4 output enable                 */
#define  TIM_CCER_CC4P                       ((uint16_t)0x2000)            /*!<Capture/Compare 4 output Polarity               */
#define  TIM_CCER_CC4NP                      ((uint16_t)0x8000)            /*!<Capture/Compare 4 Complementary output Polarity */



#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_TIMER_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void stm32f2xx_timer_set_alarm(STM32F2XXTimerState *s, int64_t now);

static void stm32f2xx_timer_interrupt(void *opaque)
{
    STM32F2XXTimerState *s = opaque;

    DB_PRINT("Interrupt\n");

    if (s->tim_dier & TIM_DIER_UIE && s->tim_cr1 & TIM_CR1_CEN) {
        s->tim_sr |= 1;
        qemu_irq_pulse(s->irq);
        stm32f2xx_timer_set_alarm(s, s->hit_time);
    }

    if (s->tim_ccmr1 & (TIM_CCMR1_OC2M2 | TIM_CCMR1_OC2M1) &&
        !(s->tim_ccmr1 & TIM_CCMR1_OC2M0) &&
        s->tim_ccmr1 & TIM_CCMR1_OC2PE &&
        s->tim_ccer & TIM_CCER_CC2E) {
        /* PWM 2 - Mode 1 */
        DB_PRINT("PWM2 Duty Cycle: %d%%\n",
                s->tim_ccr2 / (100 * (s->tim_psc + 1)));
    }
}

static inline int64_t stm32f2xx_ns_to_ticks(STM32F2XXTimerState *s, int64_t t)
{
    return muldiv64(t, s->freq_hz, 1000000000ULL) / (s->tim_psc + 1);
}

static void stm32f2xx_timer_set_alarm(STM32F2XXTimerState *s, int64_t now)
{
    uint64_t ticks;
    int64_t now_ticks;

    if (s->tim_arr == 0) {
        return;
    }

    DB_PRINT("Alarm set at: 0x%x\n", s->tim_cr1);

    now_ticks = stm32f2xx_ns_to_ticks(s, now);
    ticks = s->tim_arr - (now_ticks - s->tick_offset);

    DB_PRINT("Alarm set in %d ticks\n", (int) ticks);

    s->hit_time = muldiv64((ticks + (uint64_t) now_ticks) * (s->tim_psc + 1),
                               1000000000ULL, s->freq_hz);

    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->hit_time);
    DB_PRINT("Wait Time: %" PRId64 " ticks\n", s->hit_time);
}

static void stm32f2xx_timer_reset(DeviceState *dev)
{
    STM32F2XXTimerState *s = STM32F2XXTIMER(dev);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->tim_cr1 = 0;
    s->tim_cr2 = 0;
    s->tim_smcr = 0;
    s->tim_dier = 0;
    s->tim_sr = 0;
    s->tim_egr = 0;
    s->tim_ccmr1 = 0;
    s->tim_ccmr2 = 0;
    s->tim_ccer = 0;
    s->tim_psc = 0;
    s->tim_arr = 0;
    s->tim_ccr1 = 0;
    s->tim_ccr2 = 0;
    s->tim_ccr3 = 0;
    s->tim_ccr4 = 0;
    s->tim_dcr = 0;
    s->tim_dmar = 0;
    s->tim_or = 0;

    s->tick_offset = stm32f2xx_ns_to_ticks(s, now);
}

static uint64_t stm32f2xx_timer_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    STM32F2XXTimerState *s = opaque;

    DB_PRINT("Read 0x%"HWADDR_PRIx"\n", offset);

    switch (offset) {
    case TIM_CR1:
        return s->tim_cr1;
    case TIM_CR2:
        return s->tim_cr2;
    case TIM_SMCR:
        return s->tim_smcr;
    case TIM_DIER:
        return s->tim_dier;
    case TIM_SR:
        return s->tim_sr;
    case TIM_EGR:
        return s->tim_egr;
    case TIM_CCMR1:
        return s->tim_ccmr1;
    case TIM_CCMR2:
        return s->tim_ccmr2;
    case TIM_CCER:
        return s->tim_ccer;
    case TIM_CNT:
        return stm32f2xx_ns_to_ticks(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) -
               s->tick_offset;
    case TIM_PSC:
        return s->tim_psc;
    case TIM_ARR:
        return s->tim_arr;
    case TIM_CCR1:
        return s->tim_ccr1;
    case TIM_CCR2:
        return s->tim_ccr2;
    case TIM_CCR3:
        return s->tim_ccr3;
    case TIM_CCR4:
        return s->tim_ccr4;
    case TIM_DCR:
        return s->tim_dcr;
    case TIM_DMAR:
        return s->tim_dmar;
    case TIM_OR:
        return s->tim_or;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, offset);
    }

    return 0;
}

static void stm32f2xx_timer_write(void *opaque, hwaddr offset,
                        uint64_t val64, unsigned size)
{
    STM32F2XXTimerState *s = opaque;
    uint32_t value = val64;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t timer_val = 0;

    DB_PRINT("Write 0x%x, 0x%"HWADDR_PRIx"\n", value, offset);

    switch (offset) {
    case TIM_CR1:
        s->tim_cr1 = value;
        return;
    case TIM_CR2:
        s->tim_cr2 = value;
        return;
    case TIM_SMCR:
        s->tim_smcr = value;
        return;
    case TIM_DIER:
        s->tim_dier = value;
        return;
    case TIM_SR:
        /* This is set by hardware and cleared by software */
        s->tim_sr &= value;
        return;
    case TIM_EGR:
        s->tim_egr = value;
        if (s->tim_egr & TIM_EGR_UG) {
            timer_val = 0;
            break;
        }
        return;
    case TIM_CCMR1:
        s->tim_ccmr1 = value;
        return;
    case TIM_CCMR2:
        s->tim_ccmr2 = value;
        return;
    case TIM_CCER:
        s->tim_ccer = value;
        return;
    case TIM_PSC:
        timer_val = stm32f2xx_ns_to_ticks(s, now) - s->tick_offset;
        s->tim_psc = value & 0xFFFF;
        value = timer_val;
        break;
    case TIM_CNT:
        timer_val = value;
        break;
    case TIM_ARR:
        s->tim_arr = value;
        stm32f2xx_timer_set_alarm(s, now);
        return;
    case TIM_CCR1:
        s->tim_ccr1 = value;
        return;
    case TIM_CCR2:
        s->tim_ccr2 = value;
        return;
    case TIM_CCR3:
        s->tim_ccr3 = value;
        return;
    case TIM_CCR4:
        s->tim_ccr4 = value;
        return;
    case TIM_DCR:
        s->tim_dcr = value;
        return;
    case TIM_DMAR:
        s->tim_dmar = value;
        return;
    case TIM_OR:
        s->tim_or = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, offset);
        return;
    }

    /* This means that a register write has affected the timer in a way that
     * requires a refresh of both tick_offset and the alarm.
     */
    s->tick_offset = stm32f2xx_ns_to_ticks(s, now) - timer_val;
    stm32f2xx_timer_set_alarm(s, now);
}

static const MemoryRegionOps stm32f2xx_timer_ops = {
    .read = stm32f2xx_timer_read,
    .write = stm32f2xx_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stm32f2xx_timer = {
    .name = TYPE_STM32F2XX_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(tick_offset, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_cr1, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_cr2, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_smcr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_dier, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_sr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_egr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccmr1, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccmr2, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccer, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_psc, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_arr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr1, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr2, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr3, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr4, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_dcr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_dmar, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_or, STM32F2XXTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property stm32f2xx_timer_properties[] = {
    DEFINE_PROP_UINT64("clock-frequency", struct STM32F2XXTimerState,
                       freq_hz, 1000000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f2xx_timer_init(Object *obj)
{
    STM32F2XXTimerState *s = STM32F2XXTIMER(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &stm32f2xx_timer_ops, s,
                          "stm32f2xx_timer", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, stm32f2xx_timer_interrupt, s);
}

static void stm32f2xx_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f2xx_timer_reset;
    dc->props = stm32f2xx_timer_properties;
    dc->vmsd = &vmstate_stm32f2xx_timer;
}

static const TypeInfo stm32f2xx_timer_info = {
    .name          = TYPE_STM32F2XX_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F2XXTimerState),
    .instance_init = stm32f2xx_timer_init,
    .class_init    = stm32f2xx_timer_class_init,
};

static void stm32f2xx_timer_register_types(void)
{
    type_register_static(&stm32f2xx_timer_info);
}

type_init(stm32f2xx_timer_register_types)
