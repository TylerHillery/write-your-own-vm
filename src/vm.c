#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * LC-3 is an educational architecture that has 65,536 memory locations
 */
#define MEMORY_MAX (1 << 16)

uint16_t
    memory[MEMORY_MAX]; /* 65,536 memory locations, each one 16 bits wide */

/*
 * LC-3 has 10 total registers, each are which are 16 bits (2 bytes).
 *  - R0-R7 are general purpose registers
 *  - R_PC is the program counter
 *  - R_COND is the condition flags register
 */

enum {
  R_R0 = 0,
  R_R1,
  R_R2,
  R_R3,
  R_R4,
  R_R5,
  R_R6,
  R_R7,
  R_PC,
  R_COND,
  R_COUNT
};

uint16_t reg[R_COUNT];

/*
 * LC-3 instructions are 16 bits long, with left 4 bits storing the opcode and
 * the rest are used to store parameters. There are 16 opcodes.
 * ┌──────────┬───────────────────┐
 * │  4 bits  │     16 bits       │
 * ├──────────┼───────────────────┤
 * │  opcode  │    parameters     │
 * └──────────┴───────────────────┘
 */

enum {
  OP_BR = 0, /* branch                 */
  OP_ADD,    /* add                    */
  OP_LD,     /* load                   */
  OP_ST,     /* store                  */
  OP_JSR,    /* jump register          */
  OP_AND,    /* bitwise and            */
  OP_LDR,    /* load register          */
  OP_STR,    /* store register         */
  OP_RTI,    /* unused                 */
  OP_NOT,    /* bitwise not            */
  OP_LDI,    /* load indirect          */
  OP_STI,    /* store indirect         */
  OP_JMP,    /* jump                   */
  OP_RES,    /* reserved (unused)      */
  OP_LEA,    /* load effective address */
  OP_TRAP,   /* execute trap           */
};

/*
 * `R_COND` register stores condition flags which provide information about the
 * most recently exceuted calculation. LC-3 uses 3 flags.
 */

enum {
  FL_POS = 1 << 0, /* P */
  FL_ZRO = 1 << 1, /* Z */
  FL_NEG = 1 << 2, /* N */
};

/*
 * LC-3 provides predefined routines for performing tasks and interacting with
 * I/O devices. These are called trap routines. To execute one, the TRAP instr
 * is called with the trap code of the desired routine.
 *
 * When a trap code is called, the PC is moved to that code's address, executes
 * and PC is reset to the location of the initial call. This is why programs
 * start at address 0x3000 because the lower addresses are for the trap routine
 * code
 */
enum {
  TRAP_GETC = 0x20,  /* get char from keyboard, not echoed onto the terminal */
  TRAP_OUT = 0x21,   /* output a character */
  TRAP_PUTS = 0x22,  /* output a word string */
  TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
  TRAP_PUTSP = 0x24, /* output a byte string */
  TRAP_HALT = 0x25   /* halt the program */
};

/*
 * input buffering
 */
struct termios original_tio;

