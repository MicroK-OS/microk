#include <arch/x64/io/io.hpp>

namespace x86_64 {
void OutB(uint16_t port, uint8_t val) {
        asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

void OutW(uint16_t port, uint16_t val) {
        asm volatile ( "outw %0, %1" : : "a"(val), "Nd"(port) );
}

uint8_t InB(uint16_t port) {
        uint8_t ret;
        asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
        return ret;
}

uint16_t InW(uint16_t port) {
        uint16_t ret;
        asm volatile ( "inw %1, %0" : "=a"(ret) : "Nd"(port) );
        return ret;
}

void IoWait(void) {
        OutB(0x80, 0);
}
}
