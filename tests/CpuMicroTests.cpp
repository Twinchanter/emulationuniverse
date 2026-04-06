#include "cpu/CPU.hpp"
#include "core/Types.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

class FlatMemory final : public GB::IMemory {
public:
    FlatMemory() { mem.fill(0); }

    GB::u8 read(GB::u16 address) const override {
        return mem[address];
    }

    void write(GB::u16 address, GB::u8 value) override {
        mem[address] = value;
    }

    std::array<GB::u8, 65536> mem{};
};

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

void expect(bool cond, const std::string& msg) {
    if (!cond) {
        fail(msg);
    }
}

struct TestResult {
    std::string priority;
    std::string name;
    bool passed = false;
    std::string detail;
};

using TestFunc = std::function<void()>;

struct TestCase {
    std::string priority;
    std::string name;
    TestFunc run;
};

void testLoadAndAddImmediate() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // 0x0100: LD A,0x12 ; ADD A,0x22
    bus.write(0x0100, 0x3E);
    bus.write(0x0101, 0x12);
    bus.write(0x0102, 0xC6);
    bus.write(0x0103, 0x22);

    cpu.step();
    expect(cpu.registers().A == 0x12, "LD A,d8 failed");
    expect(cpu.getPC() == 0x0102, "PC after LD A,d8 incorrect");

    cpu.step();
    expect(cpu.registers().A == 0x34, "ADD A,d8 result incorrect");
    expect(!cpu.registers().flagZ(), "Z should be clear after 0x12+0x22");
    expect(!cpu.registers().flagN(), "N should be clear after ADD");
    expect(!cpu.registers().flagH(), "H should be clear after 0x12+0x22");
    expect(!cpu.registers().flagC(), "C should be clear after 0x12+0x22");
}

void testXorASetsZero() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // 0x0100: XOR A
    bus.write(0x0100, 0xAF);

    cpu.registers().A = 0x5A;
    cpu.step();

    expect(cpu.registers().A == 0x00, "XOR A should clear A");
    expect(cpu.registers().flagZ(), "XOR A should set Z");
    expect(!cpu.registers().flagN(), "XOR A should clear N");
    expect(!cpu.registers().flagH(), "XOR A should clear H");
    expect(!cpu.registers().flagC(), "XOR A should clear C");
}

void testCbRlcB() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // 0x0100: LD B,0x80 ; CB 00 (RLC B)
    bus.write(0x0100, 0x06);
    bus.write(0x0101, 0x80);
    bus.write(0x0102, 0xCB);
    bus.write(0x0103, 0x00);

    cpu.step();
    expect(cpu.registers().B == 0x80, "LD B,d8 failed");

    cpu.step();
    expect(cpu.registers().B == 0x01, "CB RLC B result incorrect");
    expect(!cpu.registers().flagZ(), "CB RLC B should clear Z for non-zero result");
    expect(!cpu.registers().flagN(), "CB RLC B should clear N");
    expect(!cpu.registers().flagH(), "CB RLC B should clear H");
    expect(cpu.registers().flagC(), "CB RLC B should set C when bit7 was 1");
}

void testInterruptServiceFlow() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // 0x0100: EI ; NOP ; NOP
    bus.write(0x0100, 0xFB);
    bus.write(0x0101, 0x00);
    bus.write(0x0102, 0x00);

    // Enable and request VBlank interrupt.
    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(GB::Interrupt::VBlank));
    bus.write(GB::REG_IF, static_cast<GB::u8>(GB::Interrupt::VBlank));

    cpu.step(); // EI (IME pending)
    cpu.step(); // NOP (IME becomes true at start)
    cpu.step(); // should service interrupt before fetching next opcode

    expect(cpu.getPC() == 0x0040, "Interrupt vector jump should target 0x0040");
    expect(cpu.getSP() == 0xFFFC, "Interrupt service should push return address");

    const GB::u16 returnAddr = bus.readWord(cpu.getSP());
    expect(returnAddr == 0x0102, "Interrupt return address on stack is incorrect");

    const GB::u8 ifReg = bus.read(GB::REG_IF);
    expect((ifReg & static_cast<GB::u8>(GB::Interrupt::VBlank)) == 0, "Interrupt flag should be cleared after service");
}

void testAddHalfCarryFlag() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0x0F ; ADD A,0x01
    bus.write(0x0100, 0x3E);
    bus.write(0x0101, 0x0F);
    bus.write(0x0102, 0xC6);
    bus.write(0x0103, 0x01);

    cpu.step();
    cpu.step();

    expect(cpu.registers().A == 0x10, "ADD half-carry case produced wrong A");
    expect(!cpu.registers().flagZ(), "Z should be clear for 0x10");
    expect(!cpu.registers().flagN(), "N should be clear after ADD");
    expect(cpu.registers().flagH(), "H should be set for 0x0F + 0x01");
    expect(!cpu.registers().flagC(), "C should be clear for 0x0F + 0x01");
}

void testAddCarryFlag() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0xF0 ; ADD A,0x20
    bus.write(0x0100, 0x3E);
    bus.write(0x0101, 0xF0);
    bus.write(0x0102, 0xC6);
    bus.write(0x0103, 0x20);

    cpu.step();
    cpu.step();

    expect(cpu.registers().A == 0x10, "ADD carry case produced wrong A");
    expect(!cpu.registers().flagZ(), "Z should be clear for 0x10");
    expect(!cpu.registers().flagN(), "N should be clear after ADD");
    expect(!cpu.registers().flagH(), "H should be clear for 0xF0 + 0x20");
    expect(cpu.registers().flagC(), "C should be set for 0xF0 + 0x20");
}

void testIncDecBFlags() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD B,0xFF ; INC B ; DEC B
    bus.write(0x0100, 0x06);
    bus.write(0x0101, 0xFF);
    bus.write(0x0102, 0x04);
    bus.write(0x0103, 0x05);

    cpu.step();
    cpu.step();
    expect(cpu.registers().B == 0x00, "INC B should wrap 0xFF to 0x00");
    expect(cpu.registers().flagZ(), "INC B to zero should set Z");
    expect(!cpu.registers().flagN(), "INC should clear N");
    expect(cpu.registers().flagH(), "INC 0xFF should set H");

    cpu.step();
    expect(cpu.registers().B == 0xFF, "DEC B should wrap 0x00 to 0xFF");
    expect(!cpu.registers().flagZ(), "DEC to 0xFF should clear Z");
    expect(cpu.registers().flagN(), "DEC should set N");
    expect(cpu.registers().flagH(), "DEC 0x00 should set H");
}

void testJumpAbsolute() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // JP 0x0200 ; [0x0200] LD A,0x77
    bus.write(0x0100, 0xC3);
    bus.write(0x0101, 0x00);
    bus.write(0x0102, 0x02);
    bus.write(0x0200, 0x3E);
    bus.write(0x0201, 0x77);

    cpu.step();
    expect(cpu.getPC() == 0x0200, "JP a16 should jump to 0x0200");

    cpu.step();
    expect(cpu.registers().A == 0x77, "Instruction at jump target should execute");
}

// ── Tranche 3: ALU accuracy, HL-indirect memory ops, CB BIT, 16-bit ADD ─────

void testSubBorrowFlags() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0x10 ; SUB 0x01 → A=0x0F, N=1, H=1 (nibble borrow 0-1), C=0
    // SUB 0x10 on A=0x0F → A=0xFF, N=1, H=0, C=1 (full borrow 0x0F < 0x10)
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x10); // LD A, 0x10
    bus.write(0x0102, 0xD6); bus.write(0x0103, 0x01); // SUB 0x01
    bus.write(0x0104, 0xD6); bus.write(0x0105, 0x10); // SUB 0x10

    cpu.step(); // LD A, 0x10
    cpu.step(); // SUB 0x01
    expect(cpu.registers().A == 0x0F, "SUB 0x10-0x01 should give 0x0F");
    expect(!cpu.registers().flagZ(), "Z clear: result is 0x0F");
    expect(cpu.registers().flagN(), "SUB always sets N");
    expect(cpu.registers().flagH(), "H set: nibble borrow 0x0 < 0x1");
    expect(!cpu.registers().flagC(), "C clear: 0x10 >= 0x01, no full borrow");

    cpu.step(); // SUB 0x10 on A=0x0F → wraps to 0xFF, borrow
    expect(cpu.registers().A == 0xFF, "SUB 0x0F-0x10 should wrap to 0xFF");
    expect(!cpu.registers().flagZ(), "Z clear: result is 0xFF");
    expect(cpu.registers().flagN(), "SUB always sets N");
    expect(!cpu.registers().flagH(), "H clear: nibble 0xF - 0x0 no borrow");
    expect(cpu.registers().flagC(), "C set: 0x0F < 0x10, full borrow");
}

void testSbcCarryPropagation() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // ADD A,0x20 with A=0xF0 → A=0x10, C=1 (0xF0+0x20=0x110)
    // LD B,0x01; SBC A,B → A = 0x10 - 0x01 - 1(C) = 0x0E
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0xF0); // LD A, 0xF0
    bus.write(0x0102, 0xC6); bus.write(0x0103, 0x20); // ADD A, 0x20 → C=1, A=0x10
    bus.write(0x0104, 0x06); bus.write(0x0105, 0x01); // LD B, 0x01
    bus.write(0x0106, 0x98);                           // SBC A, B → 0x10-0x01-1=0x0E

    cpu.step(); // LD A, 0xF0
    cpu.step(); // ADD A, 0x20
    expect(cpu.registers().A  == 0x10, "ADD 0xF0+0x20 should wrap to 0x10");
    expect(cpu.registers().flagC(), "ADD 0xF0+0x20 must set carry");

    cpu.step(); // LD B, 0x01
    cpu.step(); // SBC A, B
    expect(cpu.registers().A == 0x0E, "SBC: 0x10-0x01-carry(1)=0x0E");
    expect(!cpu.registers().flagZ(), "Z clear: result is 0x0E");
    expect(cpu.registers().flagN(), "SBC always sets N");
    expect(!cpu.registers().flagC(), "C clear: no borrow for 0x10 - 0x02");
}

void testAndMaskFlags() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0xFF ; AND 0x0F → A=0x0F, Z=0, N=0, H=1 (always), C=0
    // AND 0x00 on A=0x0F → A=0x00, Z=1, H=1
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0xFF); // LD A, 0xFF
    bus.write(0x0102, 0xE6); bus.write(0x0103, 0x0F); // AND 0x0F
    bus.write(0x0104, 0xE6); bus.write(0x0105, 0x00); // AND 0x00

    cpu.step(); // LD A, 0xFF
    cpu.step(); // AND 0x0F
    expect(cpu.registers().A == 0x0F, "AND 0xFF & 0x0F should give 0x0F");
    expect(!cpu.registers().flagZ(), "Z clear: result is 0x0F");
    expect(!cpu.registers().flagN(), "AND always clears N");
    expect(cpu.registers().flagH(), "AND always sets H");
    expect(!cpu.registers().flagC(), "AND always clears C");

    cpu.step(); // AND 0x00 → Z=1
    expect(cpu.registers().A == 0x00, "AND 0x0F & 0x00 should give 0x00");
    expect(cpu.registers().flagZ(), "Z set: AND result is zero");
    expect(cpu.registers().flagH(), "AND sets H even on zero result");
}

void testOrFlags() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // XOR A (Z=1); OR 0x55 → A=0x55, Z=0, N=0, H=0, C=0
    // OR 0xAA on A=0x55 → A=0xFF, Z=0
    bus.write(0x0100, 0xAF);                           // XOR A
    bus.write(0x0101, 0xF6); bus.write(0x0102, 0x55); // OR  0x55
    bus.write(0x0103, 0xF6); bus.write(0x0104, 0xAA); // OR  0xAA

    cpu.step(); // XOR A → A=0
    cpu.step(); // OR 0x55
    expect(cpu.registers().A == 0x55, "OR 0x00|0x55 should give 0x55");
    expect(!cpu.registers().flagZ(), "Z clear: result is 0x55");
    expect(!cpu.registers().flagN(), "OR always clears N");
    expect(!cpu.registers().flagH(), "OR always clears H");
    expect(!cpu.registers().flagC(), "OR always clears C");

    cpu.step(); // OR 0xAA → A=0xFF
    expect(cpu.registers().A == 0xFF, "OR 0x55|0xAA should give 0xFF");
    expect(!cpu.registers().flagZ(), "Z clear: result is 0xFF");
}

void testCpCompareFlags() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0x42 ; CP 0x42 → Z=1, N=1, C=0 (equal, no borrow)
    //            CP 0x43 → Z=0, N=1, C=1 (A < operand, borrow)
    // A must remain 0x42 throughout (CP does not modify A)
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x42); // LD A, 0x42
    bus.write(0x0102, 0xFE); bus.write(0x0103, 0x42); // CP  0x42
    bus.write(0x0104, 0xFE); bus.write(0x0105, 0x43); // CP  0x43

    cpu.step(); // LD A, 0x42
    cpu.step(); // CP 0x42 (equal)
    expect(cpu.registers().A == 0x42, "CP must not modify A");
    expect(cpu.registers().flagZ(), "CP equal: Z set");
    expect(cpu.registers().flagN(), "CP always sets N");
    expect(!cpu.registers().flagC(), "CP equal: no borrow, C clear");

    cpu.step(); // CP 0x43 (A < operand → borrow)
    expect(cpu.registers().A == 0x42, "CP must not modify A");
    expect(!cpu.registers().flagZ(), "CP unequal: Z clear");
    expect(cpu.registers().flagN(), "CP always sets N");
    expect(cpu.registers().flagC(), "CP: A(0x42) < operand(0x43) → C set");
}

