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
  bfit("++++++++++[>+++++++>++++++++++>+++>+<<<<-]>++.>+.+++++++..+++.>++.<<+++++++++++++++.>.+++.------.--------.>+.>.<<<<", NULL, NULL);
  return 0;
}
