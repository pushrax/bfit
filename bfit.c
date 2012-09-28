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
BfitInsn *bfit_lex(const char *source, unsigned int *total_out)
{
  BfitInsn *insns;
  unsigned int length = strlen(source), total = 0, index = 0, i, j;
  char c;

  if (length <= 0) return NULL;

  // Pre-calculate number of instructions
  for (i = 0; i < length; ++i)
  {
    c = source[i];
    if (c == '>' || c == '<' || c == '+' || c == '-')
    {
      for (j = i + 1; j < length; ++j) if (c != source[j]) break;
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
      for (j = i + 1; j < length; ++j) if (c != source[j]) break;
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
byte *bfit_compile(const BfitInsn *insns, unsigned int count, const byte *data, const char *input, unsigned int *length)
{
  unsigned int cl = 0, i, dataptr = (unsigned int) data, offset;
  byte *code;

  // Pre-calculate length of machine code
  for (i = 0; i < count; ++i)
  {
    char type = insns[i].type;
    if (type == '>' || type == '<') cl += 5;
    else if (type == '+' || type == '-') cl += 7;
    else if (type == '[') cl += 13;
    else if (type == ']') cl += 5;
    else if (type == '.') cl += 26;
    else if (type == ',') cl += 18;
  }

  cl += 9; // init
  cl += 10; // de-init

  code = malloc(cl);
  cl = 0;

  // Conditional jump stack
  unsigned int stack[512], stackptr = 0;

  // Assembly comments are in Intel syntax

  // Set up stack frame
  code[cl++] = 0x55; // push ebp
  code[cl++] = 0x89; // mov ebp, esp
  code[cl++] = 0xe5;

  // Set up data pointer
  code[cl++] = 0xb8; // mov eax, 0
  *(unsigned int *)(code + cl) = 0; cl += 4;

  // Set up input pointer
  code[cl++] = 0x50; // push eax

  // Generate code
  for (i = 0; i < count; ++i)
  {
    switch (insns[i].type)
    {
      case '>':
        code[cl++] = 0x05; // add eax, times
        *(unsigned int *)(code + cl) = insns[i].times; cl += 4;
        break;
      case '<':
        code[cl++] = 0x2d; // sub eax, times
        *(unsigned int *)(code + cl) = insns[i].times; cl += 4;
        break;
      case '+':
        code[cl++] = 0x80; // add byte [data+eax], times
        code[cl++] = 0x80;
        *(unsigned int *)(code + cl) = dataptr; cl += 4;
        code[cl++] = insns[i].times;
        break;
      case '-':
        code[cl++] = 0x80; // sub byte [data+eax], times
        code[cl++] = 0xa8;
        *(unsigned int *)(code + cl) = dataptr; cl += 4;
        code[cl++] = insns[i].times;
        break;
      case '[':
        stack[stackptr++] = cl;
        code[cl++] = 0x80; // cmp byte [data+eax], 0
        code[cl++] = 0xb8;
        *(unsigned int *)(code + cl) = dataptr; cl += 4;
        code[cl++] = 0x00;
        code[cl++] = 0x0f; // jz/je offset
        code[cl++] = 0x84;
        cl += 4; // offset filled in by ']'
        break;
      case ']':
        offset = cl - stack[--stackptr] + 5;
        code[cl++] = 0xe9; // jmp -offset
        *(int *)(code + cl) = -offset; cl += 4;
        
        // fill in offset for matching '['
        *(unsigned int *)(code + stack[stackptr] + 9) = cl - stack[stackptr] - 13;
        break;
      case '.':
        code[cl++] = 0x50; // push eax
        code[cl++] = 0xba; // mov edx, 1
        *(unsigned int *)(code + cl) = 1; cl += 4;
        code[cl++] = 0xb9; // mov ecx, data
        *(unsigned int *)(code + cl) = dataptr; cl += 4;
        code[cl++] = 0x01; // add ecx, eax
        code[cl++] = 0xc1;
        code[cl++] = 0xbb; // mov ebx, 1
        *(unsigned int *)(code + cl) = 1; cl += 4;
        code[cl++] = 0xb8; // mov eax, 4
        *(unsigned int *)(code + cl) = 4; cl += 4;
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
        *(unsigned int *)(code + cl) = (unsigned int) input; cl += 4;
        code[cl++] = 0x88; // mov byte [data+eax], bl
        code[cl++] = 0x98;
        *(unsigned int *)(code + cl) = dataptr; cl += 4;
        code[cl++] = 0xff; // inc dword [esp]
        code[cl++] = 0x04;
        code[cl++] = 0x24;
        break;
    }
  }

  // Clean up stack
  code[cl++] = 0x5b; // pop ebx

  // Unwind stack and return
  code[cl++] = 0x0f; // movzx eax, byte [data+eax]
  code[cl++] = 0xb6;
  code[cl++] = 0x80;
  *(unsigned int *)(code + cl) = dataptr; cl += 4;
  code[cl++] = 0xc9; // leave
  code[cl++] = 0xc3; // ret

  *length = cl;
  return code;
}

/*
 * Sticks a string of machine code into executable memory and runs it
 */
int bfit_run(const byte *source, unsigned int length, int *ret)
{
  unsigned int pagesize = getpagesize();

  // Allocate enough to overflow into the next page
  byte *block = malloc(length + pagesize + 1);
  if (block == NULL) return 2;

  // Calculate the page boundary
  byte *code = (byte *)(((uintptr_t) block + pagesize - 1) & ~(pagesize - 1));

  // Turn off DEP
  if (mprotect(code, length + 1,  PROT_READ | PROT_WRITE | PROT_EXEC))
  {
    printf("mprotect failed!!!\n");
    return 1;
  }

  memcpy(code, source, length);

  // Run dat shit
  *ret = ((int (*)()) code)();

  free(block);
  return 0;
}

/*
 * Run a string of brainfuck
 */
int bfit(const char *source, char *input, byte *data)
{
  char malloced_data = 0;
  if (data == NULL)
  {
    data = malloc(512);
    memset(data, 0, 512);
    malloced_data = 1;
  }

  unsigned int count, length;
  int ret;

  BfitInsn *insns = bfit_lex(source, &count);
  printf("Generated %d instructions\n", count);

  byte *code = bfit_compile(insns, count, data, input, &length);
  printf("Generated %d bytes of machine code\n", length);

  bfit_run(code, length, &ret);
  printf("\n");
  printf("Program returned %d\n", ret);

  printf("%d, %d\n", data[0], data[1]);

  free(insns);
  free(code);

  if (malloced_data) free(data);
  return 0;
}