void testLdHlIndirect() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD H,0x02 ; LD L,0x50 → HL=0x0250
    // seed bus at 0x0250 = 0xCD
    // LD A,(HL) → A=0xCD
    // LD A,0xAB ; LD (HL),A → bus[0x0250]=0xAB
    bus.write(0x0100, 0x26); bus.write(0x0101, 0x02); // LD H, 0x02
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x50); // LD L, 0x50
    bus.write(0x0104, 0x7E);                           // LD A, (HL)
    bus.write(0x0105, 0x3E); bus.write(0x0106, 0xAB); // LD A, 0xAB
    bus.write(0x0107, 0x77);                           // LD (HL), A

    bus.write(0x0250, 0xCD); // seed target address before execution

    cpu.step(); // LD H, 0x02
    cpu.step(); // LD L, 0x50
    expect(cpu.registers().HL() == 0x0250, "HL should be 0x0250 after loads");

    cpu.step(); // LD A, (HL)
    expect(cpu.registers().A == 0xCD, "LD A,(HL) should read seeded byte 0xCD");

    cpu.step(); // LD A, 0xAB
    cpu.step(); // LD (HL), A
    expect(bus.read(0x0250) == 0xAB, "LD (HL),A should write A=0xAB to 0x0250");
}

void testCbBitTest() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0x80 ; CB 0x47 (BIT 0,A) → Z=1  (bit 0 of 0x80 is 0), H=1, N=0
    // LD B,0x80 ; CB 0x78 (BIT 7,B) → Z=0  (bit 7 of 0x80 is 1), H=1, N=0
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x80); // LD A, 0x80
    bus.write(0x0102, 0xCB); bus.write(0x0103, 0x47); // BIT 0, A
    bus.write(0x0104, 0x06); bus.write(0x0105, 0x80); // LD B, 0x80
    bus.write(0x0106, 0xCB); bus.write(0x0107, 0x78); // BIT 7, B

    cpu.step(); // LD A, 0x80
    cpu.step(); // BIT 0, A
    expect(cpu.registers().flagZ(), "BIT 0 of 0x80 is 0 → Z set");
    expect(!cpu.registers().flagN(), "BIT always clears N");
    expect(cpu.registers().flagH(), "BIT always sets H");

    cpu.step(); // LD B, 0x80
    cpu.step(); // BIT 7, B
    expect(!cpu.registers().flagZ(), "BIT 7 of 0x80 is 1 → Z clear");
    expect(!cpu.registers().flagN(), "BIT always clears N");
    expect(cpu.registers().flagH(), "BIT always sets H");
}

void testAddHlBc() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD BC=0x0FFF ; LD HL=0x0001 ; ADD HL,BC → HL=0x1000
    //   H_flag=1 (bit-11 carry: 0x001+0xFFF > 0xFFF), C=0, N=0
    bus.write(0x0100, 0x06); bus.write(0x0101, 0x0F); // LD B, 0x0F
    bus.write(0x0102, 0x0E); bus.write(0x0103, 0xFF); // LD C, 0xFF
    bus.write(0x0104, 0x26); bus.write(0x0105, 0x00); // LD H, 0x00
    bus.write(0x0106, 0x2E); bus.write(0x0107, 0x01); // LD L, 0x01
    bus.write(0x0108, 0x09);                           // ADD HL, BC

    cpu.step(); // LD B, 0x0F
    cpu.step(); // LD C, 0xFF
    cpu.step(); // LD H, 0x00
    cpu.step(); // LD L, 0x01
    cpu.step(); // ADD HL, BC
    expect(cpu.registers().HL() == 0x1000, "ADD HL,BC: 0x0001+0x0FFF=0x1000");
    expect(cpu.registers().flagH(), "H set: bit-11 carry in 0x001+0xFFF");
    expect(!cpu.registers().flagC(), "C clear: 0x1000 < 0x10000");
    expect(!cpu.registers().flagN(), "ADD HL,BC always clears N");
}

// ── Tranche 4: DAA, rotate/shift family, CB SET/RES, SP/HL transfer ─────────

void testDaaAfterAdd() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // 0x09 + 0x09 = 0x12 (binary), then DAA => 0x18 (BCD).
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x09); // LD A,0x09
    bus.write(0x0102, 0xC6); bus.write(0x0103, 0x09); // ADD A,0x09
    bus.write(0x0104, 0x27);                           // DAA

    cpu.step();
    cpu.step();
    expect(cpu.registers().A == 0x12, "Pre-DAA add result should be 0x12");
    expect(cpu.registers().flagH(), "Pre-DAA add should set H for 0x09+0x09");

    cpu.step(); // DAA
    expect(cpu.registers().A == 0x18, "DAA after 0x09+0x09 should yield BCD 0x18");
    expect(!cpu.registers().flagN(), "DAA should preserve N=0 for add path");
    expect(!cpu.registers().flagH(), "DAA must clear H");
    expect(!cpu.registers().flagC(), "No decimal carry expected for 0x18");
}

void testDaaAfterSub() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // 0x15 - 0x06 = 0x0F (binary), then DAA(sub path) => 0x09 (BCD).
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x15); // LD A,0x15
    bus.write(0x0102, 0xD6); bus.write(0x0103, 0x06); // SUB 0x06
    bus.write(0x0104, 0x27);                           // DAA

    cpu.step();
    cpu.step();
    expect(cpu.registers().A == 0x0F, "Pre-DAA sub result should be 0x0F");
    expect(cpu.registers().flagN(), "SUB should set N before DAA");
    expect(cpu.registers().flagH(), "SUB 0x15-0x06 should set H");

    cpu.step(); // DAA (sub mode)
    expect(cpu.registers().A == 0x09, "DAA after 0x15-0x06 should yield BCD 0x09");
    expect(cpu.registers().flagN(), "DAA should preserve N=1 for sub path");
    expect(!cpu.registers().flagH(), "DAA must clear H");
    expect(!cpu.registers().flagC(), "No decimal borrow carry expected");
}

void testCbSetResB() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD B,0x01 ; SET 7,B ; RES 0,B
    bus.write(0x0100, 0x06); bus.write(0x0101, 0x01); // LD B,0x01
    bus.write(0x0102, 0xCB); bus.write(0x0103, 0xF8); // SET 7,B
    bus.write(0x0104, 0xCB); bus.write(0x0105, 0x80); // RES 0,B

    cpu.step(); // LD B,0x01
    cpu.step(); // SET 7,B
    expect(cpu.registers().B == 0x81, "SET 7,B should produce 0x81 from 0x01");

    cpu.step(); // RES 0,B
    expect(cpu.registers().B == 0x80, "RES 0,B should clear bit0 and keep bit7 set");
}

void testCbSwapSraSrl() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0xF0 ; SWAP A => 0x0F
    // LD B,0x81 ; SRA B  => 0xC0 (sign-preserving right shift, C=1)
    // LD C,0x01 ; SRL C  => 0x00 (logical right shift, C=1, Z=1)
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0xF0); // LD A,0xF0
    bus.write(0x0102, 0xCB); bus.write(0x0103, 0x37); // SWAP A
    bus.write(0x0104, 0x06); bus.write(0x0105, 0x81); // LD B,0x81
    bus.write(0x0106, 0xCB); bus.write(0x0107, 0x28); // SRA B
    bus.write(0x0108, 0x0E); bus.write(0x0109, 0x01); // LD C,0x01
    bus.write(0x010A, 0xCB); bus.write(0x010B, 0x39); // SRL C

    cpu.step();
    cpu.step(); // SWAP A
    expect(cpu.registers().A == 0x0F, "SWAP A should transform 0xF0 to 0x0F");
    expect(!cpu.registers().flagZ(), "SWAP A result 0x0F should clear Z");

    cpu.step();
    cpu.step(); // SRA B
    expect(cpu.registers().B == 0xC0, "SRA B should keep sign bit: 0x81 -> 0xC0");
    expect(cpu.registers().flagC(), "SRA B should set C from old bit0");

    cpu.step();
    cpu.step(); // SRL C
    expect(cpu.registers().C == 0x00, "SRL C should shift 0x01 down to 0x00");
    expect(cpu.registers().flagC(), "SRL C should set C from old bit0");
    expect(cpu.registers().flagZ(), "SRL C result zero should set Z");
}

void testRlaRraCarryFlow() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Prime carry via ADD: 0x80 + 0x80 => A=0x00, C=1.
    // Then LD A,0x80 ; RLA uses old C(1) and sets C from old bit7.
    // Then RRA uses old C and sets C from old bit0.
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x80); // LD A,0x80
    bus.write(0x0102, 0xC6); bus.write(0x0103, 0x80); // ADD A,0x80 => C=1
    bus.write(0x0104, 0x3E); bus.write(0x0105, 0x80); // LD A,0x80
    bus.write(0x0106, 0x17);                           // RLA
    bus.write(0x0107, 0x1F);                           // RRA

    cpu.step();
    cpu.step();
    expect(cpu.registers().flagC(), "Carry priming ADD should set C");

    cpu.step(); // LD A,0x80 (flags unchanged)
    cpu.step(); // RLA
    expect(cpu.registers().A == 0x01, "RLA should rotate 0x80 with carry-in(1) to 0x01");
    expect(cpu.registers().flagC(), "RLA should set C from old bit7=1");

    cpu.step(); // RRA
    expect(cpu.registers().A == 0x80, "RRA should rotate 0x01 with carry-in(1) to 0x80");
    expect(cpu.registers().flagC(), "RRA should set C from old bit0=1");
}

void testLdSpHl() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD HL,0x1234 ; LD SP,HL
    bus.write(0x0100, 0x26); bus.write(0x0101, 0x12); // LD H,0x12
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x34); // LD L,0x34
    bus.write(0x0104, 0xF9);                           // LD SP,HL

    cpu.step();
    cpu.step();
    cpu.step();
    expect(cpu.registers().HL() == 0x1234, "HL should be 0x1234 before LD SP,HL");
    expect(cpu.getSP() == 0x1234, "LD SP,HL should copy HL into SP");
}

void testLdHlSpPlusR8() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD SP,0x00FF ; LD HL,SP+0x01 => HL=0x0100
    // For low-byte overflow, alu_addSP should set H and C; Z/N always clear.
    bus.write(0x0100, 0x31); bus.write(0x0101, 0xFF); bus.write(0x0102, 0x00); // LD SP,d16
    bus.write(0x0103, 0xF8); bus.write(0x0104, 0x01);                           // LD HL,SP+r8

    cpu.step(); // LD SP,0x00FF
    cpu.step(); // LD HL,SP+1
    expect(cpu.getSP() == 0x00FF, "LD HL,SP+r8 should not modify SP");
    expect(cpu.registers().HL() == 0x0100, "LD HL,SP+1 should compute 0x0100");
    expect(!cpu.registers().flagZ(), "LD HL,SP+r8 always clears Z");
    expect(!cpu.registers().flagN(), "LD HL,SP+r8 always clears N");
    expect(cpu.registers().flagH(), "LD HL,SP+r8 should set H on low nibble overflow");
    expect(cpu.registers().flagC(), "LD HL,SP+r8 should set C on low-byte overflow");
}

// ── Tranche 2: control-flow and stack integrity ───────────────────────────────

void testCallAndRet() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // 0x0100: CALL 0x0200 (CD 00 02)
    // 0x0103: LD A, 0x42  (3E 42)  ← return landing pad
    // 0x0200: LD A, 0x77  (3E 77)  ← callee body
    // 0x0202: RET         (C9)
    bus.write(0x0100, 0xCD); bus.write(0x0101, 0x00); bus.write(0x0102, 0x02);
    bus.write(0x0103, 0x3E); bus.write(0x0104, 0x42);
    bus.write(0x0200, 0x3E); bus.write(0x0201, 0x77);
    bus.write(0x0202, 0xC9);

    const GB::u16 spBefore = cpu.getSP(); // 0xFFFE

    cpu.step(); // CALL 0x0200
    expect(cpu.getPC() == 0x0200, "CALL should jump to callee 0x0200");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "CALL should decrement SP by 2");
    expect(bus.readWord(cpu.getSP()) == 0x0103, "CALL should push return address 0x0103");

    cpu.step(); // LD A, 0x77
    expect(cpu.registers().A == 0x77, "Callee body: LD A,0x77 should set A");

    cpu.step(); // RET
    expect(cpu.getPC() == 0x0103, "RET should restore PC to return address 0x0103");
    expect(cpu.getSP() == spBefore, "RET should restore SP to pre-CALL value");

    cpu.step(); // LD A, 0x42 (instruction after CALL site)
    expect(cpu.registers().A == 0x42, "Instruction post-RET should execute normally");
}

void testPushPopBc() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD B,0x12; LD C,0x34; PUSH BC; LD B,0x00; LD C,0x00; POP DE
    bus.write(0x0100, 0x06); bus.write(0x0101, 0x12); // LD B, 0x12
    bus.write(0x0102, 0x0E); bus.write(0x0103, 0x34); // LD C, 0x34
    bus.write(0x0104, 0xC5);                           // PUSH BC
    bus.write(0x0105, 0x06); bus.write(0x0106, 0x00); // LD B, 0x00
    bus.write(0x0107, 0x0E); bus.write(0x0108, 0x00); // LD C, 0x00
    bus.write(0x0109, 0xD1);                           // POP DE

    const GB::u16 spInit = cpu.getSP();

    cpu.step(); // LD B, 0x12
    cpu.step(); // LD C, 0x34

    cpu.step(); // PUSH BC
    expect(cpu.getSP() == static_cast<GB::u16>(spInit - 2), "PUSH BC should decrement SP by 2");
    expect(bus.readWord(cpu.getSP()) == 0x1234, "PUSH BC should write 0x1234 to stack");

    cpu.step(); // LD B, 0x00
    cpu.step(); // LD C, 0x00
    expect(cpu.registers().B == 0x00, "BC should be clobbered before POP");
    expect(cpu.registers().C == 0x00, "BC should be clobbered before POP");

    cpu.step(); // POP DE
    expect(cpu.getSP() == spInit, "POP DE should restore SP");
    expect(cpu.registers().D == 0x12, "POP DE should restore D = 0x12");
    expect(cpu.registers().E == 0x34, "POP DE should restore E = 0x34");
}

