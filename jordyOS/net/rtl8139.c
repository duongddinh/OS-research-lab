/* Polled legacy RTL8139, PCI bus 0. DMA uses identity-mapped static storage.
   Register semantics: QEMU hw/net/rtl8139.c; no IRQ or C+ mode required. */
#include "network.h"
#include "../util.h"

#define RX_SIZE 8192u
static uint16_t io;
static unsigned rx_offset, tx_slot;
static volatile uint8_t rx_ring[RX_SIZE] __attribute__((aligned(256)));
static uint8_t tx_buffers[4][1536] __attribute__((aligned(256)));
static uint8_t frame[NET_FRAME_MAX];
uint8_t net_mac[6];

static uint8_t in8(uint16_t port) {
    uint8_t n; __asm__ volatile("inb %1,%0" : "=a"(n) : "Nd"(port) : "memory"); return n;
}
static uint16_t in16(uint16_t port) {
    uint16_t n; __asm__ volatile("inw %1,%0" : "=a"(n) : "Nd"(port) : "memory"); return n;
}
static uint32_t in32(uint16_t port) {
    uint32_t n; __asm__ volatile("inl %1,%0" : "=a"(n) : "Nd"(port) : "memory"); return n;
}
static void out8(uint16_t port, uint8_t n) {
    __asm__ volatile("outb %0,%1" : : "a"(n), "Nd"(port) : "memory");
}
static void out16(uint16_t port, uint16_t n) {
    __asm__ volatile("outw %0,%1" : : "a"(n), "Nd"(port) : "memory");
}
static void out32(uint16_t port, uint32_t n) {
    __asm__ volatile("outl %0,%1" : : "a"(n), "Nd"(port) : "memory");
}
static uint32_t pci_read(unsigned slot, unsigned function, unsigned reg) {
    out32(0xcf8, 0x80000000u | slot << 11 | function << 8 | reg);
    return in32(0xcfc);
}
bool rtl_up(void) {
    if (native_network.up) return true;
    for (unsigned slot = 0; slot < 32; slot++) {
        for (unsigned fn = 0; fn < 8; fn++) {
            if (pci_read(slot, fn, 0) != 0x813910ec) continue;
            uint32_t bar = pci_read(slot, fn, 0x10);
            if (!(bar & 1) || (bar & ~3u) > 0xff00 || !(bar & ~3u)) return false;
            io = (uint16_t)(bar & ~3u);
            /* This legacy NIC has 32-bit DMA addresses, even in a 64-bit OS. */
            if ((uintptr_t)rx_ring > UINT32_MAX - sizeof rx_ring ||
                (uintptr_t)tx_buffers > UINT32_MAX - sizeof tx_buffers) return false;
            uint16_t command = (uint16_t)pci_read(slot, fn, 4);
            /* A 16-bit write avoids clearing write-one-to-clear PCI status. */
            out16(0xcfc, command | 5); /* I/O decode + bus master */
            out8(io + 0x52, 0); /* wake */
            out8(io + 0x37, 0x10); /* reset with bounded wait */
            unsigned tries = 100000;
            while ((in8(io + 0x37) & 0x10) && --tries) __asm__ volatile("pause");
            if (!tries) return false;
            for (unsigned i = 0; i < 6; i++) net_mac[i] = in8(io + i);
            rx_offset = tx_slot = 0;
            out16(io + 0x3c, 0); /* interrupts masked: polled kernel */
            out16(io + 0x3e, 0xffff);
            out32(io + 0x30, (uint32_t)(uintptr_t)rx_ring);
            for (unsigned i = 0; i < 4; i++)
                out32(io + 0x20 + i * 4, (uint32_t)(uintptr_t)tx_buffers[i]);
            out8(io + 0x37, 0x0c); /* RX + TX */
            out32(io + 0x44, 0x0000070a); /* 8K wrapping RX, own MAC + broadcast */
            out32(io + 0x40, 0x03000700); /* normal TX, max DMA burst */
            out16(io + 0x38, (uint16_t)-16);
            native_network.up = true;
            return true;
        }
    }
    return false;
}
void rtl_down(void) {
    if (io) out8(io + 0x37, 0);
    native_network.up = false;
}
bool rtl_send(const uint8_t *data, size_t size) {
    if (!native_network.up || size < 14 || size > NET_FRAME_MAX) return false;
    if (!(in32(io + 0x10 + tx_slot * 4) & 0x2000)) {
        native_network.tx_dropped++; return false;
    }
    size_t wire_size = size < 60 ? 60 : size;
    memset(tx_buffers[tx_slot], 0, wire_size);
    memcpy(tx_buffers[tx_slot], data, size);
    out32(io + 0x10 + tx_slot * 4, (uint32_t)wire_size);
    tx_slot = (tx_slot + 1) % 4;
    native_network.transmitted++; return true;
}
void rtl_poll(void) {
    if (!native_network.up) return;
    if (in16(io + 0x3e) & 0x50) {
        /* Overflow is recoverable; reset ring rather than trusting old offsets. */
        native_network.driver_errors++; rtl_down(); rtl_up(); return;
    }
    for (unsigned budget = 0; budget < 8 && !(in8(io + 0x37) & 1); budget++) {
        uint16_t status = rx_ring[rx_offset] | (uint16_t)rx_ring[(rx_offset + 1) % RX_SIZE] << 8;
        unsigned size = rx_ring[(rx_offset + 2) % RX_SIZE] |
                        (unsigned)rx_ring[(rx_offset + 3) % RX_SIZE] << 8;
        if (size == 0xfff0) break; /* DMA still in progress */
        if (size < 4 || size > NET_FRAME_MAX + 4) {
            native_network.driver_errors++; rtl_down(); rtl_up(); return;
        }
        if ((status & 0x3f) == 1) {
            for (unsigned i = 0; i < size - 4; i++) frame[i] = rx_ring[(rx_offset + 4 + i) % RX_SIZE];
            net_receive(frame, size - 4);
        } else native_network.driver_errors++;
        rx_offset = ((rx_offset + size + 4 + 3) & ~3u) % RX_SIZE;
        out16(io + 0x38, (uint16_t)(rx_offset - 16));
    }
    out16(io + 0x3e, 0x0f);
}
