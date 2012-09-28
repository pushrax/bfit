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
 */
uint8_t *bfit_compile(const BfitInsn *insns, uint32_t count, const uint8_t *data, const char *input, uint32_t *length)
{
  uint32_t cl = 0, i, offset;
  uint8_t *code;

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
  cl = 0;

  // Conditional jump stack
  uint32_t stack[512], stackptr = 0;

  // Assembly comments are in Intel syntax

  // Set up stack frame
  code[cl++] = 0x55; // push ebp
  code[cl++] = 0x89; // mov ebp, esp
  code[cl++] = 0xe5;

  // Set up data pointer
  code[cl++] = 0xb8; // mov eax, data
  *(uint32_t *)(code + cl) = (uint32_t) data; cl += 4;

  // Set up input pointer
  code[cl++] = 0x68; // push 0
  *(uint32_t *)(code + cl) = 0; cl += 4;

  // Generate code
  for (i = 0; i < count; ++i)
  {
    switch (insns[i].type)
    {
      case '>':
        code[cl++] = 0x83; // add eax, byte times
        code[cl++] = 0xc0;
        code[cl++] = insns[i].times;
        break;
      case '<':
        code[cl++] = 0x83; // sub eax, byte times
        code[cl++] = 0xe8;
        code[cl++] = insns[i].times;
        break;
      case '+':
        code[cl++] = 0x80; // add byte [eax], times
        code[cl++] = 0x00;
        code[cl++] = insns[i].times;
        break;
      case '-':
        code[cl++] = 0x80; // sub byte [eax], times
        code[cl++] = 0x28;
        code[cl++] = insns[i].times;
        break;
      case '[':
        stack[stackptr++] = cl;
        code[cl++] = 0x80; // cmp byte [eax], 0
        code[cl++] = 0x38;
        code[cl++] = 0x00;
        code[cl++] = 0x0f; // jz/je offset
        code[cl++] = 0x84;
        cl += 4; // offset filled in by ']'
        break;
      case ']':
        offset = cl - stack[--stackptr] + 5;
        code[cl++] = 0xe9; // jmp -offset
        *(int32_t *)(code + cl) = -offset; cl += 4;
        
        // fill in offset for matching '['
        *(uint32_t *)(code + stack[stackptr] + 5) = cl - stack[stackptr] - 9;
        break;
      case '.':
        code[cl++] = 0x50; // push eax
        code[cl++] = 0xba; // mov edx, 1
        *(uint32_t *)(code + cl) = 1; cl += 4;
        code[cl++] = 0x89; // mov ecx, eax
        code[cl++] = 0xc1;
        code[cl++] = 0xbb; // mov ebx, 1
        *(uint32_t *)(code + cl) = 1; cl += 4;
        code[cl++] = 0xb8; // mov eax, 4
        *(uint32_t *)(code + cl) = 4; cl += 4;
        code[cl++] = 0xcd; // int 80h
        code[cl++] = 0x80;
        code[cl++] = 0x58; // pop eax
        break;
      case ',':
        code[cl++] = 0x8b; // mov ebx, [esp]
        code[cl++] = 0x1c;
        code[cl++] = 0x24;
        code[cl++] = 0x8a; // mov bl, [input+ebx]
        code[cl++] = 0x9b;
        *(uint32_t *)(code + cl) = (uint32_t) input; cl += 4;
        code[cl++] = 0x88; // mov uint8_t [eax], bl
        code[cl++] = 0x18;
        code[cl++] = 0xff; // inc dword [esp]
        code[cl++] = 0x04;
        code[cl++] = 0x24;
        break;
    }
  }

  // Clean up stack
  code[cl++] = 0x5b; // pop ebx

  // Unwind stack and return [eax]
  code[cl++] = 0x0f; // movzx eax, byte [eax]
  code[cl++] = 0xb6;
  code[cl++] = 0x00;
  code[cl++] = 0xc9; // leave
  code[cl++] = 0xc3; // ret

  *length = cl;
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
  *ret = ((int32_t (*)()) code)();

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