void testRetiReEnablesIme() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Manually place a fake return address on the stack at 0x0140.
    // RETI at 0x0100 should pop it and re-enable IME.
    // A pending VBlank interrupt is pre-requested so the very next step
    // after RETI services it (proving IME is immediately active).
    bus.write(0x0100, 0xD9); // RETI
    cpu.registers().SP = 0x0140;
    bus.write(0x0140, 0x10); // lo of return addr 0x0210
    bus.write(0x0141, 0x02); // hi of return addr 0x0210

    // Pre-arm VBlank interrupt (both enable and request).
    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(GB::Interrupt::VBlank));
    bus.write(GB::REG_IF,      static_cast<GB::u8>(GB::Interrupt::VBlank));

    cpu.step(); // RETI: PC = 0x0210, SP = 0x0142, IME = true (immediate)
    expect(cpu.getPC() == 0x0210, "RETI should pop return address 0x0210");
    expect(cpu.getSP() == 0x0142, "RETI should increment SP by 2");

    cpu.step(); // Should service VBlank interrupt (proves IME was re-enabled)
    expect(cpu.getPC() == 0x0040, "Pending interrupt must be serviced immediately after RETI");
}

void testConditionalJrTaken() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // XOR A sets Z=1; JR Z, +3 should jump over LD A,0xFF and land at LD A,0xAA.
    // After fetch of JR Z, PC = 0x0103; adding offset +3 → PC = 0x0106.
    bus.write(0x0100, 0xAF);                           // XOR A  (Z=1)
    bus.write(0x0101, 0x28); bus.write(0x0102, 0x03); // JR Z, +3
    bus.write(0x0103, 0x3E); bus.write(0x0104, 0xFF); // LD A, 0xFF  (skipped)
    bus.write(0x0105, 0x00);                           // NOP          (skipped)
    bus.write(0x0106, 0x3E); bus.write(0x0107, 0xAA); // LD A, 0xAA  (jump target)

    cpu.step(); // XOR A
    expect(cpu.registers().flagZ(), "XOR A should set Z for taken-branch setup");

    cpu.step(); // JR Z, +3 (taken)
    expect(cpu.getPC() == 0x0106, "JR Z taken should land at 0x0106");
    expect(cpu.registers().A == 0x00, "A must not be overwritten by skipped LD A,0xFF");

    cpu.step(); // LD A, 0xAA at jump target
    expect(cpu.registers().A == 0xAA, "Instruction at JR Z target should execute");
}

void testConditionalJrNotTaken() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0x01 then OR A (0xB7) to explicitly clear Z (since LD doesn't touch flags).
    // JR Z, +3 with Z=0 should NOT branch — continue sequentially to LD A,0xBB.
    // After fetch of JR Z at 0x0103, PC = 0x0105; Z=0 → no branch, PC stays 0x0105.
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x01); // LD A, 0x01
    bus.write(0x0102, 0xB7);                           // OR A  (Z=0 since A=1)
    bus.write(0x0103, 0x28); bus.write(0x0104, 0x03); // JR Z, +3
    bus.write(0x0105, 0x3E); bus.write(0x0106, 0xBB); // LD A, 0xBB  (falls through here)

    cpu.step(); // LD A, 0x01
    cpu.step(); // OR A → Z=0
    expect(!cpu.registers().flagZ(), "OR A with non-zero A should clear Z (not-taken setup)");

    cpu.step(); // JR Z, +3 (not taken)
    expect(cpu.getPC() == 0x0105, "JR Z not-taken should fall through to 0x0105");

    cpu.step(); // LD A, 0xBB
    expect(cpu.registers().A == 0xBB, "Fall-through instruction after JR Z not-taken should execute");
}

void testCallZAndRetZ() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // XOR A → Z=1; CALL Z,0x0200 taken; LD A,0xCC; RET Z taken; LD A,0x11.
    bus.write(0x0100, 0xAF);                           // XOR A      (Z=1)
    bus.write(0x0101, 0xCC); bus.write(0x0102, 0x00); // CALL Z, lo
    bus.write(0x0103, 0x02);                           // CALL Z, hi → target 0x0200
    bus.write(0x0104, 0x3E); bus.write(0x0105, 0x11); // LD A, 0x11 (return landing)
    bus.write(0x0200, 0x3E); bus.write(0x0201, 0xCC); // LD A, 0xCC (callee body)
    bus.write(0x0202, 0xC8);                           // RET Z

    const GB::u16 spBefore = cpu.getSP();

    cpu.step(); // XOR A → Z=1
    expect(cpu.registers().flagZ(), "XOR A should set Z for conditional CALL setup");

    cpu.step(); // CALL Z, 0x0200 (taken because Z=1)
    expect(cpu.getPC() == 0x0200, "CALL Z taken should jump to 0x0200");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "CALL Z should push return address");
    expect(bus.readWord(cpu.getSP()) == 0x0104, "CALL Z return address should be 0x0104");

    cpu.step(); // LD A, 0xCC (Z still 1, LD doesn't change flags)
    expect(cpu.registers().A == 0xCC, "Callee body LD A,0xCC should execute");

    cpu.step(); // RET Z (taken because Z still 1)
    expect(cpu.getPC() == 0x0104, "RET Z taken should return to 0x0104");
    expect(cpu.getSP() == spBefore, "RET Z should restore SP");

    cpu.step(); // LD A, 0x11
    expect(cpu.registers().A == 0x11, "Post-return instruction LD A,0x11 should execute");
}

// ── Tranche 5: ADC matrix, conditional not-taken timing, CB (HL) path ───────

void testAdcConsumesCarryIn() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD A,0xF0 ; ADD A,0x20 -> A=0x10, C=1
    // ADC A,0x0E uses carry-in: 0x10 + 0x0E + 1 = 0x1F
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0xF0); // LD A,0xF0
    bus.write(0x0102, 0xC6); bus.write(0x0103, 0x20); // ADD A,0x20
    bus.write(0x0104, 0xCE); bus.write(0x0105, 0x0E); // ADC A,0x0E

    cpu.step();
    cpu.step();
    expect(cpu.registers().A == 0x10, "Carry priming ADD should wrap to 0x10");
    expect(cpu.registers().flagC(), "Carry priming ADD should set C=1");

    cpu.step(); // ADC A,0x0E
    expect(cpu.registers().A == 0x1F, "ADC must include carry-in: 0x10+0x0E+1=0x1F");
    expect(!cpu.registers().flagZ(), "ADC result 0x1F should clear Z");
    expect(!cpu.registers().flagN(), "ADC is addition: N should clear");
    expect(!cpu.registers().flagC(), "No full carry expected for 0x1F");
}

void testAdcSetsHalfCarryAndCarry() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Prime carry with ADD overflow, then load A without touching flags.
    // ADC 0x8F + 0x70 + 1 = 0x100 -> A=0x00, Z=1, H=1, C=1
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0xF0); // LD A,0xF0
    bus.write(0x0102, 0xC6); bus.write(0x0103, 0x20); // ADD A,0x20 -> C=1
    bus.write(0x0104, 0x3E); bus.write(0x0105, 0x8F); // LD A,0x8F (flags unchanged)
    bus.write(0x0106, 0xCE); bus.write(0x0107, 0x70); // ADC A,0x70

    cpu.step();
    cpu.step();
    expect(cpu.registers().flagC(), "Carry priming ADD should set C=1");

    cpu.step(); // LD A,0x8F, carry remains set
    expect(cpu.registers().A == 0x8F, "LD A,0x8F should set operand base for ADC");
    expect(cpu.registers().flagC(), "LD should preserve C=1 for ADC input");

    cpu.step(); // ADC
    expect(cpu.registers().A == 0x00, "ADC 0x8F+0x70+1 should wrap to 0x00");
    expect(cpu.registers().flagZ(), "ADC wrapped zero should set Z");
    expect(!cpu.registers().flagN(), "ADC should clear N");
    expect(cpu.registers().flagH(), "ADC should set H for nibble overflow");
    expect(cpu.registers().flagC(), "ADC should set C for full overflow");
}

void testConditionalNotTakenCycles() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Build Z=0, then execute JP Z / CALL Z / RET Z in not-taken path.
    // Verify PC fall-through, SP unchanged, and documented cycle counts.
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x01); // LD A,0x01
    bus.write(0x0102, 0xB7);                           // OR A (Z=0)
    bus.write(0x0103, 0xCA); bus.write(0x0104, 0x34); bus.write(0x0105, 0x12); // JP Z,0x1234
    bus.write(0x0106, 0xCC); bus.write(0x0107, 0x78); bus.write(0x0108, 0x56); // CALL Z,0x5678
    bus.write(0x0109, 0xC8); // RET Z

    const GB::u16 spBefore = cpu.getSP();

    cpu.step();
    cpu.step();
    expect(!cpu.registers().flagZ(), "Setup should leave Z=0");

    const GB::u32 jpCycles = cpu.step();
    expect(jpCycles == 12, "JP Z not taken should consume 12 cycles");
    expect(cpu.getPC() == 0x0106, "JP Z not-taken should fall through");
    expect(cpu.getSP() == spBefore, "JP Z not-taken should not touch stack");

    const GB::u32 callCycles = cpu.step();
    expect(callCycles == 12, "CALL Z not taken should consume 12 cycles");
    expect(cpu.getPC() == 0x0109, "CALL Z not-taken should fall through");
    expect(cpu.getSP() == spBefore, "CALL Z not-taken should not push stack");

    const GB::u32 retCycles = cpu.step();
    expect(retCycles == 8, "RET Z not taken should consume 8 cycles");
    expect(cpu.getPC() == 0x010A, "RET Z not-taken should fall through");
    expect(cpu.getSP() == spBefore, "RET Z not-taken should not pop stack");
}

void testCbOpsOnHlIndirectPath() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Exercise CB handlers on (HL) to validate memory operand path and cycles.
    // Start at (HL)=0x81:
    //   SRA (HL) -> 0xC0 (C=1)
    //   SET 7,(HL) keeps 0xC0
    //   RES 0,(HL) keeps 0xC0
    //   SRL (HL) -> 0x60 (C=0)
    bus.write(0x0100, 0x26); bus.write(0x0101, 0xC1); // LD H,0xC1
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x00); // LD L,0x00
    bus.write(0x0104, 0x36); bus.write(0x0105, 0x81); // LD (HL),0x81
    bus.write(0x0106, 0xCB); bus.write(0x0107, 0x2E); // SRA (HL)
    bus.write(0x0108, 0xCB); bus.write(0x0109, 0xFE); // SET 7,(HL)
    bus.write(0x010A, 0xCB); bus.write(0x010B, 0x86); // RES 0,(HL)
    bus.write(0x010C, 0xCB); bus.write(0x010D, 0x3E); // SRL (HL)

    cpu.step();
    cpu.step();
    cpu.step();
    expect(cpu.registers().HL() == 0xC100, "HL should point to 0xC100 for CB (HL) tests");
    expect(bus.read(0xC100) == 0x81, "Seed write for (HL) should be 0x81");

    const GB::u32 sraCycles = cpu.step();
    expect(sraCycles == 20, "CB SRA (HL) should consume 20 cycles");
    expect(bus.read(0xC100) == 0xC0, "SRA (HL): 0x81 -> 0xC0");
    expect(cpu.registers().flagC(), "SRA (HL) should set C from old bit0");

    const GB::u32 setCycles = cpu.step();
    expect(setCycles == 20, "CB SET 7,(HL) should consume 20 cycles");
    expect(bus.read(0xC100) == 0xC0, "SET 7,(HL) on 0xC0 should remain 0xC0");

    const GB::u32 resCycles = cpu.step();
    expect(resCycles == 20, "CB RES 0,(HL) should consume 20 cycles");
    expect(bus.read(0xC100) == 0xC0, "RES 0,(HL) on 0xC0 should remain 0xC0");

    const GB::u32 srlCycles = cpu.step();
    expect(srlCycles == 20, "CB SRL (HL) should consume 20 cycles");
    expect(bus.read(0xC100) == 0x60, "SRL (HL): 0xC0 -> 0x60");
    expect(!cpu.registers().flagC(), "SRL (HL) on 0xC0 should clear C (old bit0=0)");
}

// ── Tranche 6: ADC/SBC register+(HL), POP AF masking, CPL/SCF/CCF ───────────

void testAdcRegisterVariant() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Prime C=1, then ADC A,B (0x88) should consume carry and B register.
    // A=0x10, B=0x22, C=1 => 0x33.
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0xF0); // LD A,0xF0
    bus.write(0x0102, 0xC6); bus.write(0x0103, 0x20); // ADD A,0x20 -> C=1
    bus.write(0x0104, 0x3E); bus.write(0x0105, 0x10); // LD A,0x10
    bus.write(0x0106, 0x06); bus.write(0x0107, 0x22); // LD B,0x22
    bus.write(0x0108, 0x88);                           // ADC A,B

    cpu.step();
    cpu.step();
    expect(cpu.registers().flagC(), "Carry priming ADD should set C=1");

    cpu.step();
    cpu.step();
    const GB::u32 cycles = cpu.step();
    expect(cycles == 4, "ADC A,B should consume 4 cycles");
    expect(cpu.registers().A == 0x33, "ADC A,B should compute 0x10+0x22+1=0x33");
    expect(!cpu.registers().flagC(), "ADC A,B result 0x33 should clear C");
}

