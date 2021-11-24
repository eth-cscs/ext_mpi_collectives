#include "no_first_barrier.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_no_first_barrier(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, flag, first = 1;
  char line[1000];
  enum eassembler_type estring1;
  struct parameters_block *parameters;
  nbuffer_in += read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      if (read_assembler_line_s(line, &estring1, 0) >= 0) {
        if (first && (estring1 == enode_barrier)) {
          first = 0;
        } else {
          nbuffer_out +=
              write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
        }
      }
    }
  } while (flag);
  write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  return nbuffer_out;
}
