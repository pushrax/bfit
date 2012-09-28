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
#include "bfit.h"

int main()
{
  char *input = malloc(4);
  input[0] = 12;
  input[1] = 31;
  input[2] = 1;
  bfit("++++++++++[>+++++++>++++++++++>+++>+<<<<-]>++.>+.+++++++..+++.>++.<<+++++++++++++++.>.+++.------.--------.>+.>.<<<<", input, NULL);
  free(input);
  return 0;
}