void testSbcRegisterVariant() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Prime C=1, then SBC A,B (0x98): 0x20 - 0x05 - 1 = 0x1A.
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0xF0); // LD A,0xF0
    bus.write(0x0102, 0xC6); bus.write(0x0103, 0x20); // ADD A,0x20 -> C=1
    bus.write(0x0104, 0x3E); bus.write(0x0105, 0x20); // LD A,0x20
    bus.write(0x0106, 0x06); bus.write(0x0107, 0x05); // LD B,0x05
    bus.write(0x0108, 0x98);                           // SBC A,B

    cpu.step();
    cpu.step();
    expect(cpu.registers().flagC(), "Carry priming ADD should set C=1");

    cpu.step();
    cpu.step();
    const GB::u32 cycles = cpu.step();
    expect(cycles == 4, "SBC A,B should consume 4 cycles");
    expect(cpu.registers().A == 0x1A, "SBC A,B should compute 0x20-0x05-1=0x1A");
    expect(cpu.registers().flagN(), "SBC should set N");
    expect(!cpu.registers().flagC(), "SBC result 0x1A should not borrow");
}

void testAdcSbcHlIndirectVariants() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Exercise ADC A,(HL) and SBC A,(HL) through memory operand path (SRC_IDX=6).
    // ADC: 0x10 + [HL]=0x0E + carry(1) => 0x1F
    // For SBC, re-prime carry first, then SBC: 0x20 - [HL]=0x05 - carry(1) => 0x1A
    bus.write(0x0100, 0x26); bus.write(0x0101, 0xC2); // LD H,0xC2
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x00); // LD L,0x00
    bus.write(0x0104, 0x36); bus.write(0x0105, 0x0E); // LD (HL),0x0E
    bus.write(0x0106, 0x3E); bus.write(0x0107, 0xF0); // LD A,0xF0
    bus.write(0x0108, 0xC6); bus.write(0x0109, 0x20); // ADD A,0x20 -> C=1
    bus.write(0x010A, 0x3E); bus.write(0x010B, 0x10); // LD A,0x10
    bus.write(0x010C, 0x8E);                           // ADC A,(HL)
    bus.write(0x010D, 0x36); bus.write(0x010E, 0x05); // LD (HL),0x05
    bus.write(0x010F, 0x3E); bus.write(0x0110, 0xF0); // LD A,0xF0
    bus.write(0x0111, 0xC6); bus.write(0x0112, 0x20); // ADD A,0x20 -> C=1
    bus.write(0x0113, 0x3E); bus.write(0x0114, 0x20); // LD A,0x20
    bus.write(0x0115, 0x9E);                           // SBC A,(HL)

    cpu.step();
    cpu.step();
    cpu.step();
    cpu.step();
    cpu.step();
    cpu.step();

    const GB::u32 adcCycles = cpu.step();
    expect(adcCycles == 8, "ADC A,(HL) should consume 8 cycles");
    expect(cpu.registers().A == 0x1F, "ADC A,(HL) should compute 0x10+0x0E+1=0x1F");

    cpu.step();
    cpu.step();
    cpu.step();
    cpu.step();
    const GB::u32 sbcCycles = cpu.step();
    expect(sbcCycles == 8, "SBC A,(HL) should consume 8 cycles");
    expect(cpu.registers().A == 0x1A, "SBC A,(HL) should compute 0x20-0x05-1=0x1A");
}

void testPopAfMasksLowNibble() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LD SP,0xC300 ; POP AF with stack word 0x12FF.
    // setAF masks F low nibble, so F must become 0xF0 (not 0xFF).
    bus.write(0x0100, 0x31); bus.write(0x0101, 0x00); bus.write(0x0102, 0xC3); // LD SP,0xC300
    bus.write(0x0103, 0xF1); // POP AF
    bus.write(0xC300, 0xFF); // low byte -> F candidate
    bus.write(0xC301, 0x12); // high byte -> A

    cpu.step();
    const GB::u32 cycles = cpu.step();
    expect(cycles == 12, "POP AF should consume 12 cycles");
    expect(cpu.registers().A == 0x12, "POP AF should load A from stack high byte");
    expect(cpu.registers().F == 0xF0, "POP AF should mask F low nibble to zero");
    expect(cpu.getSP() == 0xC302, "POP AF should increment SP by 2");
}

void testCplScfCcfFlagSemantics() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // XOR A sets Z=1 and clears C.
    // SCF should set C and clear N/H while preserving Z.
    // CCF should toggle C and clear N/H while preserving Z.
    // CPL should invert A and set N/H while preserving Z/C.
    bus.write(0x0100, 0xAF); // XOR A
    bus.write(0x0101, 0x37); // SCF
    bus.write(0x0102, 0x3F); // CCF
    bus.write(0x0103, 0x3E); bus.write(0x0104, 0x0F); // LD A,0x0F (flags unchanged)
    bus.write(0x0105, 0x2F); // CPL

    cpu.step(); // XOR A
    expect(cpu.registers().flagZ(), "XOR A should set Z for preservation checks");

    cpu.step(); // SCF
    expect(cpu.registers().flagZ(), "SCF should preserve Z");
    expect(cpu.registers().flagC(), "SCF should set C");
    expect(!cpu.registers().flagN(), "SCF should clear N");
    expect(!cpu.registers().flagH(), "SCF should clear H");

    cpu.step(); // CCF
    expect(cpu.registers().flagZ(), "CCF should preserve Z");
    expect(!cpu.registers().flagC(), "CCF should toggle C from 1 to 0");
    expect(!cpu.registers().flagN(), "CCF should clear N");
    expect(!cpu.registers().flagH(), "CCF should clear H");

    cpu.step(); // LD A,0x0F
    cpu.step(); // CPL
    expect(cpu.registers().A == 0xF0, "CPL should bitwise invert A");
    expect(cpu.registers().flagN(), "CPL should set N");
    expect(cpu.registers().flagH(), "CPL should set H");
    expect(!cpu.registers().flagC(), "CPL should preserve C (expected 0)");
}

// ── Tranche 7: INC/DEC (HL), ADD SP,r8 signed, LDI/LDD pointer wrap ─────────

void testIncHlIndirectFlags() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // HL=0xC400, (HL)=0x0F, INC (HL) => 0x10 with H=1, N=0, Z=0.
    bus.write(0x0100, 0x26); bus.write(0x0101, 0xC4); // LD H,0xC4
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x00); // LD L,0x00
    bus.write(0x0104, 0x36); bus.write(0x0105, 0x0F); // LD (HL),0x0F
    bus.write(0x0106, 0x34);                           // INC (HL)

    cpu.step();
    cpu.step();
    cpu.step();
    const GB::u32 cycles = cpu.step();
    expect(cycles == 12, "INC (HL) should consume 12 cycles");
    expect(bus.read(0xC400) == 0x10, "INC (HL) should increment 0x0F to 0x10");
    expect(!cpu.registers().flagZ(), "INC (HL) result 0x10 should clear Z");
    expect(!cpu.registers().flagN(), "INC should clear N");
    expect(cpu.registers().flagH(), "INC (HL) 0x0F->0x10 should set H");
}

void testDecHlIndirectFlags() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // DEC (HL) edge checks:
    // 0x10 -> 0x0F (H=1, N=1, Z=0), then 0x01 -> 0x00 (H=0, N=1, Z=1).
    bus.write(0x0100, 0x26); bus.write(0x0101, 0xC4); // LD H,0xC4
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x10); // LD L,0x10
    bus.write(0x0104, 0x36); bus.write(0x0105, 0x10); // LD (HL),0x10
    bus.write(0x0106, 0x35);                           // DEC (HL)
    bus.write(0x0107, 0x36); bus.write(0x0108, 0x01); // LD (HL),0x01
    bus.write(0x0109, 0x35);                           // DEC (HL)

    cpu.step();
    cpu.step();
    cpu.step();
    const GB::u32 dec1Cycles = cpu.step();
    expect(dec1Cycles == 12, "DEC (HL) should consume 12 cycles");
    expect(bus.read(0xC410) == 0x0F, "DEC (HL) should decrement 0x10 to 0x0F");
    expect(!cpu.registers().flagZ(), "DEC 0x10->0x0F should clear Z");
    expect(cpu.registers().flagN(), "DEC should set N");
    expect(cpu.registers().flagH(), "DEC 0x10->0x0F should set H");

    cpu.step();
    const GB::u32 dec2Cycles = cpu.step();
    expect(dec2Cycles == 12, "DEC (HL) second case should consume 12 cycles");
    expect(bus.read(0xC410) == 0x00, "DEC (HL) should decrement 0x01 to 0x00");
    expect(cpu.registers().flagZ(), "DEC 0x01->0x00 should set Z");
    expect(cpu.registers().flagN(), "DEC should keep N set");
    expect(!cpu.registers().flagH(), "DEC 0x01->0x00 should clear H");
}

void testAddSpR8SignedMatrix() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Matrix:
    // 1) SP=0x00FF; ADD SP,+1  -> 0x0100, H=1, C=1
    // 2) SP=0x0008; ADD SP,-1  -> 0x0007, H=1, C=1
    // 3) SP=0x1234; ADD SP,+2  -> 0x1236, H=0, C=0
    bus.write(0x0100, 0x31); bus.write(0x0101, 0xFF); bus.write(0x0102, 0x00); // LD SP,0x00FF
    bus.write(0x0103, 0xE8); bus.write(0x0104, 0x01);                           // ADD SP,+1
    bus.write(0x0105, 0x31); bus.write(0x0106, 0x08); bus.write(0x0107, 0x00); // LD SP,0x0008
    bus.write(0x0108, 0xE8); bus.write(0x0109, 0xFF);                           // ADD SP,-1
    bus.write(0x010A, 0x31); bus.write(0x010B, 0x34); bus.write(0x010C, 0x12); // LD SP,0x1234
    bus.write(0x010D, 0xE8); bus.write(0x010E, 0x02);                           // ADD SP,+2

    cpu.step();
    const GB::u32 add1Cycles = cpu.step();
    expect(add1Cycles == 16, "ADD SP,r8 should consume 16 cycles");
    expect(cpu.getSP() == 0x0100, "ADD SP,+1 should produce 0x0100");
    expect(!cpu.registers().flagZ(), "ADD SP,r8 always clears Z");
    expect(!cpu.registers().flagN(), "ADD SP,r8 always clears N");
    expect(cpu.registers().flagH(), "ADD SP,+1 from 0x00FF should set H");
    expect(cpu.registers().flagC(), "ADD SP,+1 from 0x00FF should set C");

    cpu.step();
    const GB::u32 add2Cycles = cpu.step();
    expect(add2Cycles == 16, "ADD SP,r8 second case should consume 16 cycles");
    expect(cpu.getSP() == 0x0007, "ADD SP,-1 from 0x0008 should produce 0x0007");
    expect(cpu.registers().flagH(), "ADD SP,-1 from 0x0008 should set H");
    expect(cpu.registers().flagC(), "ADD SP,-1 from 0x0008 should set C");

    cpu.step();
    const GB::u32 add3Cycles = cpu.step();
    expect(add3Cycles == 16, "ADD SP,r8 third case should consume 16 cycles");
    expect(cpu.getSP() == 0x1236, "ADD SP,+2 from 0x1234 should produce 0x1236");
    expect(!cpu.registers().flagH(), "ADD SP,+2 from 0x1234 should clear H");
    expect(!cpu.registers().flagC(), "ADD SP,+2 from 0x1234 should clear C");
}

void testLdiPointerWrapBehavior() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LDI write wrap: HL=0xFFFF, LDI (HL),A should write and wrap HL->0x0000.
    // LDI read path: HL=0x0000, LDI A,(HL) should read and increment HL->0x0001.
    bus.write(0x0100, 0x21); bus.write(0x0101, 0xFF); bus.write(0x0102, 0xFF); // LD HL,0xFFFF
    bus.write(0x0103, 0x3E); bus.write(0x0104, 0x5A);                           // LD A,0x5A
    bus.write(0x0105, 0x22);                                                     // LDI (HL),A
    bus.write(0x0106, 0x21); bus.write(0x0107, 0x00); bus.write(0x0108, 0x00); // LD HL,0x0000
    bus.write(0x0109, 0x2A);                                                     // LDI A,(HL)

    bus.write(0x0000, 0xA5);

    cpu.step();
    cpu.step();
    const GB::u32 ldiWriteCycles = cpu.step();
    expect(ldiWriteCycles == 8, "LDI (HL),A should consume 8 cycles");
    expect(bus.read(0xFFFF) == 0x5A, "LDI (HL),A should write A to 0xFFFF");
    expect(cpu.registers().HL() == 0x0000, "LDI (HL),A should wrap HL to 0x0000");

    cpu.step();
    const GB::u32 ldiReadCycles = cpu.step();
    expect(ldiReadCycles == 8, "LDI A,(HL) should consume 8 cycles");
    expect(cpu.registers().A == 0xA5, "LDI A,(HL) should read from 0x0000");
    expect(cpu.registers().HL() == 0x0001, "LDI A,(HL) should increment HL");
}

