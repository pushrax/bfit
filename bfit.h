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

typedef unsigned char byte;

typedef struct
{
  char type;
  byte times;
} BfitInsn;

BfitInsn *bfit_lex(const char *source, unsigned int *total_out);

byte *bfit_compile(const BfitInsn *insns, unsigned int count, const byte *data, const char *input, unsigned int *length);

int bfit_run(const byte *source, unsigned int length, int *ret);

int bfit(const char *source, char *input, byte *data);

#endif
