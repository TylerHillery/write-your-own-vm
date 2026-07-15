from std.collections import List
from std.sys import argv

from terminal import Terminal


struct Registers:
    comptime R_R0: Int = 0
    comptime R_R1: Int = 1
    comptime R_R2: Int = 2
    comptime R_R3: Int = 3
    comptime R_R4: Int = 4
    comptime R_R5: Int = 5
    comptime R_R6: Int = 6
    comptime R_R7: Int = 7
    comptime PC: Int = 8
    comptime COND: Int = 9
    comptime COUNT: Int = 10


struct Flags:
    comptime POS: UInt16 = 1 << 0
    comptime ZRO: UInt16 = 1 << 1
    comptime NEG: UInt16 = 1 << 2


struct OpCode:
    comptime OP_BR: UInt16 = 0
    comptime OP_ADD: UInt16 = 1
    comptime OP_LD: UInt16 = 2
    comptime OP_ST: UInt16 = 3
    comptime OP_JSR: UInt16 = 4
    comptime OP_AND: UInt16 = 5
    comptime OP_LDR: UInt16 = 6
    comptime OP_STR: UInt16 = 7
    comptime OP_RTI: UInt16 = 8
    comptime OP_NOT: UInt16 = 9
    comptime OP_LDI: UInt16 = 10
    comptime OP_STI: UInt16 = 11
    comptime OP_JMP: UInt16 = 12
    comptime OP_RES: UInt16 = 13
    comptime OP_LEA: UInt16 = 14
    comptime OP_TRAP: UInt16 = 15


struct KeyboardStatus:
    comptime MR_KBSR: UInt16 = 0xFE00
    comptime MR_KBDR: UInt16 = 0xFE02


struct TrapCode:
    comptime GETC: UInt16 = 0x20
    comptime OUT: UInt16 = 0x21
    comptime PUTS: UInt16 = 0x22
    comptime IN: UInt16 = 0x23
    comptime PUTSP: UInt16 = 0x24
    comptime HALT: UInt16 = 0x25


comptime MEMORY_MAX = 1 << 16


def sign_extend(x: UInt16, bit_count: Int) -> UInt16:
    var value = Int(x)

    if ((value >> (bit_count - 1)) & 1) != 0:
        value |= -1 << bit_count

    return UInt16(value & 0xFFFF)


def swap16(x: UInt16) -> UInt16:
    return (x << 8) | (x >> 8)


def update_flags(mut reg: List[UInt16], r: Int):
    if reg[r] == UInt16(0):
        reg[Registers.COND] = UInt16(Flags.ZRO)
    elif (reg[r] & UInt16(0x8000)) != UInt16(0):
        reg[Registers.COND] = UInt16(Flags.NEG)
    else:
        reg[Registers.COND] = UInt16(Flags.POS)


def mem_write(mut memory: List[UInt16], address: UInt16, val: UInt16):
    memory[Int(address)] = val


def mem_read(
    mut memory: List[UInt16], terminal: Terminal, address: UInt16
) raises -> UInt16:
    if address == KeyboardStatus.MR_KBSR:
        if terminal.check_key():
            memory[Int(KeyboardStatus.MR_KBSR)] = UInt16(1 << 15)
            memory[Int(KeyboardStatus.MR_KBDR)] = terminal.getch()
        else:
            memory[Int(KeyboardStatus.MR_KBSR)] = UInt16(0)

    return memory[Int(address)]


def read_image(mut memory: List[UInt16], image_path: String) raises -> Bool:
    var file = open(image_path, "r")
    var data = file.read_bytes()
    file.close()

    if len(data) < 2:
        return False

    var origin = (UInt16(data[0]) << 8) | UInt16(data[1])
    var address = Int(origin)

    var i = 2
    while i + 1 < len(data):
        if address >= MEMORY_MAX:
            return False

        memory[address] = (UInt16(data[i]) << 8) | UInt16(data[i + 1])
        address += 1
        i += 2

    return True


