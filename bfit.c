/*
 * bfit -- A JIT compiler for brainfuck.
 *
 * This is the result of being quite bored in linear algebra and wanting to learn more assembly.
 *
 * Copyright(c) 2012 Justin Li <j-li.net>
 * MIT Licensed
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "bfit.h"


/*
 * Builds a slightly optimized list of instructions
 */
BfitInsn *bfit_lex(const char *source, uint32_t *total_out)
{
  BfitInsn *insns;
  uint32_t length = strlen(source), total = 0, index = 0, i, j;
  char c;

  if (length <= 0) return NULL;

  // Pre-calculate number of instructions
  for (i = 0; i < length; ++i)
  {
    c = source[i];
    if (c == '>' || c == '<' || c == '+' || c == '-')
    {
      for (j = i + 1; j < length && j - i < 255; ++j) if (c != source[j]) break;
      i = j - 1;
      ++total;
    }
    else if (c == '[' || c == ']' || c == '.' || c == ',')
    {
      ++total;
    }
  }

  insns = malloc(total * sizeof(BfitInsn));

  // Generate instructions
  for (i = 0; i < length; ++i)
  {
    c = source[i];
    if (c == '>' || c == '<' || c == '+' || c == '-')
    {
      for (j = i + 1; j < length && j - i < 255; ++j) if (c != source[j]) break;
      insns[index++] = (BfitInsn) { c, j - i };
      i = j - 1;
    }
    else if (c == '[' || c == ']' || c == '.' || c == ',')
    {
      insns[index++] = (BfitInsn) { c, 1 };
    }
  }

  *total_out = total;

  return insns;
};

/*
 * "Compiles" a list of instructions into a string of x86 machine code
 * - Assembly comments are in NASM (Intel) syntax
 */