void testLddPointerWrapBehavior() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // LDD write: HL=0x0000, LDD (HL),A should write and wrap HL->0xFFFF.
    // LDD read: then LDD A,(HL) from 0xFFFF should read and decrement HL->0xFFFE.
    bus.write(0x0100, 0x21); bus.write(0x0101, 0x00); bus.write(0x0102, 0x00); // LD HL,0x0000
    bus.write(0x0103, 0x3E); bus.write(0x0104, 0x3C);                           // LD A,0x3C
    bus.write(0x0105, 0x32);                                                     // LDD (HL),A
    bus.write(0x0106, 0x3A);                                                     // LDD A,(HL)

    bus.write(0xFFFF, 0x77);

    cpu.step();
    cpu.step();
    const GB::u32 lddWriteCycles = cpu.step();
    expect(lddWriteCycles == 8, "LDD (HL),A should consume 8 cycles");
    expect(bus.read(0x0000) == 0x3C, "LDD (HL),A should write A to 0x0000");
    expect(cpu.registers().HL() == 0xFFFF, "LDD (HL),A should decrement HL to 0xFFFF");

    const GB::u32 lddReadCycles = cpu.step();
    expect(lddReadCycles == 8, "LDD A,(HL) should consume 8 cycles");
    expect(cpu.registers().A == 0x77, "LDD A,(HL) should read from 0xFFFF");
    expect(cpu.registers().HL() == 0xFFFE, "LDD A,(HL) should decrement HL");
}

// ── Tranche 8: taken-path timing, RST vectors, HALT/EI interactions ─────────

void testTakenControlFlowCyclesAndStack() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // XOR A -> Z=1
    // JP Z,0x0200 (taken, 16)
    // CALL Z,0x0300 (taken, 24) pushes 0x0203
    // RET Z (taken, 20) pops back to 0x0203
    bus.write(0x0100, 0xAF); // XOR A
    bus.write(0x0101, 0xCA); bus.write(0x0102, 0x00); bus.write(0x0103, 0x02); // JP Z,0x0200
    bus.write(0x0200, 0xCC); bus.write(0x0201, 0x00); bus.write(0x0202, 0x03); // CALL Z,0x0300
    bus.write(0x0203, 0x00); // NOP (return landing)
    bus.write(0x0300, 0xC8); // RET Z

    const GB::u16 spBefore = cpu.getSP();

    cpu.step(); // XOR A
    expect(cpu.registers().flagZ(), "Setup should set Z for taken control-flow path");

    const GB::u32 jpCycles = cpu.step();
    expect(jpCycles == 16, "JP Z taken should consume 16 cycles");
    expect(cpu.getPC() == 0x0200, "JP Z taken should jump to 0x0200");

    const GB::u32 callCycles = cpu.step();
    expect(callCycles == 24, "CALL Z taken should consume 24 cycles");
    expect(cpu.getPC() == 0x0300, "CALL Z taken should jump to 0x0300");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "CALL Z taken should push return address");
    expect(bus.readWord(cpu.getSP()) == 0x0203, "CALL Z should push return address 0x0203");

    const GB::u32 retCycles = cpu.step();
    expect(retCycles == 20, "RET Z taken should consume 20 cycles");
    expect(cpu.getPC() == 0x0203, "RET Z taken should return to 0x0203");
    expect(cpu.getSP() == spBefore, "RET Z taken should restore SP");
}

void testRstVectorsAndStack() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // RST 00 then RET, then RST 38 then RET.
    bus.write(0x0100, 0xC7); // RST 00
    bus.write(0x0101, 0xFF); // RST 38
    bus.write(0x0102, 0x00); // NOP

    bus.write(0x0000, 0xC9); // RET from vector 00
    bus.write(0x0038, 0xC9); // RET from vector 38

    const GB::u16 spBefore = cpu.getSP();

    const GB::u32 rst00Cycles = cpu.step();
    expect(rst00Cycles == 16, "RST 00 should consume 16 cycles");
    expect(cpu.getPC() == 0x0000, "RST 00 should jump to vector 0x0000");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "RST 00 should push return address");
    expect(bus.readWord(cpu.getSP()) == 0x0101, "RST 00 should push return address 0x0101");

    const GB::u32 ret00Cycles = cpu.step();
    expect(ret00Cycles == 16, "RET should consume 16 cycles");
    expect(cpu.getPC() == 0x0101, "RET from 0x0000 should return to 0x0101");
    expect(cpu.getSP() == spBefore, "RET should restore SP after RST 00");

    const GB::u32 rst38Cycles = cpu.step();
    expect(rst38Cycles == 16, "RST 38 should consume 16 cycles");
    expect(cpu.getPC() == 0x0038, "RST 38 should jump to vector 0x0038");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "RST 38 should push return address");
    expect(bus.readWord(cpu.getSP()) == 0x0102, "RST 38 should push return address 0x0102");

    const GB::u32 ret38Cycles = cpu.step();
    expect(ret38Cycles == 16, "RET should consume 16 cycles after RST 38");
    expect(cpu.getPC() == 0x0102, "RET from 0x0038 should return to 0x0102");
    expect(cpu.getSP() == spBefore, "RET should restore SP after RST 38");
}

void testEiThenHaltServicesPendingInterrupt() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // With IF+IE pre-set:
    // EI (IME pending) -> HALT (sets halted, then IME active from deferred EI) ->
    // next step services IRQ before opcode fetch.
    bus.write(0x0100, 0xFB); // EI
    bus.write(0x0101, 0x76); // HALT
    bus.write(0x0102, 0x00); // NOP (should not execute before IRQ service)

    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(GB::Interrupt::VBlank));
    bus.write(GB::REG_IF,      static_cast<GB::u8>(GB::Interrupt::VBlank));

    const GB::u16 spBefore = cpu.getSP();

    const GB::u32 eiCycles = cpu.step();
    expect(eiCycles == 4, "EI should consume 4 cycles");

    const GB::u32 haltCycles = cpu.step();
    expect(haltCycles == 4, "HALT opcode execution should consume 4 cycles");

    const GB::u32 irqCycles = cpu.step();
    expect(irqCycles == 20, "Interrupt service should consume 20 cycles");
    expect(cpu.getPC() == 0x0040, "Pending VBlank should vector to 0x0040");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "IRQ service should push return address");
    expect(bus.readWord(cpu.getSP()) == 0x0102, "IRQ return address should be HALT successor 0x0102");

    const GB::u8 ifReg = bus.read(GB::REG_IF);
    expect((ifReg & static_cast<GB::u8>(GB::Interrupt::VBlank)) == 0, "Serviced interrupt flag should be cleared");
}

void testHaltWakeWithoutImeExecutesNextOpcode() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // IME stays false. HALT should spin 4 cycles until pending interrupt wakes it.
    // Because IME is false, wake does not service IRQ and execution continues at next opcode.
    bus.write(0x0100, 0x76);                           // HALT
    bus.write(0x0101, 0x3E); bus.write(0x0102, 0x66); // LD A,0x66

    const GB::u32 haltCycles = cpu.step();
    expect(haltCycles == 4, "HALT should consume 4 cycles when entered");
    expect(cpu.getPC() == 0x0101, "After HALT fetch, PC should point to next opcode");

    const GB::u32 spinCycles = cpu.step();
    expect(spinCycles == 4, "HALT spin with no pending interrupt should consume 4 cycles");
    expect(cpu.getPC() == 0x0101, "HALT spin should not advance PC");

    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(GB::Interrupt::VBlank));
    bus.write(GB::REG_IF,      static_cast<GB::u8>(GB::Interrupt::VBlank));

    const GB::u32 wakeCycles = cpu.step();
    expect(wakeCycles == 8, "Wake without IME should execute LD A,d8 in 8 cycles");
    expect(cpu.registers().A == 0x66, "Wake path should execute next opcode after HALT");
    expect(cpu.getPC() == 0x0103, "LD A,d8 execution should advance PC to 0x0103");
}

// ── Tranche 9: RETI multi-source, C/NC taken paths, CB cycle matrix ─────────

void testRetiMultiSourcePriority() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // RETI should re-enable IME immediately.
    // On next step, pending interrupts should be serviced in priority order.
    // Here pending: Timer(bit2) + Joypad(bit4) => vector should be 0x0050.
    bus.write(0x0100, 0xD9); // RETI
    cpu.registers().SP = 0xC500;
    bus.write(0xC500, 0x10); // return lo -> 0x0210
    bus.write(0xC501, 0x02); // return hi

    const GB::u8 timerBit  = static_cast<GB::u8>(GB::Interrupt::Timer);
    const GB::u8 joypadBit = static_cast<GB::u8>(GB::Interrupt::Joypad);
    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(timerBit | joypadBit));
    bus.write(GB::REG_IF,      static_cast<GB::u8>(timerBit | joypadBit));

    const GB::u32 retiCycles = cpu.step();
    expect(retiCycles == 16, "RETI should consume 16 cycles");
    expect(cpu.getPC() == 0x0210, "RETI should pop return address 0x0210");
    expect(cpu.getSP() == 0xC502, "RETI should pop 2 bytes from stack");

    const GB::u16 spBeforeIrq = cpu.getSP();
    const GB::u32 irqCycles = cpu.step();
    expect(irqCycles == 20, "Interrupt dispatch should consume 20 cycles");
    expect(cpu.getPC() == 0x0050, "Timer interrupt should be selected over Joypad");
    expect(cpu.getSP() == static_cast<GB::u16>(spBeforeIrq - 2), "Interrupt service should push return PC");
    expect(bus.readWord(cpu.getSP()) == 0x0210, "Interrupt return address should be 0x0210");

    const GB::u8 ifAfter = bus.read(GB::REG_IF);
    expect((ifAfter & timerBit) == 0, "Serviced Timer IF bit should be cleared");
    expect((ifAfter & joypadBit) != 0, "Unserviced Joypad IF bit should remain set");
}

void testCarryConditionalTakenMatrix() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // SCF sets C=1.
    // JP C taken (16), CALL C taken (24) pushes 0x0203, RET C taken (20) restores.
    bus.write(0x0100, 0x37); // SCF
    bus.write(0x0101, 0xDA); bus.write(0x0102, 0x00); bus.write(0x0103, 0x02); // JP C,0x0200
    bus.write(0x0200, 0xDC); bus.write(0x0201, 0x00); bus.write(0x0202, 0x03); // CALL C,0x0300
    bus.write(0x0203, 0x00);                                                     // NOP landing
    bus.write(0x0300, 0xD8);                                                     // RET C

    const GB::u16 spBefore = cpu.getSP();

    cpu.step();
    expect(cpu.registers().flagC(), "SCF should set C for taken C-conditional flow");

    const GB::u32 jpCycles = cpu.step();
    expect(jpCycles == 16, "JP C taken should consume 16 cycles");
    expect(cpu.getPC() == 0x0200, "JP C taken should jump to 0x0200");

    const GB::u32 callCycles = cpu.step();
    expect(callCycles == 24, "CALL C taken should consume 24 cycles");
    expect(cpu.getPC() == 0x0300, "CALL C taken should jump to 0x0300");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "CALL C taken should push return");
    expect(bus.readWord(cpu.getSP()) == 0x0203, "CALL C return address should be 0x0203");

    const GB::u32 retCycles = cpu.step();
    expect(retCycles == 20, "RET C taken should consume 20 cycles");
    expect(cpu.getPC() == 0x0203, "RET C taken should return to 0x0203");
    expect(cpu.getSP() == spBefore, "RET C taken should restore SP");
}

void testNoCarryConditionalTakenMatrix() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Ensure C=0 via XOR A.
    // JP NC taken (16), CALL NC taken (24), RET NC taken (20).
    bus.write(0x0100, 0xAF); // XOR A -> C=0
    bus.write(0x0101, 0xD2); bus.write(0x0102, 0x00); bus.write(0x0103, 0x02); // JP NC,0x0200
    bus.write(0x0200, 0xD4); bus.write(0x0201, 0x00); bus.write(0x0202, 0x03); // CALL NC,0x0300
    bus.write(0x0203, 0x00);                                                     // NOP landing
    bus.write(0x0300, 0xD0);                                                     // RET NC

    const GB::u16 spBefore = cpu.getSP();

    cpu.step();
    expect(!cpu.registers().flagC(), "XOR A should clear C for taken NC-conditional flow");

    const GB::u32 jpCycles = cpu.step();
    expect(jpCycles == 16, "JP NC taken should consume 16 cycles");
    expect(cpu.getPC() == 0x0200, "JP NC taken should jump to 0x0200");

    const GB::u32 callCycles = cpu.step();
    expect(callCycles == 24, "CALL NC taken should consume 24 cycles");
    expect(cpu.getPC() == 0x0300, "CALL NC taken should jump to 0x0300");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "CALL NC taken should push return");
    expect(bus.readWord(cpu.getSP()) == 0x0203, "CALL NC return address should be 0x0203");

    const GB::u32 retCycles = cpu.step();
    expect(retCycles == 20, "RET NC taken should consume 20 cycles");
    expect(cpu.getPC() == 0x0203, "RET NC taken should return to 0x0203");
    expect(cpu.getSP() == spBefore, "RET NC taken should restore SP");
}

