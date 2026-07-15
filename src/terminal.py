import sys
import termios
import select
import atexit
import os
import signal


_old_settings = None
_old_sigint_handler = None


def disable_input_buffering():
    global _old_settings, _old_sigint_handler
    if _old_settings is None:
        fd = sys.stdin.fileno()
        _old_settings = termios.tcgetattr(fd)
        _old_sigint_handler = signal.getsignal(signal.SIGINT)

        new_settings = termios.tcgetattr(fd)
        new_settings[3] = new_settings[3] & ~termios.ICANON & ~termios.ECHO
        termios.tcsetattr(fd, termios.TCSANOW, new_settings)
        signal.signal(signal.SIGINT, handle_interrupt)

        atexit.register(restore_input_buffering)


def restore_input_buffering():
    global _old_settings, _old_sigint_handler
    if _old_settings is not None:
        termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, _old_settings)
        _old_settings = None

    if _old_sigint_handler is not None:
        signal.signal(signal.SIGINT, _old_sigint_handler)
        _old_sigint_handler = None


def handle_interrupt(signum, frame):
    restore_input_buffering()
    print()
    sys.stdout.flush()
    os._exit(254)


def getch():
    ch = sys.stdin.read(1)

    if ord(ch) == 3:
        restore_input_buffering()
        os._exit(130)

    return ch


def check_key():
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    return len(ready) != 0


def put_code(code):
    sys.stdout.write(chr(code & 0xFF))
    sys.stdout.flush()


def write(text):
    sys.stdout.write(text)
    sys.stdout.flush()
