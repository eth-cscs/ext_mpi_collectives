#include "constants.h"
#include "no_node_barriers.h"
#include "read.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_move_first_memcpy(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, i, flag, flag2 = 0, o1, o2, size, o1_,
      size_, partner, num_comm;
  char line[1000];
  enum eassembler_type estring1, estring2, estring3, estring1_, estring2_;
  struct parameters_block *parameters;
  nbuffer_in += i = read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      switch (flag2) {
      case 0:
        if (read_assembler_line_ssdsdd(line, &estring1, &estring2, &o1,
                                       &estring3, &o2, &size, 0) >= 0) {
          if (estring1 != ememcpy) {
            nbuffer_out += write_line(buffer_out + nbuffer_out, line,
                                      parameters->ascii_out);
          } else {
            flag2 = 1;
          }
        }
        break;
      case 1:
        if (read_assembler_line_s(line, &estring1, 0) >= 0) {
          if (estring1 == eisend) {
            read_assembler_line_ssdddd(line, &estring1_, &estring2_, &o1_,
                                       &size_, &partner, &num_comm, 0);
            nbuffer_out += write_assembler_line_ssdddd(
                buffer_out + nbuffer_out, estring1_, esendbufp, o1_, size_,
                partner, num_comm, parameters->ascii_out);
          } else {
            if ((estring1 == ewaitall)||(estring1 == ewaitany)) {
              nbuffer_out += write_assembler_line_ssdsdd(
                  buffer_out + nbuffer_out, ememcpy, estring2, o1, estring3, o2,
                  size, parameters->ascii_out);
              flag2 = 2;
            }
            nbuffer_out += write_line(buffer_out + nbuffer_out, line,
                                      parameters->ascii_out);
          }
        }
        break;
      default:
        nbuffer_out +=
            write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    }
  } while (flag);
  write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