void testCbBitResSetCycleMatrix() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Compare CB BIT/RES/SET cycles and effects for register vs (HL) forms.
    bus.write(0x0100, 0x26); bus.write(0x0101, 0xC6); // LD H,0xC6
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x00); // LD L,0x00
    bus.write(0x0104, 0x06); bus.write(0x0105, 0x01); // LD B,0x01
    bus.write(0x0106, 0x36); bus.write(0x0107, 0x01); // LD (HL),0x01

    bus.write(0x0108, 0xCB); bus.write(0x0109, 0x40); // BIT 0,B
    bus.write(0x010A, 0xCB); bus.write(0x010B, 0x46); // BIT 0,(HL)
    bus.write(0x010C, 0xCB); bus.write(0x010D, 0x80); // RES 0,B
    bus.write(0x010E, 0xCB); bus.write(0x010F, 0x86); // RES 0,(HL)
    bus.write(0x0110, 0xCB); bus.write(0x0111, 0xC0); // SET 0,B
    bus.write(0x0112, 0xCB); bus.write(0x0113, 0xC6); // SET 0,(HL)

    cpu.step();
    cpu.step();
    cpu.step();
    cpu.step();

    const GB::u32 bitRegCycles = cpu.step();
    expect(bitRegCycles == 12, "CB BIT 0,B should consume 12 cycles (prefix+op)");
    expect(!cpu.registers().flagZ(), "BIT 0,B with B=1 should clear Z");

    const GB::u32 bitMemCycles = cpu.step();
    expect(bitMemCycles == 20, "CB BIT 0,(HL) should consume 20 cycles");
    expect(!cpu.registers().flagZ(), "BIT 0,(HL) with value=1 should clear Z");

    const GB::u32 resRegCycles = cpu.step();
    expect(resRegCycles == 12, "CB RES 0,B should consume 12 cycles (prefix+op)");
    expect(cpu.registers().B == 0x00, "RES 0,B should clear bit 0 in B");

    const GB::u32 resMemCycles = cpu.step();
    expect(resMemCycles == 20, "CB RES 0,(HL) should consume 20 cycles");
    expect(bus.read(0xC600) == 0x00, "RES 0,(HL) should clear bit 0 in memory");

    const GB::u32 setRegCycles = cpu.step();
    expect(setRegCycles == 12, "CB SET 0,B should consume 12 cycles (prefix+op)");
    expect(cpu.registers().B == 0x01, "SET 0,B should set bit 0 in B");

    const GB::u32 setMemCycles = cpu.step();
    expect(setMemCycles == 20, "CB SET 0,(HL) should consume 20 cycles");
    expect(bus.read(0xC600) == 0x01, "SET 0,(HL) should set bit 0 in memory");
}

// ── Tranche 10: RETI chaining, JP HL/JP a16, extra RST, HALT timer wake ─────

void testRetiChainsPendingInterrupts() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Boot through RETI, then chain two pending interrupts (LCD STAT then Serial).
    // Vector handler at 0x0048 executes RETI so IME is re-enabled for the second IRQ.
    bus.write(0x0100, 0xD9); // RETI
    bus.write(0x0048, 0xD9); // RETI inside first ISR

    cpu.registers().SP = 0xC700;
    bus.write(0xC700, 0x10); // return lo -> 0x0210
    bus.write(0xC701, 0x02); // return hi

    const GB::u8 lcdBit    = static_cast<GB::u8>(GB::Interrupt::LCDStat);
    const GB::u8 serialBit = static_cast<GB::u8>(GB::Interrupt::Serial);
    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(lcdBit | serialBit));
    bus.write(GB::REG_IF,      static_cast<GB::u8>(lcdBit | serialBit));

    const GB::u32 reti0Cycles = cpu.step();
    expect(reti0Cycles == 16, "Initial RETI should consume 16 cycles");
    expect(cpu.getPC() == 0x0210, "Initial RETI should restore PC=0x0210");

    const GB::u16 spBeforeIrq1 = cpu.getSP();
    const GB::u32 irq1Cycles = cpu.step();
    expect(irq1Cycles == 20, "First interrupt service should consume 20 cycles");
    expect(cpu.getPC() == 0x0048, "Highest-priority pending interrupt should vector to 0x0048");
    expect(cpu.getSP() == static_cast<GB::u16>(spBeforeIrq1 - 2), "IRQ service should push return PC");
    expect(bus.readWord(cpu.getSP()) == 0x0210, "First IRQ return address should be 0x0210");

    const GB::u32 reti1Cycles = cpu.step();
    expect(reti1Cycles == 16, "ISR RETI should consume 16 cycles");
    expect(cpu.getPC() == 0x0210, "ISR RETI should return to 0x0210");

    const GB::u16 spBeforeIrq2 = cpu.getSP();
    const GB::u32 irq2Cycles = cpu.step();
    expect(irq2Cycles == 20, "Second interrupt service should consume 20 cycles");
    expect(cpu.getPC() == 0x0058, "Second pending interrupt should vector to 0x0058");
    expect(cpu.getSP() == static_cast<GB::u16>(spBeforeIrq2 - 2), "Second IRQ should push return PC");
    expect(bus.readWord(cpu.getSP()) == 0x0210, "Second IRQ return address should be 0x0210");

    const GB::u8 ifAfter = bus.read(GB::REG_IF);
    expect((ifAfter & lcdBit) == 0, "LCD STAT IF bit should be cleared after service");
    expect((ifAfter & serialBit) == 0, "Serial IF bit should be cleared after service");
}

void testJpAbsoluteAndJpHlMatrix() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // JP a16 to 0x0200, then LD HL,0x1234, then JP HL to 0x1234.
    bus.write(0x0100, 0xC3); bus.write(0x0101, 0x00); bus.write(0x0102, 0x02); // JP 0x0200
    bus.write(0x0200, 0x21); bus.write(0x0201, 0x34); bus.write(0x0202, 0x12); // LD HL,0x1234
    bus.write(0x0203, 0xE9);                                                     // JP HL
    bus.write(0x1234, 0x3E); bus.write(0x1235, 0x7B);                           // LD A,0x7B

    const GB::u32 jpAbsCycles = cpu.step();
    expect(jpAbsCycles == 16, "JP a16 should consume 16 cycles");
    expect(cpu.getPC() == 0x0200, "JP a16 should jump to 0x0200");

    cpu.step(); // LD HL,d16
    expect(cpu.registers().HL() == 0x1234, "LD HL,d16 should load 0x1234");

    const GB::u32 jpHlCycles = cpu.step();
    expect(jpHlCycles == 4, "JP HL should consume 4 cycles");
    expect(cpu.getPC() == 0x1234, "JP HL should jump to address in HL");

    cpu.step();
    expect(cpu.registers().A == 0x7B, "Instruction at JP HL target should execute");
}

void testAdditionalRstVectors() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Validate vectors 0x0008 and 0x0030 with stack round-trip.
    bus.write(0x0100, 0xCF); // RST 08
    bus.write(0x0101, 0xF7); // RST 30
    bus.write(0x0102, 0x00); // NOP

    bus.write(0x0008, 0xC9); // RET
    bus.write(0x0030, 0xC9); // RET

    const GB::u16 spBefore = cpu.getSP();

    const GB::u32 rst08Cycles = cpu.step();
    expect(rst08Cycles == 16, "RST 08 should consume 16 cycles");
    expect(cpu.getPC() == 0x0008, "RST 08 should jump to vector 0x0008");
    expect(bus.readWord(cpu.getSP()) == 0x0101, "RST 08 should push return address 0x0101");

    const GB::u32 ret08Cycles = cpu.step();
    expect(ret08Cycles == 16, "RET after RST 08 should consume 16 cycles");
    expect(cpu.getPC() == 0x0101, "RET from 0x0008 should return to 0x0101");
    expect(cpu.getSP() == spBefore, "SP should be restored after RST 08 round-trip");

    const GB::u32 rst30Cycles = cpu.step();
    expect(rst30Cycles == 16, "RST 30 should consume 16 cycles");
    expect(cpu.getPC() == 0x0030, "RST 30 should jump to vector 0x0030");
    expect(bus.readWord(cpu.getSP()) == 0x0102, "RST 30 should push return address 0x0102");

    const GB::u32 ret30Cycles = cpu.step();
    expect(ret30Cycles == 16, "RET after RST 30 should consume 16 cycles");
    expect(cpu.getPC() == 0x0102, "RET from 0x0030 should return to 0x0102");
    expect(cpu.getSP() == spBefore, "SP should be restored after RST 30 round-trip");
}

void testHaltWakeOnTimerWithoutIme() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // HALT with IME=0 should wake on pending interrupt but continue normal execution.
    // Use Timer source to broaden beyond VBlank.
    bus.write(0x0100, 0x76);                           // HALT
    bus.write(0x0101, 0x06); bus.write(0x0102, 0x99); // LD B,0x99

    const GB::u32 haltCycles = cpu.step();
    expect(haltCycles == 4, "HALT entry should consume 4 cycles");

    const GB::u32 spinCycles = cpu.step();
    expect(spinCycles == 4, "HALT idle spin should consume 4 cycles");

    const GB::u8 timerBit = static_cast<GB::u8>(GB::Interrupt::Timer);
    bus.write(GB::IE_REGISTER, timerBit);
    bus.write(GB::REG_IF,      timerBit);

    const GB::u32 wakeExecCycles = cpu.step();
    expect(wakeExecCycles == 8, "Wake without IME should execute LD B,d8 in 8 cycles");
    expect(cpu.registers().B == 0x99, "Wake path should execute next opcode after HALT");
    expect(cpu.getPC() == 0x0103, "Execution should continue at next sequential PC");
    expect(cpu.getPC() != 0x0050, "IME-off wake should not vector to Timer ISR");

    const GB::u8 ifAfter = bus.read(GB::REG_IF);
    expect((ifAfter & timerBit) != 0, "Timer IF bit should remain set when not serviced");
}

// ── Tranche 11: interrupt source sweeps, RETI mask behavior, CB rotate matrix ─

void testInterruptVectorAllSourcesSweep() {
    struct IrqCase {
        GB::u8 bit;
        GB::u16 vector;
        const char* name;
    };

    const std::array<IrqCase, 5> cases = {{
        {static_cast<GB::u8>(GB::Interrupt::VBlank),  0x0040, "VBlank"},
        {static_cast<GB::u8>(GB::Interrupt::LCDStat), 0x0048, "LCDStat"},
        {static_cast<GB::u8>(GB::Interrupt::Timer),   0x0050, "Timer"},
        {static_cast<GB::u8>(GB::Interrupt::Serial),  0x0058, "Serial"},
        {static_cast<GB::u8>(GB::Interrupt::Joypad),  0x0060, "Joypad"}
    }};

    for (const IrqCase& tc : cases) {
        FlatMemory bus;
        GB::CPU cpu(bus);
        cpu.init();

        // Enter with RETI so IME is guaranteed true before interrupt dispatch.
        bus.write(0x0100, 0xD9); // RETI
        cpu.registers().SP = 0xC800;
        bus.write(0xC800, 0x10); // return lo -> 0x0210
        bus.write(0xC801, 0x02); // return hi

        bus.write(GB::IE_REGISTER, tc.bit);
        bus.write(GB::REG_IF, tc.bit);

        cpu.step(); // RETI
        const GB::u16 spBeforeIrq = cpu.getSP();
        const GB::u32 irqCycles = cpu.step();

        expect(irqCycles == 20, std::string("Interrupt service cycles wrong for ") + tc.name);
        expect(cpu.getPC() == tc.vector, std::string("Vector mismatch for ") + tc.name);
        expect(cpu.getSP() == static_cast<GB::u16>(spBeforeIrq - 2), std::string("SP push mismatch for ") + tc.name);
        expect(bus.readWord(cpu.getSP()) == 0x0210, std::string("Return address mismatch for ") + tc.name);

        const GB::u8 ifAfter = bus.read(GB::REG_IF);
        expect((ifAfter & tc.bit) == 0, std::string("IF bit not cleared for ") + tc.name);
    }
}

void testRetiRespectsIeMaskWithPendingIf() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // IF has Timer+Joypad pending but IE initially enables Timer only.
    // After first service, Joypad remains pending but masked; once IE enables it,
    // next step should service Joypad.
    const GB::u8 timerBit  = static_cast<GB::u8>(GB::Interrupt::Timer);
    const GB::u8 joypadBit = static_cast<GB::u8>(GB::Interrupt::Joypad);

    bus.write(0x0100, 0xD9); // RETI
    cpu.registers().SP = 0xC900;
    bus.write(0xC900, 0x10); // return lo -> 0x0210
    bus.write(0xC901, 0x02); // return hi

    bus.write(GB::IE_REGISTER, timerBit);
    bus.write(GB::REG_IF, static_cast<GB::u8>(timerBit | joypadBit));

    cpu.step(); // RETI
    const GB::u32 timerIrqCycles = cpu.step();
    expect(timerIrqCycles == 20, "Enabled Timer interrupt should be serviced first");
    expect(cpu.getPC() == 0x0050, "Timer vector should be 0x0050");

    const GB::u8 ifAfterTimer = bus.read(GB::REG_IF);
    expect((ifAfterTimer & timerBit) == 0, "Timer IF bit should clear after service");
    expect((ifAfterTimer & joypadBit) != 0, "Masked Joypad IF bit should remain pending");

    // While Joypad remains masked, no service should occur.
    bus.write(0x0050, 0x00); // NOP at timer vector for deterministic step
    const GB::u32 noIrqCycles = cpu.step();
    expect(noIrqCycles == 4, "With masked pending IF, next opcode should execute normally");
    expect(cpu.getPC() == 0x0051, "PC should advance by NOP when no enabled IRQ remains");

    // Re-enable IME via RETI and then enable Joypad in IE; pending Joypad should service.
    bus.write(0x0051, 0xD9); // RETI
    cpu.registers().SP = 0xC902;
    bus.write(0xC902, 0x20); // return lo -> 0x0220
    bus.write(0xC903, 0x02); // return hi

    cpu.step(); // RETI
    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(timerBit | joypadBit));
    const GB::u32 joypadIrqCycles = cpu.step();
    expect(joypadIrqCycles == 20, "Unmasked pending Joypad interrupt should now service");
    expect(cpu.getPC() == 0x0060, "Joypad vector should be 0x0060");
}

