/*
 * bfit -- A JIT compiler for brainfuck.
 *
 * This is the result of being quite bored in linear algebra and wanting to learn more assembly.
 *
 * Copyright(c) 2012 Justin Li <j-li.net>
 * MIT Licensed
 */

#ifndef BFIT_H_INCLUDED
#define BFIT_H_INCLUDED

#include <stdint.h>

#ifdef __GNUC__

#ifdef __i386__
#define BFIT_X86
#endif

#ifdef __x86_64__
#define BFIT_X64
#endif

#endif

typedef struct
{
  char type;
  uint8_t times;
} BfitInsn;

BfitInsn *bfit_lex(const char *source, uint32_t *total_out);

uint8_t *bfit_compile(const BfitInsn *insns, uint32_t count, const uint8_t *data, const char *input, uint32_t *length);

int bfit_run(const uint8_t *source, uint32_t length, int *ret);

int bfit(const char *source, const char *input, uint8_t *data);

#endif
