from std import sys
from std.collections import BitSet, InlineArray

from mist.termios.c import (
    FileDescriptorBitSet,
    LocalFlag,
    SpecialCharacter,
    Termios,
    _TimeValue,
    _select,
    tcgetattr,
    tcsetattr,
)

comptime STDIN_FILENO = 0
comptime TCSANOW = 0
comptime TCSADRAIN = 1

struct Terminal:
    var original: Termios
    var buffering_disabled: Bool

    def __init__(out self):
        self.original = Termios()
        self.buffering_disabled = False

    def disable_input_buffering(mut self) raises:
        if self.buffering_disabled:
            return

        var current = Termios()
        if tcgetattr(STDIN_FILENO, Pointer(to=current)) != 0:
            raise Error("failed to get terminal attributes")

        self.original = current.copy()
        current.c_lflag &= ~(LocalFlag.ICANON.value | LocalFlag.ECHO.value)
        current.c_cc[SpecialCharacter.VMIN.value] = 1
        current.c_cc[SpecialCharacter.VTIME.value] = 0

        if tcsetattr(STDIN_FILENO, TCSANOW, Pointer(to=current)) != 0:
            raise Error("failed to set terminal attributes")

        self.buffering_disabled = True

    def restore_input_buffering(mut self) raises:
        if not self.buffering_disabled:
            return

        if tcsetattr(STDIN_FILENO, TCSADRAIN, Pointer(to=self.original)) != 0:
            raise Error("failed to restore terminal attributes")

        self.buffering_disabled = False

    def check_key(self) -> Bool:
        var read_fds = FileDescriptorBitSet()
        var write_fds = BitSet[1]()
        var except_fds = BitSet[1]()
        var timeout = _TimeValue(0, 0)

        read_fds.set(STDIN_FILENO)

        return _select(
            STDIN_FILENO + 1,
            Pointer(to=read_fds),
            Pointer(to=write_fds),
            Pointer(to=except_fds),
            Pointer(to=timeout),
        ) != 0

    def getch(self) raises -> UInt16:
        var stdin = sys.stdin
        var buf = InlineArray[UInt8, 1](uninitialized=True)
        var bytes_read = stdin.read_bytes(buf)

        if bytes_read != 1:
            raise Error("failed to read character from terminal")

        return UInt16(buf[0])

    def put_code(self, code: Int):
        var out = sys.stdout
        out.write(chr(code & 0xFF))

    def write(self, text: String):
        var out = sys.stdout
        out.write(text)