void testHaltWakeAllSourcesWithoutImeSweep() {
    struct IrqCase {
        GB::u8 bit;
        const char* name;
    };

    const std::array<IrqCase, 5> cases = {{
        {static_cast<GB::u8>(GB::Interrupt::VBlank),  "VBlank"},
        {static_cast<GB::u8>(GB::Interrupt::LCDStat), "LCDStat"},
        {static_cast<GB::u8>(GB::Interrupt::Timer),   "Timer"},
        {static_cast<GB::u8>(GB::Interrupt::Serial),  "Serial"},
        {static_cast<GB::u8>(GB::Interrupt::Joypad),  "Joypad"}
    }};

    for (const IrqCase& tc : cases) {
        FlatMemory bus;
        GB::CPU cpu(bus);
        cpu.init();

        // IME remains false: HALT should wake on pending IRQ and execute next opcode.
        bus.write(0x0100, 0x76);                           // HALT
        bus.write(0x0101, 0x3E); bus.write(0x0102, 0x4D); // LD A,0x4D

        cpu.step(); // HALT entry
        const GB::u32 spinCycles = cpu.step();
        expect(spinCycles == 4, std::string("HALT spin cycles mismatch for ") + tc.name);

        bus.write(GB::IE_REGISTER, tc.bit);
        bus.write(GB::REG_IF, tc.bit);

        const GB::u32 wakeExecCycles = cpu.step();
        expect(wakeExecCycles == 8, std::string("Wake execution cycles mismatch for ") + tc.name);
        expect(cpu.registers().A == 0x4D, std::string("Wake execution value mismatch for ") + tc.name);
        expect(cpu.getPC() == 0x0103, std::string("Wake execution PC mismatch for ") + tc.name);

        // Must not vector when IME is false; IF bit remains pending.
        expect(cpu.getPC() != 0x0040 && cpu.getPC() != 0x0048 && cpu.getPC() != 0x0050 &&
               cpu.getPC() != 0x0058 && cpu.getPC() != 0x0060,
               std::string("Unexpected vectoring with IME off for ") + tc.name);
        const GB::u8 ifAfter = bus.read(GB::REG_IF);
        expect((ifAfter & tc.bit) != 0, std::string("IF should remain set with IME off for ") + tc.name);
    }
}

void testCbRotateCycleMatrix() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // RL/RR/RLC/RRC register forms should be 12 cycles (prefix+op),
    // (HL) forms should be 20 cycles.
    bus.write(0x0100, 0x26); bus.write(0x0101, 0xCA); // LD H,0xCA
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x00); // LD L,0x00
    bus.write(0x0104, 0x06); bus.write(0x0105, 0x80); // LD B,0x80
    bus.write(0x0106, 0x36); bus.write(0x0107, 0x80); // LD (HL),0x80

    bus.write(0x0108, 0xCB); bus.write(0x0109, 0x00); // RLC B
    bus.write(0x010A, 0xCB); bus.write(0x010B, 0x06); // RLC (HL)
    bus.write(0x010C, 0xCB); bus.write(0x010D, 0x10); // RL B
    bus.write(0x010E, 0xCB); bus.write(0x010F, 0x16); // RL (HL)
    bus.write(0x0110, 0xCB); bus.write(0x0111, 0x18); // RR B
    bus.write(0x0112, 0xCB); bus.write(0x0113, 0x1E); // RR (HL)
    bus.write(0x0114, 0xCB); bus.write(0x0115, 0x08); // RRC B
    bus.write(0x0116, 0xCB); bus.write(0x0117, 0x0E); // RRC (HL)

    cpu.step();
    cpu.step();
    cpu.step();
    cpu.step();

    expect(cpu.step() == 12, "CB RLC B should consume 12 cycles");
    expect(cpu.step() == 20, "CB RLC (HL) should consume 20 cycles");
    expect(cpu.step() == 12, "CB RL B should consume 12 cycles");
    expect(cpu.step() == 20, "CB RL (HL) should consume 20 cycles");
    expect(cpu.step() == 12, "CB RR B should consume 12 cycles");
    expect(cpu.step() == 20, "CB RR (HL) should consume 20 cycles");
    expect(cpu.step() == 12, "CB RRC B should consume 12 cycles");
    expect(cpu.step() == 20, "CB RRC (HL) should consume 20 cycles");
}

// ── Tranche 12: EI timing boundaries, return-address lengths, CB shifts ─────

void testEiDelayedEnableAcrossTakenJump() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // EI defers IME until the *following* instruction start.
    // With IF+IE pending from the beginning:
    // 1) EI executes (no immediate IRQ)
    // 2) JP executes normally
    // 3) IRQ is serviced and pushes JP target PC.
    bus.write(0x0100, 0xFB);                           // EI
    bus.write(0x0101, 0xC3); bus.write(0x0102, 0x00); // JP 0x0200
    bus.write(0x0103, 0x02);
    bus.write(0x0200, 0x00);                           // NOP (should be preempted)

    const GB::u8 vb = static_cast<GB::u8>(GB::Interrupt::VBlank);
    bus.write(GB::IE_REGISTER, vb);
    bus.write(GB::REG_IF, vb);

    const GB::u16 spBefore = cpu.getSP();

    expect(cpu.step() == 4, "EI should consume 4 cycles");
    expect(cpu.getPC() == 0x0101, "PC should advance to instruction after EI");

    expect(cpu.step() == 16, "JP a16 should execute before deferred EI can service IRQ");
    expect(cpu.getPC() == 0x0200, "JP should land at 0x0200 before IRQ service");

    expect(cpu.step() == 20, "Pending interrupt should service after the following instruction");
    expect(cpu.getPC() == 0x0040, "VBlank interrupt should vector to 0x0040");
    expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), "IRQ service should push return PC");
    expect(bus.readWord(cpu.getSP()) == 0x0200, "Return address should be JP target PC 0x0200");
}

void testInterruptReturnAddressByOpcodeLength() {
    struct LenCase {
        std::string name;
        std::vector<GB::u8> program;
        GB::u16 expectedReturnPc;
    };

    const std::vector<LenCase> cases = {
        {"1-byte NOP", {0xFB, 0x00}, 0x0102},
        {"2-byte LD A,d8", {0xFB, 0x3E, 0x77}, 0x0103},
        {"3-byte JP a16", {0xFB, 0xC3, 0x00, 0x02}, 0x0200}
    };

    for (const LenCase& tc : cases) {
        FlatMemory bus;
        GB::CPU cpu(bus);
        cpu.init();

        for (size_t i = 0; i < tc.program.size(); ++i) {
            bus.write(static_cast<GB::u16>(0x0100 + i), tc.program[i]);
        }
        bus.write(0x0200, 0x00);

        const GB::u8 vb = static_cast<GB::u8>(GB::Interrupt::VBlank);
        bus.write(GB::IE_REGISTER, vb);
        bus.write(GB::REG_IF, vb);

        const GB::u16 spBefore = cpu.getSP();

        cpu.step(); // EI
        cpu.step(); // instruction under test executes before IRQ service
        const GB::u32 irqCycles = cpu.step();
        expect(irqCycles == 20, std::string("IRQ service cycles mismatch for ") + tc.name);
        expect(cpu.getPC() == 0x0040, std::string("IRQ vector mismatch for ") + tc.name);
        expect(cpu.getSP() == static_cast<GB::u16>(spBefore - 2), std::string("SP push mismatch for ") + tc.name);
        expect(bus.readWord(cpu.getSP()) == tc.expectedReturnPc,
               std::string("Return address mismatch for ") + tc.name);
    }
}

void testCbShiftFamilyFlagAndCycleMatrix() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Cover SLA/SRA/SRL/SWAP on register and (HL) with explicit flag checks.
    bus.write(0x0100, 0x26); bus.write(0x0101, 0xCB); // LD H,0xCB
    bus.write(0x0102, 0x2E); bus.write(0x0103, 0x00); // LD L,0x00

    bus.write(0x0104, 0x06); bus.write(0x0105, 0x81); // LD B,0x81
    bus.write(0x0106, 0xCB); bus.write(0x0107, 0x20); // SLA B
    bus.write(0x0108, 0x36); bus.write(0x0109, 0x81); // LD (HL),0x81
    bus.write(0x010A, 0xCB); bus.write(0x010B, 0x26); // SLA (HL)

    bus.write(0x010C, 0x06); bus.write(0x010D, 0x81); // LD B,0x81
    bus.write(0x010E, 0xCB); bus.write(0x010F, 0x28); // SRA B
    bus.write(0x0110, 0x36); bus.write(0x0111, 0x81); // LD (HL),0x81
    bus.write(0x0112, 0xCB); bus.write(0x0113, 0x2E); // SRA (HL)

    bus.write(0x0114, 0x06); bus.write(0x0115, 0x01); // LD B,0x01
    bus.write(0x0116, 0xCB); bus.write(0x0117, 0x38); // SRL B
    bus.write(0x0118, 0x36); bus.write(0x0119, 0x01); // LD (HL),0x01
    bus.write(0x011A, 0xCB); bus.write(0x011B, 0x3E); // SRL (HL)

    bus.write(0x011C, 0x06); bus.write(0x011D, 0xF0); // LD B,0xF0
    bus.write(0x011E, 0xCB); bus.write(0x011F, 0x30); // SWAP B
    bus.write(0x0120, 0x36); bus.write(0x0121, 0xF0); // LD (HL),0xF0
    bus.write(0x0122, 0xCB); bus.write(0x0123, 0x36); // SWAP (HL)

    cpu.step();
    cpu.step();

    cpu.step(); // LD B,0x81
    expect(cpu.step() == 12, "CB SLA B should consume 12 cycles");
    expect(cpu.registers().B == 0x02, "SLA B 0x81 -> 0x02");
    expect(cpu.registers().flagC(), "SLA B should set C from old bit7");
    expect(!cpu.registers().flagZ(), "SLA B result 0x02 should clear Z");

    cpu.step(); // LD (HL),0x81
    expect(cpu.step() == 20, "CB SLA (HL) should consume 20 cycles");
    expect(bus.read(0xCB00) == 0x02, "SLA (HL) 0x81 -> 0x02");
    expect(cpu.registers().flagC(), "SLA (HL) should set C from old bit7");

    cpu.step(); // LD B,0x81
    expect(cpu.step() == 12, "CB SRA B should consume 12 cycles");
    expect(cpu.registers().B == 0xC0, "SRA B 0x81 -> 0xC0");
    expect(cpu.registers().flagC(), "SRA B should set C from old bit0");

    cpu.step(); // LD (HL),0x81
    expect(cpu.step() == 20, "CB SRA (HL) should consume 20 cycles");
    expect(bus.read(0xCB00) == 0xC0, "SRA (HL) 0x81 -> 0xC0");
    expect(cpu.registers().flagC(), "SRA (HL) should set C from old bit0");

    cpu.step(); // LD B,0x01
    expect(cpu.step() == 12, "CB SRL B should consume 12 cycles");
    expect(cpu.registers().B == 0x00, "SRL B 0x01 -> 0x00");
    expect(cpu.registers().flagC(), "SRL B should set C from old bit0");
    expect(cpu.registers().flagZ(), "SRL B zero result should set Z");

    cpu.step(); // LD (HL),0x01
    expect(cpu.step() == 20, "CB SRL (HL) should consume 20 cycles");
    expect(bus.read(0xCB00) == 0x00, "SRL (HL) 0x01 -> 0x00");
    expect(cpu.registers().flagC(), "SRL (HL) should set C from old bit0");
    expect(cpu.registers().flagZ(), "SRL (HL) zero result should set Z");

    cpu.step(); // LD B,0xF0
    expect(cpu.step() == 12, "CB SWAP B should consume 12 cycles");
    expect(cpu.registers().B == 0x0F, "SWAP B 0xF0 -> 0x0F");
    expect(!cpu.registers().flagC(), "SWAP should clear C");
    expect(!cpu.registers().flagZ(), "SWAP B result 0x0F should clear Z");

    cpu.step(); // LD (HL),0xF0
    expect(cpu.step() == 20, "CB SWAP (HL) should consume 20 cycles");
    expect(bus.read(0xCB00) == 0x0F, "SWAP (HL) 0xF0 -> 0x0F");
    expect(!cpu.registers().flagC(), "SWAP (HL) should clear C");
}

// ── Tranche 13: preemption order, mixed-length return integrity, ADC/SBC sweep ─

void testInterruptPreemptionWithDynamicIeMasks() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Start with IF containing multiple pending bits.
    // Enable only VBlank+Timer first, then change IE mid-flow to Timer-only,
    // then Joypad-only to verify dynamic masking and priority behavior.
    const GB::u8 vb  = static_cast<GB::u8>(GB::Interrupt::VBlank);
    const GB::u8 lcd = static_cast<GB::u8>(GB::Interrupt::LCDStat);
    const GB::u8 tim = static_cast<GB::u8>(GB::Interrupt::Timer);
    const GB::u8 joy = static_cast<GB::u8>(GB::Interrupt::Joypad);

    bus.write(0x0100, 0xD9); // RETI bootstrap -> IME on
    bus.write(0x0040, 0xD9); // RETI in VBlank vector
    bus.write(0x0050, 0xD9); // RETI in Timer vector
    bus.write(0x0060, 0xD9); // RETI in Joypad vector

    cpu.registers().SP = 0xCC00;
    bus.write(0xCC00, 0x10); // return lo -> 0x0210
    bus.write(0xCC01, 0x02); // return hi

    bus.write(GB::REG_IF, static_cast<GB::u8>(vb | lcd | tim | joy));
    bus.write(GB::IE_REGISTER, static_cast<GB::u8>(vb | tim));

    cpu.step(); // RETI to 0x0210

    // Phase 1: with IE={VBlank,Timer}, VBlank (highest) should service first.
    expect(cpu.step() == 20, "First serviced IRQ should consume 20 cycles");
    expect(cpu.getPC() == 0x0040, "VBlank should preempt first under IE={VBlank,Timer}");
    cpu.step(); // RETI from 0x0040

    // Phase 2: restrict IE to Timer only; Timer should service next (LCD still masked).
    bus.write(GB::IE_REGISTER, tim);
    expect(cpu.step() == 20, "Second serviced IRQ should consume 20 cycles");
    expect(cpu.getPC() == 0x0050, "Timer should service when IE mask allows only Timer");
    cpu.step(); // RETI from 0x0050

    // Phase 3: switch IE to Joypad only; Joypad should service, LCD remains pending/masked.
    bus.write(GB::IE_REGISTER, joy);
    expect(cpu.step() == 20, "Third serviced IRQ should consume 20 cycles");
    expect(cpu.getPC() == 0x0060, "Joypad should service after dynamic IE switch");

    const GB::u8 ifAfter = bus.read(GB::REG_IF);
    expect((ifAfter & vb) == 0, "VBlank IF bit should be cleared");
    expect((ifAfter & tim) == 0, "Timer IF bit should be cleared");
    expect((ifAfter & joy) == 0, "Joypad IF bit should be cleared");
    expect((ifAfter & lcd) != 0, "LCD IF bit should remain pending while masked");
}