uint8_t *bfit_compile(const BfitInsn *insns, uint32_t count, const uint8_t *data, const char *input, uint32_t *length)
{
  uint32_t cl = 0, i, offset;

  // Machine code buffer
  uint8_t *code, *cp;

  // Conditional jump stack
  uint8_t *stack[512];
  uint32_t stackptr = 0;

#ifdef BFIT_X86

  // Pre-calculate length of machine code
  for (i = 0; i < count; ++i)
  {
    char type = insns[i].type;
    if (type == '>' || type == '<') cl += 3;
    else if (type == '+' || type == '-') cl += 3;
    else if (type == '[') cl += 9;
    else if (type == ']') cl += 5;
    else if (type == '.') cl += 21;
    else if (type == ',') cl += 14;
  }

  cl += 13; // init
  cl += 6; // de-init

  code = malloc(cl);
  cp = code;

  // Set up stack frame
  *cp++ = 0x55; // push ebp
  *cp++ = 0x89; // mov ebp, esp
  *cp++ = 0xe5;

  // Set up data pointer
  *cp++ = 0xb8; // mov eax, data
  *(uint32_t *) cp = (uint32_t) data; cp += 4;

  // Set up input pointer
  *cp++ = 0x68; // push 0
  *(uint32_t *) cp = 0; cp += 4;

  // Generate code
  for (i = 0; i < count; ++i)
  {
    switch (insns[i].type)
    {
      case '>':
        *cp++ = 0x83; // add eax, byte times
        *cp++ = 0xc0;
        *cp++ = insns[i].times;
        break;
      case '<':
        *cp++ = 0x83; // sub eax, byte times
        *cp++ = 0xe8;
        *cp++ = insns[i].times;
        break;
      case '+':
        *cp++ = 0x80; // add byte [eax], times
        *cp++ = 0x00;
        *cp++ = insns[i].times;
        break;
      case '-':
        *cp++ = 0x80; // sub byte [eax], times
        *cp++ = 0x28;
        *cp++ = insns[i].times;
        break;
      case '[':
        stack[stackptr++] = cp;
        *cp++ = 0x80; // cmp byte [eax], 0
        *cp++ = 0x38;
        *cp++ = 0x00;
        *cp++ = 0x0f; // jz/je offset
        *cp++ = 0x84;
        cp += 4; // offset filled in by ']'
        break;
      case ']':
        offset = cp - stack[--stackptr] + 5;
        *cp++ = 0xe9; // jmp -offset
        *(int32_t *) cp = -offset; cp += 4;
        
        // fill in offset for matching '['
        *(uint32_t *)(stack[stackptr] + 5) = cp - stack[stackptr] - 9;
        break;
      case '.':
        *cp++ = 0x50; // push eax
        *cp++ = 0xba; // mov edx, 1
        *(uint32_t *) cp = 1; cp += 4;
        *cp++ = 0x89; // mov ecx, eax
        *cp++ = 0xc1;
        *cp++ = 0xbb; // mov ebx, 1
        *(uint32_t *) cp = 1; cp += 4;
        *cp++ = 0xb8; // mov eax, 4
        *(uint32_t *) cp = 4; cp += 4;
        *cp++ = 0xcd; // int 80h
        *cp++ = 0x80;
        *cp++ = 0x58; // pop eax
        break;
      case ',':
        *cp++ = 0x8b; // mov ebx, [esp]
        *cp++ = 0x1c;
        *cp++ = 0x24;
        *cp++ = 0x8a; // mov bl, [input+ebx]
        *cp++ = 0x9b;
        *(uint32_t *) cp = (uint32_t) input; cp += 4;
        *cp++ = 0x88; // mov uint8_t [eax], bl
        *cp++ = 0x18;
        *cp++ = 0xff; // inc dword [esp]
        *cp++ = 0x04;
        *cp++ = 0x24;
        break;
    }
  }

  // Clean up stack
  *cp++ = 0x5b; // pop ebx

  // Unwind stack and return [eax]
  *cp++ = 0x0f; // movzx eax, byte [eax]
  *cp++ = 0xb6;
  *cp++ = 0x00;
  *cp++ = 0xc9; // leave
  *cp++ = 0xc3; // ret
#endif

#ifdef BFIT_X64

  // Pre-calculate length of machine code
  for (i = 0; i < count; ++i)
  {
    char type = insns[i].type;
    if (type == '>' || type == '<') cl += 4;
    else if (type == '+' || type == '-') cl += 3;
    else if (type == '[') cl += 9;
    else if (type == ']') cl += 5;
    else if (type == '.') cl += 37;
    else if (type == ',') cl += 12;
  }

  cl += 25; // init
  cl += 7; // de-init

  code = malloc(cl);
  cp = code;

  // Set up stack frame
  *cp++ = 0x55; // push rbp
  *cp++ = 0x48; // mov rbp, rsp
  *cp++ = 0x89;
  *cp++ = 0xe5;

  // Set up data pointer
  *cp++ = 0x48; // mov rax, data
  *cp++ = 0xb8;
  *(uint64_t *) cp = (uint64_t) data; cp += 8;

  // Set up input pointer
  *cp++ = 0x48; // mov rbx, input
  *cp++ = 0xbb;
  *(uint64_t *) cp = (uint64_t) input; cp += 8;
  *cp++ = 0x53; // push rbx

  // Generate code
  for (i = 0; i < count; ++i)
  {
    switch (insns[i].type)
    {
      case '>':
        *cp++ = 0x48; // add rax, byte times
        *cp++ = 0x83;
        *cp++ = 0xc0;
        *cp++ = insns[i].times;
        break;
      case '<':
        *cp++ = 0x48; // sub rax, byte times
        *cp++ = 0x83;
        *cp++ = 0xe8;
        *cp++ = insns[i].times;
        break;
      case '+':
        *cp++ = 0x80; // add byte [rax], times
        *cp++ = 0x00;
        *cp++ = insns[i].times;
        break;
      case '-':
        *cp++ = 0x80; // sub byte [rax], times
        *cp++ = 0x28;
        *cp++ = insns[i].times;
        break;
      case '[':
        stack[stackptr++] = cp;
        *cp++ = 0x80; // cmp byte [rax], 0
        *cp++ = 0x38;
        *cp++ = 0x00;
        *cp++ = 0x0f; // jz/je offset
        *cp++ = 0x84;
        cp += 4; // offset filled in by ']'
        break;
      case ']':
        offset = cp - stack[--stackptr] + 5;
        *cp++ = 0xe9; // jmp -offset
        *(int32_t *) cp = -offset; cp += 4;
        
        // fill in offset for matching '['
        *(uint32_t *)(stack[stackptr] + 5) = cp - stack[stackptr] - 9;
        break;
      case '.':
        *cp++ = 0x50; // push rax
        *cp++ = 0x48; // mov rdi, 1   ; stdout
        *cp++ = 0xbf;
        *(uint64_t *) cp = 1; cp += 8;
        *cp++ = 0x48; // mov rsi, rax ; data
        *cp++ = 0x89;
        *cp++ = 0xc6;
        *cp++ = 0x48; // mov rdx, 1   ; length
        *cp++ = 0xba;
        *(uint64_t *) cp = 1; cp += 8;
        *cp++ = 0x48; // mov rax, 1   ; sys_write
        *cp++ = 0xb8;
        *(uint64_t *) cp = 1; cp += 8;
        *cp++ = 0x0f; // syscall
        *cp++ = 0x05;
        *cp++ = 0x58; // pop eax
        break;
      case ',':
        *cp++ = 0x48; // mov rbx, [rsp] ; input
        *cp++ = 0x8b;
        *cp++ = 0x1c;
        *cp++ = 0x24;
        *cp++ = 0x8a; // mov bl, [rbx]
        *cp++ = 0x1b;
        *cp++ = 0x88; // mov byte [rax], bl
        *cp++ = 0x18;
        *cp++ = 0x48; // inc qword [rsp]
        *cp++ = 0xff;
        *cp++ = 0x04;
        *cp++ = 0x24;
        break;
    }
  }

  // Clean up stack
  *cp++ = 0x5b; // pop rbx

  // Unwind stack and return [eax]
  *cp++ = 0x48; // movzx eax, byte [eax]
  *cp++ = 0x0f;
  *cp++ = 0xb6;
  *cp++ = 0x00;
  *cp++ = 0xc9; // leave
  *cp++ = 0xc3; // ret

#endif


  *length = (uint32_t)(cp - code);
  return code;
}

