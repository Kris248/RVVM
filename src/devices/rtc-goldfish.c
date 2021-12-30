/*
rtc-goldfish.c - Goldfish Real-time Clock
Copyright (C) 2021  LekKit <github.com/LekKit>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "rtc-goldfish.h"
#include "plic.h"
#include "mem_ops.h"
#include <time.h>

#define RTC_TIME_LOW     0x0
#define RTC_TIME_HIGH    0x4
#define RTC_ALARM_LOW    0x8
#define RTC_ALARM_HIGH   0xC
#define RTC_IRQ_ENABLED  0x10
#define RTC_ALARM_CLEAR  0x14
#define RTC_ALARM_STATUS 0x18
#define RTC_IRQ_CLEAR    0x1C

#define RTC_REG_SIZE     0x20

struct rtc_goldfish_data {
    void* plic;
    uint32_t irq;
    uint32_t alarm_low;
    uint32_t alarm_high;
    bool irq_enabled;
    bool alarm_enabled;
};

static bool rtc_goldfish_mmio_read(rvvm_mmio_dev_t* dev, void* data, paddr_t offset, uint8_t size)
{
    struct rtc_goldfish_data* rtc = dev->data;
    uint64_t timer64 = time(NULL) * 1000000000ULL;
    switch (offset) {
        case RTC_TIME_LOW:
            write_uint32_le(data, timer64);
            break;
        case RTC_TIME_HIGH:
            write_uint32_le(data, timer64 >> 32);
            break;
        case RTC_ALARM_LOW:
            write_uint32_le(data, rtc->alarm_low);
            break;
        case RTC_ALARM_HIGH:
            write_uint32_le(data, rtc->alarm_high);
            break;
        case RTC_IRQ_ENABLED:
            write_uint32_le(data, rtc->irq_enabled);
            break;
        case RTC_ALARM_STATUS:
            write_uint32_le(data, rtc->alarm_enabled);
            break;
        default:
            memset(data, 0, size);
            break;
    }
    return true;
}

static bool rtc_goldfish_mmio_write(rvvm_mmio_dev_t* dev, void* data, paddr_t offset, uint8_t size)
{
    struct rtc_goldfish_data* rtc = dev->data;
    uint64_t timer64 = time(NULL) * 1000000000ULL;
    UNUSED(size);
    switch (offset) {
        case RTC_ALARM_LOW:
            rtc->alarm_low = read_uint32_le(data);
            break;
        case RTC_ALARM_HIGH:
            rtc->alarm_high = read_uint32_le(data);
            break;
        case RTC_IRQ_ENABLED:
            rtc->irq_enabled = read_uint32_le(data);
            break;
        case RTC_ALARM_CLEAR:
            rtc->alarm_enabled = false;
            break;
        default:
            break;
    }
    uint64_t alarm64 = rtc->alarm_low | (((uint64_t)rtc->alarm_high) << 32);
    if (rtc->alarm_enabled && rtc->irq_enabled && timer64 <= alarm64) {
        if (rtc->plic) plic_send_irq(dev->machine, rtc->plic, rtc->irq);
        rtc->alarm_enabled = false;
    } else {
        rtc->alarm_enabled = true;
    }
    return true;
}

static rvvm_mmio_type_t rtc_goldfish_dev_type = {
    .name = "rtc_goldfish",
};

void rtc_goldfish_init(rvvm_machine_t* machine, paddr_t base_addr, void* intc_data, uint32_t irq)
{
    struct rtc_goldfish_data* ptr = safe_calloc(sizeof(struct rtc_goldfish_data), 1);
    ptr->plic = intc_data;
    ptr->irq = irq;
    
    rvvm_mmio_dev_t rtc_goldfish = {0};
    rtc_goldfish.min_op_size = 4;
    rtc_goldfish.max_op_size = 4;
    rtc_goldfish.read = rtc_goldfish_mmio_read;
    rtc_goldfish.write = rtc_goldfish_mmio_write;
    rtc_goldfish.type = &rtc_goldfish_dev_type;
    rtc_goldfish.begin = base_addr;
    rtc_goldfish.end = base_addr + RTC_REG_SIZE;
    rtc_goldfish.data = ptr;
    rvvm_attach_mmio(machine, &rtc_goldfish);
#ifdef USE_FDT
    struct fdt_node* soc = fdt_node_find(machine->fdt, "soc");
    struct fdt_node* plic = soc ? fdt_node_find_reg_any(soc, "plic") : NULL;
    if (plic == NULL) {
        rvvm_warn("Missing nodes in FDT!");
        return;
    }
    
    struct fdt_node* rtc = fdt_node_create_reg("rtc", base_addr);
    fdt_node_add_prop_reg(rtc, "reg", base_addr, RTC_REG_SIZE);
    fdt_node_add_prop_str(rtc, "compatible", "google,goldfish-rtc");
    fdt_node_add_prop_u32(rtc, "interrupt-parent", fdt_node_get_phandle(plic));
    fdt_node_add_prop_u32(rtc, "interrupts", irq);
    fdt_node_add_child(soc, rtc);
#endif
}