void testMixedLengthBeforeRstAndConditionalReturnIntegrity() {
    FlatMemory bus;
    GB::CPU cpu(bus);
    cpu.init();

    // Mixed-length stream before RST and conditional return:
    // 0100: LD A,0x12      (2 bytes)
    // 0102: LD BC,0x3456   (3 bytes)
    // 0105: RST 20         (1 byte) -> push 0x0106
    // 0020: RET            (1 byte)
    // 0106: XOR A          (1 byte, sets Z)
    // 0107: CALL Z,0x0300  (3 bytes) -> push 0x010A
    // 0300: RET Z          (1 byte)
    bus.write(0x0100, 0x3E); bus.write(0x0101, 0x12);
    bus.write(0x0102, 0x01); bus.write(0x0103, 0x56); bus.write(0x0104, 0x34);
    bus.write(0x0105, 0xE7); // RST 20
    bus.write(0x0020, 0xC9); // RET
    bus.write(0x0106, 0xAF); // XOR A
    bus.write(0x0107, 0xCC); bus.write(0x0108, 0x00); bus.write(0x0109, 0x03); // CALL Z,0x0300
    bus.write(0x0300, 0xC8); // RET Z

    const GB::u16 spBefore = cpu.getSP();

    cpu.step(); // LD A,d8
    cpu.step(); // LD BC,d16

    expect(cpu.step() == 16, "RST 20 should consume 16 cycles");
    expect(cpu.getPC() == 0x0020, "RST 20 should jump to 0x0020");
    expect(bus.readWord(cpu.getSP()) == 0x0106, "RST 20 should push mixed-length next PC 0x0106");

    expect(cpu.step() == 16, "RET should consume 16 cycles");
    expect(cpu.getPC() == 0x0106, "RET from vector should return to 0x0106");
    expect(cpu.getSP() == spBefore, "SP should restore after RST/RET round-trip");

    cpu.step(); // XOR A => Z=1
    expect(cpu.registers().flagZ(), "XOR A should set Z for conditional CALL/RET");

    expect(cpu.step() == 24, "CALL Z taken should consume 24 cycles");
    expect(cpu.getPC() == 0x0300, "CALL Z should jump to 0x0300");
    expect(bus.readWord(cpu.getSP()) == 0x010A, "CALL Z should push return PC 0x010A");

    expect(cpu.step() == 20, "RET Z taken should consume 20 cycles");
    expect(cpu.getPC() == 0x010A, "RET Z should return to 0x010A");
    expect(cpu.getSP() == spBefore, "SP should restore after CALL Z/RET Z round-trip");
}

void testAdcSbcFlagEdgeSweepTableDriven() {
    struct Case {
        bool isAdc;
        GB::u8 a;
        GB::u8 operand;
        bool carryIn;
        GB::u8 expectedA;
        bool z;
        bool n;
        bool h;
        bool c;
        const char* name;
    };

    const std::vector<Case> cases = {
        // ADC nibble boundary
        {true,  0x0F, 0x00, true,  0x10, false, false, true,  false, "adc_nibble_boundary"},
        // ADC byte boundary
        {true,  0xFF, 0x00, true,  0x00, true,  false, true,  true,  "adc_byte_boundary"},
        // ADC no byte carry but high result
        {true,  0x7F, 0x80, false, 0xFF, false, false, false, false, "adc_high_result_no_carry"},

        // SBC nibble borrow boundary
        {false, 0x10, 0x00, true,  0x0F, false, true,  true,  false, "sbc_nibble_borrow_boundary"},
        // SBC byte borrow boundary
        {false, 0x00, 0x00, true,  0xFF, false, true,  true,  true,  "sbc_byte_borrow_boundary"},
        // SBC exact zero with carry-in contribution
        {false, 0x80, 0x7F, true,  0x00, true,  true,  true,  false, "sbc_zero_with_carryin"}
    };

    for (const Case& tc : cases) {
        FlatMemory bus;
        GB::CPU cpu(bus);
        cpu.init();

        bus.write(0x0100, 0x3E); bus.write(0x0101, tc.a); // LD A,a
        if (tc.isAdc) {
            bus.write(0x0102, 0xCE); bus.write(0x0103, tc.operand); // ADC d8
        } else {
            bus.write(0x0102, 0xDE); bus.write(0x0103, tc.operand); // SBC d8
        }

        cpu.registers().setC(tc.carryIn);

        cpu.step(); // LD A
        cpu.step(); // ADC/SBC

        expect(cpu.registers().A == tc.expectedA, std::string("A mismatch in ") + tc.name);
        expect(cpu.registers().flagZ() == tc.z, std::string("Z mismatch in ") + tc.name);
        expect(cpu.registers().flagN() == tc.n, std::string("N mismatch in ") + tc.name);
        expect(cpu.registers().flagH() == tc.h, std::string("H mismatch in ") + tc.name);
        expect(cpu.registers().flagC() == tc.c, std::string("C mismatch in ") + tc.name);
    }
}

void writeCsvLog(const std::vector<TestResult>& results) {
    const fs::path logPath = fs::path("tests") / "logs" / "cpu_micro_results.csv";
    fs::create_directories(logPath.parent_path());

    std::ofstream out(logPath, std::ios::trunc);
    out << "priority,test,passed,detail\n";
    for (const TestResult& r : results) {
        out << r.priority << ','
            << '"' << r.name << '"' << ','
            << (r.passed ? "PASS" : "FAIL") << ','
            << '"' << r.detail << '"'
            << "\n";
    }
}

} // namespace

int main() {
    const std::vector<TestCase> cases = {
        {"critical", "ld_add_immediate", testLoadAndAddImmediate},
        {"critical", "interrupt_service_flow", testInterruptServiceFlow},
        {"high", "xor_a_sets_zero", testXorASetsZero},
        {"high", "cb_rlc_b", testCbRlcB},
        {"high", "add_half_carry_flag", testAddHalfCarryFlag},
        {"high", "add_carry_flag", testAddCarryFlag},
        {"medium", "inc_dec_b_flags", testIncDecBFlags},
        {"medium", "jump_absolute", testJumpAbsolute},
        // Tranche 2: control-flow and stack integrity
        {"critical", "call_and_ret", testCallAndRet},
        {"critical", "push_pop_bc", testPushPopBc},
        {"critical", "reti_re_enables_ime", testRetiReEnablesIme},
        {"high", "conditional_jr_taken", testConditionalJrTaken},
        {"high", "conditional_jr_not_taken", testConditionalJrNotTaken},
        {"high", "call_z_and_ret_z", testCallZAndRetZ},
        // Tranche 3: ALU flag accuracy, HL-indirect memory, CB BIT, 16-bit ADD
        {"critical", "sub_borrow_flags", testSubBorrowFlags},
        {"critical", "sbc_carry_propagation", testSbcCarryPropagation},
        {"high", "and_mask_flags", testAndMaskFlags},
        {"high", "or_flags", testOrFlags},
        {"high", "cp_compare_flags", testCpCompareFlags},
        {"high", "ldr_hl_indirect", testLdHlIndirect},
        {"medium", "cb_bit_test", testCbBitTest},
        {"medium", "add_hl_bc", testAddHlBc},
        // Tranche 4: DAA, rotate/shift family, CB SET/RES, SP/HL transfer
        {"critical", "daa_after_add", testDaaAfterAdd},
        {"critical", "daa_after_sub", testDaaAfterSub},
        {"high", "cb_set_res_b", testCbSetResB},
        {"high", "cb_swap_sra_srl", testCbSwapSraSrl},
        {"high", "rla_rra_carry_flow", testRlaRraCarryFlow},
        {"medium", "ld_sp_hl", testLdSpHl},
        {"medium", "ld_hl_sp_plus_r8", testLdHlSpPlusR8},
        // Tranche 5: ADC matrix, conditional not-taken timing, CB (HL) path
        {"critical", "adc_consumes_carry_in", testAdcConsumesCarryIn},
        {"critical", "adc_sets_halfcarry_and_carry", testAdcSetsHalfCarryAndCarry},
        {"high", "conditional_not_taken_cycles", testConditionalNotTakenCycles},
        {"high", "cb_ops_on_hl_indirect_path", testCbOpsOnHlIndirectPath},
        // Tranche 6: ADC/SBC register+(HL), POP AF masking, CPL/SCF/CCF
        {"critical", "adc_register_variant", testAdcRegisterVariant},
        {"critical", "sbc_register_variant", testSbcRegisterVariant},
        {"high", "adc_sbc_hl_indirect_variants", testAdcSbcHlIndirectVariants},
        {"high", "pop_af_masks_low_nibble", testPopAfMasksLowNibble},
        {"medium", "cpl_scf_ccf_flag_semantics", testCplScfCcfFlagSemantics},
        // Tranche 7: INC/DEC (HL), ADD SP,r8 signed, LDI/LDD pointer wrap
        {"critical", "add_sp_r8_signed_matrix", testAddSpR8SignedMatrix},
        {"high", "inc_hl_indirect_flags", testIncHlIndirectFlags},
        {"high", "dec_hl_indirect_flags", testDecHlIndirectFlags},
        {"high", "ldi_pointer_wrap_behavior", testLdiPointerWrapBehavior},
        {"high", "ldd_pointer_wrap_behavior", testLddPointerWrapBehavior},
        // Tranche 8: taken-path timing, RST vectors, HALT/EI interactions
        {"critical", "taken_control_flow_cycles_and_stack", testTakenControlFlowCyclesAndStack},
        {"critical", "rst_vectors_and_stack", testRstVectorsAndStack},
        {"high", "ei_then_halt_services_pending_interrupt", testEiThenHaltServicesPendingInterrupt},
        {"high", "halt_wake_without_ime_executes_next_opcode", testHaltWakeWithoutImeExecutesNextOpcode},
        // Tranche 9: RETI multi-source, C/NC taken paths, CB cycle matrix
        {"critical", "reti_multi_source_priority", testRetiMultiSourcePriority},
        {"critical", "carry_conditional_taken_matrix", testCarryConditionalTakenMatrix},
        {"critical", "nocarry_conditional_taken_matrix", testNoCarryConditionalTakenMatrix},
        {"high", "cb_bit_res_set_cycle_matrix", testCbBitResSetCycleMatrix},
        // Tranche 10: RETI chaining, JP HL/JP a16, extra RST, HALT timer wake
        {"critical", "reti_chains_pending_interrupts", testRetiChainsPendingInterrupts},
        {"high", "jp_absolute_and_jp_hl_matrix", testJpAbsoluteAndJpHlMatrix},
        {"high", "additional_rst_vectors", testAdditionalRstVectors},
        {"high", "halt_wake_on_timer_without_ime", testHaltWakeOnTimerWithoutIme},
        // Tranche 11: interrupt sweeps, RETI mask behavior, CB rotate matrix
        {"critical", "interrupt_vector_all_sources_sweep", testInterruptVectorAllSourcesSweep},
        {"critical", "reti_respects_ie_mask_with_pending_if", testRetiRespectsIeMaskWithPendingIf},
        {"high", "halt_wake_all_sources_without_ime_sweep", testHaltWakeAllSourcesWithoutImeSweep},
        {"high", "cb_rotate_cycle_matrix", testCbRotateCycleMatrix},
        // Tranche 12: EI boundaries, return-address lengths, CB shift matrix
        {"critical", "ei_delayed_enable_across_taken_jump", testEiDelayedEnableAcrossTakenJump},
        {"critical", "interrupt_return_address_by_opcode_length", testInterruptReturnAddressByOpcodeLength},
        {"high", "cb_shift_family_flag_and_cycle_matrix", testCbShiftFamilyFlagAndCycleMatrix},
        // Tranche 13: preemption order, mixed-length return integrity, ADC/SBC edges
        {"critical", "interrupt_preemption_with_dynamic_ie_masks", testInterruptPreemptionWithDynamicIeMasks},
        {"critical", "mixed_length_before_rst_and_conditional_return_integrity", testMixedLengthBeforeRstAndConditionalReturnIntegrity},
        {"high", "adc_sbc_flag_edge_sweep_table_driven", testAdcSbcFlagEdgeSweepTableDriven}
    };

    std::vector<TestResult> results;
    results.reserve(cases.size());

    bool allPassed = true;
    for (const TestCase& tc : cases) {
        TestResult r;
        r.priority = tc.priority;
        r.name = tc.name;
        try {
            tc.run();
            r.passed = true;
            r.detail = "ok";
        } catch (const std::exception& ex) {
            r.passed = false;
            r.detail = ex.what();
            allPassed = false;
        }

        std::cout << "[cpu-micro-tests] [" << r.priority << "] " << r.name << " => "
                  << (r.passed ? "PASS" : "FAIL")
                  << " (" << r.detail << ")\n";
        results.push_back(r);
    }

    writeCsvLog(results);
    std::cout << "[cpu-micro-tests] Wrote log: tests/logs/cpu_micro_results.csv\n";
    return allPassed ? 0 : 1;
}