/*
 * Sticks a string of machine code into executable memory and runs it
 */
int bfit_run(const uint8_t *source, uint32_t length, int32_t *ret)
{
  uint32_t pagesize = getpagesize();

  // Allocate enough to overflow into the next page
  uint8_t *block = malloc(length + pagesize + 1);
  if (block == NULL) return 2;

  // Calculate the page boundary
  uint8_t *code = (uint8_t *)(((uintptr_t) block + pagesize - 1) & ~(pagesize - 1));

  // Turn off DEP
  if (mprotect(code, length + 1,  PROT_READ | PROT_WRITE | PROT_EXEC))
  {
    printf("mprotect failed!!!\n");
    return 1;
  }

  memcpy(code, source, length);

  // Run dat shit
  *ret = ((int64_t (*)()) code)();

  free(block);
  return 0;
}

/*
 * Run a string of brainfuck
 */
int bfit(const char *source, const char *input, uint8_t *data)
{
  char malloced_data = 0;
  if (data == NULL)
  {
    data = malloc(512);
    memset(data, 0, 512);
    malloced_data = 1;
  }

  uint32_t count, length;
  int ret;

  BfitInsn *insns = bfit_lex(source, &count);
  printf("Generated %d instructions\n", count);

  uint8_t *code = bfit_compile(insns, count, data, input, &length);
  printf("Generated %d bytes of machine code\n", length);

  bfit_run(code, length, &ret);
  printf("\n");
  printf("Program returned %d\n", ret);

  free(insns);
  free(code);

  if (malloced_data) free(data);
  return 0;
}

