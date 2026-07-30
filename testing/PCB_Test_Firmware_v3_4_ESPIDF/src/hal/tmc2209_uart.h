#pragma once

#include <cstdint>
#include "driver/uart.h"

// Minimal native ESP-IDF TMC2209 single-wire UART register driver — replaces
// the Arduino-only TMCStepper library. Talks directly to driver/uart.h; both
// TMC2209 modules share one physical UART bus/node (config.h,
// PIN_TMC_UART_TX/RX), addressed by their MS1/MS2-strapped slave address.
//
// Implements exactly the register operations this firmware needs (see
// motors.cpp): GCONF (pdn_disable / i_scale_analog / en_spreadCycle),
// CHOPCONF (toff), IHOLD_IRUN (run/hold current), SLAVECONF (senddelay),
// IOIN (version readback for testConnection), SG_RESULT (StallGuard).
//
// TMC2209 UART datagram format (single-wire, half-duplex):
//   Write (8 bytes):  [sync=0x05][addr][reg|0x80][data3][data2][data1][data0][crc8]
//   Read request (4 bytes): [sync=0x05][addr][reg][crc8]
//   Read reply (8 bytes):   [sync=0x05][0xFF][reg][data3][data2][data1][data0][crc8]
//   (data bytes big-endian / MSB-first; CRC is the TMC-specific CRC8-ATM,
//   polynomial 0x07, matching TMCStepper's CRC8() algorithm.)
//
// Because TX and RX are both wired to the same physical bus node, every byte
// this driver transmits is also received back on RX (bus echo) before the
// slave's actual reply arrives — readReg() accounts for that.
namespace tmc2209 {

// Register addresses used by this firmware (TMC2209 datasheet register map).
constexpr uint8_t REG_GCONF      = 0x00;
constexpr uint8_t REG_SLAVECONF  = 0x03;
constexpr uint8_t REG_IOIN       = 0x06;
constexpr uint8_t REG_IHOLD_IRUN = 0x10;
constexpr uint8_t REG_SGTHRS     = 0x40;
constexpr uint8_t REG_SG_RESULT  = 0x41;
constexpr uint8_t REG_CHOPCONF   = 0x6C;

// GCONF bit positions (TMC2209 datasheet §5.2 register map).
constexpr uint32_t GCONF_I_SCALE_ANALOG = (1u << 0);
constexpr uint32_t GCONF_EN_SPREADCYCLE = (1u << 2);
constexpr uint32_t GCONF_PDN_DISABLE    = (1u << 6);

constexpr uint8_t TMC2209_VERSION = 0x21; // IOIN bits[31:24], expected part version

// Bring up the shared UART bus. Call once, before any per-driver ops below.
bool busInit(uart_port_t uartNum, int txGpio, int rxGpio, uint32_t baud);

// Raw register access, addressed by TMC UART slave address.
bool writeReg(uint8_t addr, uint8_t reg, uint32_t val);
bool readReg(uint8_t addr, uint8_t reg, uint32_t &val);

// Higher-level ops used by motors.cpp (mirror what TMCStepper's
// TMC2209Stepper class used to provide):

// Sets toff=4, en_spreadCycle=1, i_scale_analog=0, pdn_disable=1, senddelay=4,
// and IHOLD/IRUN from the requested mA (current scaling per TMC2209
// datasheet / TMCStepper's rms_current() formula, using TMC_R_SENSE from
// config.h). Read-modify-write on GCONF/CHOPCONF/IHOLD_IRUN so any other
// hardware-default bits in those registers are left alone.
bool configureDriver(uint8_t addr, uint16_t runCurrentMa, uint16_t holdCurrentMa);

// Re-asserts GCONF.en_spreadCycle=1 and reads it back. True if confirmed set.
bool confirmSpreadCycle(uint8_t addr);

// UART link check: reads IOIN and checks the VERSION byte. 0 = OK (matches
// TMCStepper's test_connection() convention), non-zero = not OK.
uint8_t testConnection(uint8_t addr);

// StallGuard result, 10 bits (0-1023, lower = more load). Returns 0 on a
// failed read.
uint16_t readStallGuard(uint8_t addr);

} // namespace tmc2209