def ins[
    op: Int
](
    mut memory: List[UInt16],
    mut reg: List[UInt16],
    terminal: Terminal,
    instr: UInt16,
) raises -> Bool:
    var r0 = UInt16(0)
    var r1 = UInt16(0)
    var r2 = UInt16(0)
    var imm5 = UInt16(0)
    var imm_flag = UInt16(0)
    var pc_plus_off = UInt16(0)
    var base_plus_off = UInt16(0)

    comptime opbit = 1 << op

    comptime if (0x4EEE & opbit) != 0:
        r0 = (instr >> 9) & UInt16(0x7)

    comptime if (0x12F3 & opbit) != 0:
        r1 = (instr >> 6) & UInt16(0x7)

    comptime if (0x0022 & opbit) != 0:
        imm_flag = (instr >> 5) & UInt16(0x1)

        if imm_flag != UInt16(0):
            imm5 = sign_extend(instr & UInt16(0x1F), 5)
        else:
            r2 = instr & UInt16(0x7)

    comptime if (0x00C0 & opbit) != 0:
        base_plus_off = reg[Int(r1)] + sign_extend(instr & UInt16(0x3F), 6)

    comptime if (0x4C0D & opbit) != 0:
        pc_plus_off = reg[Registers.PC] + sign_extend(instr & UInt16(0x1FF), 9)

    comptime if (0x0001 & opbit) != 0:
        var cond = (instr >> 9) & UInt16(0x7)
        if (cond & reg[Registers.COND]) != UInt16(0):
            reg[Registers.PC] = pc_plus_off

    comptime if (0x0002 & opbit) != 0:
        if imm_flag != UInt16(0):
            reg[Int(r0)] = reg[Int(r1)] + imm5
        else:
            reg[Int(r0)] = reg[Int(r1)] + reg[Int(r2)]

    comptime if (0x0020 & opbit) != 0:
        if imm_flag != UInt16(0):
            reg[Int(r0)] = reg[Int(r1)] & imm5
        else:
            reg[Int(r0)] = reg[Int(r1)] & reg[Int(r2)]

    comptime if (0x0200 & opbit) != 0:
        reg[Int(r0)] = ~reg[Int(r1)]

    comptime if (0x1000 & opbit) != 0:
        reg[Registers.PC] = reg[Int(r1)]

    comptime if (0x0010 & opbit) != 0:
        var long_flag = (instr >> 11) & UInt16(1)
        reg[Registers.R_R7] = reg[Registers.PC]

        if long_flag != UInt16(0):
            pc_plus_off = reg[Registers.PC] + sign_extend(
                instr & UInt16(0x7FF), 11
            )
            reg[Registers.PC] = pc_plus_off
        else:
            reg[Registers.PC] = reg[Int(r1)]

    comptime if (0x0004 & opbit) != 0:
        reg[Int(r0)] = mem_read(memory, terminal, pc_plus_off)

    comptime if (0x0400 & opbit) != 0:
        var indirect_address = mem_read(memory, terminal, pc_plus_off)
        reg[Int(r0)] = mem_read(memory, terminal, indirect_address)

    comptime if (0x0040 & opbit) != 0:
        reg[Int(r0)] = mem_read(memory, terminal, base_plus_off)

    comptime if (0x4000 & opbit) != 0:
        reg[Int(r0)] = pc_plus_off

    comptime if (0x0008 & opbit) != 0:
        mem_write(memory, pc_plus_off, reg[Int(r0)])

    comptime if (0x0800 & opbit) != 0:
        var indirect_address = mem_read(memory, terminal, pc_plus_off)
        mem_write(memory, indirect_address, reg[Int(r0)])

    comptime if (0x0080 & opbit) != 0:
        mem_write(memory, base_plus_off, reg[Int(r0)])

    comptime if (0x8000 & opbit) != 0:
        reg[Registers.R_R7] = reg[Registers.PC]

        var trap = instr & UInt16(0xFF)
        if trap == TrapCode.GETC:
            reg[Registers.R_R0] = terminal.getch()
            update_flags(reg, Registers.R_R0)
        elif trap == TrapCode.OUT:
            terminal.put_code(Int(reg[Registers.R_R0]))
        elif trap == TrapCode.PUTS:
            var address = Int(reg[Registers.R_R0])
            while memory[address] != UInt16(0):
                terminal.put_code(Int(memory[address] & UInt16(0xFF)))
                address += 1
        elif trap == TrapCode.IN:
            terminal.write("Enter a character: ")
            var ch = terminal.getch()
            terminal.put_code(Int(ch))
            reg[Registers.R_R0] = ch
            update_flags(reg, Registers.R_R0)
        elif trap == TrapCode.PUTSP:
            var address = Int(reg[Registers.R_R0])
            while memory[address] != UInt16(0):
                var char1 = memory[address] & UInt16(0xFF)
                terminal.put_code(Int(char1))

                var char2 = memory[address] >> 8
                if char2 != UInt16(0):
                    terminal.put_code(Int(char2))

                address += 1
        elif trap == TrapCode.HALT:
            terminal.write("HALT\n")
            return False

    comptime if (0x4666 & opbit) != 0:
        update_flags(reg, Int(r0))

    return True


def execute_instruction(
    mut memory: List[UInt16],
    mut reg: List[UInt16],
    terminal: Terminal,
    instr: UInt16,
) raises -> Bool:
    var op = Int(instr >> 12)

    if op == 0:
        return ins[0](memory, reg, terminal, instr)
    elif op == 1:
        return ins[1](memory, reg, terminal, instr)
    elif op == 2:
        return ins[2](memory, reg, terminal, instr)
    elif op == 3:
        return ins[3](memory, reg, terminal, instr)
    elif op == 4:
        return ins[4](memory, reg, terminal, instr)
    elif op == 5:
        return ins[5](memory, reg, terminal, instr)
    elif op == 6:
        return ins[6](memory, reg, terminal, instr)
    elif op == 7:
        return ins[7](memory, reg, terminal, instr)
    elif op == 9:
        return ins[9](memory, reg, terminal, instr)
    elif op == 10:
        return ins[10](memory, reg, terminal, instr)
    elif op == 11:
        return ins[11](memory, reg, terminal, instr)
    elif op == 12:
        return ins[12](memory, reg, terminal, instr)
    elif op == 14:
        return ins[14](memory, reg, terminal, instr)
    elif op == 15:
        return ins[15](memory, reg, terminal, instr)

    return False


def main() raises:
    var terminal = Terminal()
    var memory = List[UInt16](length=MEMORY_MAX, fill=UInt16(0))
    var reg = List[UInt16](length=Registers.COUNT, fill=UInt16(0))
    var args = argv()

    if len(args) < 2:
        print("lc3 [image-file1] ...")
        return

    var j = 1
    while j < len(args):
        var image_path = String(args[j])
        if not read_image(memory, image_path):
            print(String("failed to load image: ", image_path))
            return
        j += 1

    reg[Registers.COND] = Flags.ZRO
    reg[Registers.PC] = UInt16(0x3000)

    terminal.disable_input_buffering()

    try:
        var running = True
        while running:
            var instr = mem_read(memory, terminal, reg[Registers.PC])
            reg[Registers.PC] += UInt16(1)
            running = execute_instruction(memory, reg, terminal, instr)
    finally:
        terminal.restore_input_buffering()
