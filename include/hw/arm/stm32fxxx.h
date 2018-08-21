/*
 * STM32FXXX Controller
 *
 * Copyright (C) 2018 Martin Schröder <mkschreder.uk@gmail.com>
 *
 * Implementation based on ST Microelectronics "RM0008 Reference Manual Rev 10"
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

#pragma once

#define STM32FXXX_NUM_UARTS 8
#define STM32FXXX_NUM_TIMERS 4
#define STM32FXXX_NUM_ADCS 3
#define STM32FXXX_NUM_SPIS 6
#define STM32FXXX_NUM_GPIOS 11

struct stm32fxxx_state {
    struct stm32fxxx_gpio_state {
        union {
            uint32_t regs[10];
            struct {
                uint32_t MODER;
                uint32_t OTYPER;
                uint32_t OSPEEDR;
                uint32_t PUPDR;
                uint32_t IDR;
                uint32_t ODR;
                uint32_t BSRR;
                uint32_t LCKR;
                uint32_t AFRL;
                uint32_t AFRH;
            };
        };
    } GPIO[STM32FXXX_NUM_GPIOS];
    struct stm32fxxx_spi_regs {
        union {
            uint16_t regs[9];
            struct {
                uint16_t CR1;
                uint16_t CR2;
                uint16_t SR;
                uint16_t DR;
                uint16_t CRCPR;
                uint16_t RXCRCR;
                uint16_t TXCRCR;
                uint16_t I2SCFGR;
                uint16_t I2SPR;
            };
        };
    } SPI[STM32FXXX_NUM_SPIS];
    uint32_t PWR_CR;
    uint32_t PWR_CSR;
};