void disable_input_buffering(void) {
  tcgetattr(STDIN_FILENO, &original_tio);
  struct termios new_tio = original_tio;
  new_tio.c_lflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering(void) {
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key(void) {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void handle_interrupt(int signal) {
  (void)signal;
  restore_input_buffering();
  printf("\n");
  exit(-2);
}

enum {
  MR_KBSR = 0xFE00, /* keyboard status */
  MR_KBDR = 0xFE02  /* keyboard data */
};

void mem_write(uint16_t address, uint16_t val) { memory[address] = val; }

uint16_t mem_read(uint16_t address) {
  if (address == MR_KBSR) {
    if (check_key()) {
      memory[MR_KBSR] = (1 << 15);
      memory[MR_KBDR] = getchar();
    } else {
      memory[MR_KBSR] = 0;
    }
  }
  return memory[address];
}

/*
 * sign-extending is needed when using immediate values in instructions.
 * Immediate values are 5 bits whereas values in registers are 2^16. We need to
 * extend the 5 bits to 16 but this gets complicated with negative numbers.
 * Example -1 in 5 bits is 1 1111 but if you extend by 11 zeros 0000 0001 1111
 * that is equal to 31.
 *
 * bit_count is how many bits is of the number you want to extend to 16 bits
 */
uint16_t sign_extend(uint16_t x, int bit_count) {
  // the if condition here is essentially compare against the sign bit and if 1
  // then we need to flip the upper bits to 1 for 2's compliment
  // otherwise we can just extend with 0s nothing needs to be done for this.
  if ((x >> (bit_count - 1) & 1)) {
    x |= (0xFFFF << bit_count);
  };
  return x;
}

void update_flags(uint16_t r) {
  if (reg[r] == 0) {
    reg[R_COND] = FL_ZRO;
  } else if (reg[r] >> 15) { /* a 1 in the left-most bit indicates a negative */
    reg[R_COND] = FL_NEG;
  } else {
    reg[R_COND] = FL_POS;
  }
}

uint16_t swap16(uint16_t x) { return (x << 8) | (x >> 8); }

void read_image_file(FILE *file) {
  /* the origin tells us where in memory to place the image */
  uint16_t origin;
  fread(&origin, sizeof(origin), 1, file);
  origin = swap16(origin);

  /* we know the maximum file size so we only need one fread */
  uint16_t max_read = MEMORY_MAX - origin;
  uint16_t *p = memory + origin;
  size_t read = fread(p, sizeof(uint16_t), max_read, file);

  /* swap to little endian */
  while (read-- > 0) {
    *p = swap16(*p);
    ++p;
  }
}

int read_image(const char *image_path) {
  FILE *file = fopen(image_path, "rb");
  if (!file) {
    return 0;
  };
  read_image_file(file);
  fclose(file);
  return 1;
}

int main(int argc, char *argv[]) {

  if (argc < 2) {
    printf("lc3 [image-file] ...\n");
    exit(2);
  }
  for (int j = 1; j < argc; ++j) {
    if (!read_image(argv[j])) {
      printf("failed to load image: %s\n", argv[j]);
      exit(1);
    }
  }

  signal(SIGINT, handle_interrupt);
  disable_input_buffering();

  /* since exactly one condition flag should be set any give time, set the Z
   * flag */
  reg[R_COND] = FL_ZRO;

  /* set the PC to starting position */
  /* 0x3000 is the default */
  enum { PC_START = 0x3000 };
  reg[R_PC] = PC_START;

  int running = 1;
  while (running) {
    /* FETCH */
    uint16_t instr = mem_read(reg[R_PC]++);
    uint16_t op = instr >> 12;

    switch (op) {
      case OP_ADD: {
        /*
        * ADD instruction takes two numbers, adds them together, and stores the
        * result in a register.
        *
        * DR = destination register, where the result gets stored
        * SR1 = register container the first number to add
        * 5th bit = mode register, 0 = register mode, 1 = immediate
        * In register mode the second number is stored just like the first SR2
        * e.g. ADD R2 R0 R1 ; In immediate mode the second value is embedded in
        * the instruction itself e.g. ADD R0 R0 1  ;
        *
        * register mode
        * ┌──────────┬────────┬────────┬──┬──────┬────────┐
        * │  4 bits  │ 3 bits │ 3 bits │1 │2 bits│ 3 bits │
        * ├──────────┼────────┼────────┼──┼──────┼────────┤
        * │   0001   │   DR   │  SR1   │0 │  00  │  SR2   │
        * └──────────┴────────┴────────┴──┴──────┴────────┘
        * immediate mode
        * ┌──────────┬────────┬────────┬──┬───────────────┐
        * │  4 bits  │ 3 bits │ 3 bits │1 │    5 bits     │
        * ├──────────┼────────┼────────┼──┼───────────────┤
        * │   0001   │   DR   │  SR1   │1 │     imm5      │
        * └──────────┴────────┴────────┴──┴───────────────┘
        */

        /* destination register DR */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* first operand (SR1) */
        uint16_t r1 = (instr >> 6) & 0x7;
        /* whether we are in immediate mode */
        uint16_t imm_flag = (instr >> 5) & 0x1;

        if (imm_flag) {
          uint16_t imm5 = sign_extend(instr & 0x1F, 5);
          reg[r0] = reg[r1] + imm5;
        } else {
          /* second operand (SR2) */
          uint16_t r2 = instr & 0x7;
          reg[r0] = reg[r1] + reg[r2];
        }
        update_flags(r0);

        break;
      }
      case OP_AND: {
        /*
        * AND instruction takes two numbers, does bitwise AND, and stores the
        * result in a register.
        *
        * DR = destination register, where the result gets stored
        * SR1 = register container the first number to add
        * 5th bit = mode register, 0 = register mode, 1 = immediate
        * In register mode the second number is stored just like the first SR2
        * e.g. AND R2 R0 R1 ; In immediate mode the second value is embedded in
        * the instruction itself e.g. AND R0 R0 1  ;
        *
        * register mode
        * ┌──────────┬────────┬────────┬──┬──────┬────────┐
        * │  4 bits  │ 3 bits │ 3 bits │1 │2 bits│ 3 bits │
        * ├──────────┼────────┼────────┼──┼──────┼────────┤
        * │   0101   │   DR   │  SR1   │0 │  00  │  SR2   │
        * └──────────┴────────┴────────┴──┴──────┴────────┘
        * immediate mode
        * ┌──────────┬────────┬────────┬──┬───────────────┐
        * │  4 bits  │ 3 bits │ 3 bits │1 │    5 bits     │
        * ├──────────┼────────┼────────┼──┼───────────────┤
        * │   0101   │   DR   │  SR1   │1 │     imm5      │
        * └──────────┴────────┴────────┴──┴───────────────┘
        */
        /* destination register DR */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* first operand (SR1) */
        uint16_t r1 = (instr >> 6) & 0x7;
        /* whether we are in immediate mode */
        uint16_t imm_flag = (instr >> 5) & 0x1;

        if (imm_flag) {
          uint16_t imm5 = sign_extend(instr & 0x1F, 5);
          reg[r0] = reg[r1] & imm5;
        } else {
          /* second operand (SR2) */
          uint16_t r2 = instr & 0x7;
          reg[r0] = reg[r1] & reg[r2];
        }
        update_flags(r0);
        break;
      }
      case OP_NOT: {
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t r1 = (instr >> 6) & 0x7;
        reg[r0] = ~reg[r1];
        update_flags(r0);
        break;
      }
      case OP_BR: {
        /*
        * BR uses condition bits n, z, p to decide whether to branch.
        * ┌──────────┬────────┬───────────────────────────┐
        * │  4 bits  │ 3 bits │           9 bits          │
        * ├──────────┼────────┼───────────────────────────┤
        * │   0000   │ n z p  │          PCoffset         │
        * └──────────┴────────┴───────────────────────────┘
        */
        /* desitination register (DR) */
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        uint16_t cond_flag = (instr >> 9) & 0x7;
        if (cond_flag & reg[R_COND]) {
          reg[R_PC] += pc_offset;
        }
        break;
      }
      case OP_JMP: {
        uint16_t r1 = (instr >> 6) & 0x7;
        reg[R_PC] = reg[r1];
        break;
      }
      case OP_JSR: {
        uint16_t long_flag = (instr >> 11) & 1;
        reg[R_R7] = reg[R_PC];
        if (long_flag) {
          uint16_t long_pc_offset = sign_extend(instr & 0x7FF, 11);
          reg[R_PC] += long_pc_offset;
        } else {
          uint16_t r1 = (instr >> 6) & 0x7;
          reg[R_PC] = reg[r1];
        }
        break;
      }
      case OP_LD: {
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        reg[r0] = mem_read(reg[R_PC] + pc_offset);
        update_flags(r0);
        break;
      }
      case OP_LDI: {
        /*
        * Load indrect instruction is used to load a value from location in
        * memory into a register
        * ┌──────────┬────────┬───────────────────────────┐
        * │  4 bits  │ 3 bits │           9 bits          │
        * ├──────────┼────────┼───────────────────────────┤
        * │   1010   │   DR   │          PCoffset         │
        * └──────────┴────────┴───────────────────────────┘
        */
        /* desitination register (DR) */
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t pc_offest = sign_extend(instr & 0x1FF, 9);
        reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offest));
        update_flags(r0);
        break;
      }
      case OP_LDR: {
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t r1 = (instr >> 6) & 0x7;
        uint16_t offset = sign_extend(instr & 0x3F, 6);
        reg[r0] = mem_read(reg[r1] + offset);
        update_flags(r0);
        break;
      }
      case OP_LEA: {
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        reg[r0] = reg[R_PC] + pc_offset;
        update_flags(r0);
        break;
      }
      case OP_ST: {
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        mem_write(reg[R_PC] + pc_offset, reg[r0]);
        break;
      }
      case OP_STI: {
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
        break;
      }
      case OP_STR: {
        uint16_t r0 = (instr >> 9) & 0x7;
        uint16_t r1 = (instr >> 6) & 0x7;
        uint16_t offset = sign_extend(instr & 0x3F, 6);
        mem_write(reg[r1] + offset, reg[r0]);
        break;
      }
      case OP_TRAP: {
        reg[R_R7] = reg[R_PC];
        switch (instr & 0xFF) {
        case TRAP_GETC: {
          /* read a single ASCII char */
          reg[R_R0] = (uint16_t)getchar();
          update_flags(R_R0);
          break;
        }
        case TRAP_OUT: {
          putc((char)reg[R_R0], stdout);
          fflush(stdout);
          break;
        }
        case TRAP_PUTS: {
          /* one char per word */
          uint16_t *c = memory + reg[R_R0];
          while (*c) {
            putc((char)*c, stdout);
            ++c;
          }
          fflush(stdout);
          break;
        }
        case TRAP_IN: {
          printf("Enter a character: ");
          char c = getchar();
          putc(c, stdout);
          fflush(stdout);
          reg[R_R0] = (uint16_t)c;
          update_flags(R_R0);
          break;
        }
        case TRAP_PUTSP: {
          /* one char per byte (two bytes per word)
                here we need to swap back to
                big endian format */
          uint16_t *c = memory + reg[R_R0];
          while (*c) {
            char char1 = (*c) & 0xFF;
            putc(char1, stdout);
            char char2 = (*c) >> 8;
            if (char2)
              putc(char2, stdout);
            ++c;
          }
          fflush(stdout);
          break;
        }
        case TRAP_HALT: {
          puts("HALT");
          fflush(stdout);
          running = 0;
          break;
        }
        case OP_RES:
        case OP_RTI:
        default:
          abort();
          break;
        }
      }
    }
  }
  restore_input_buffering();
}