#include "move_first_memcpy.h"
#include "constants.h"
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
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      switch (flag2) {
      case 0:
        if (ext_mpi_read_assembler_line_ssdsdd(line, &estring1, &estring2, &o1,
                                               &estring3, &o2, &size, 0) >= 0) {
          if (estring1 != ememcpy) {
            nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                              parameters->ascii_out);
          } else {
            flag2 = 1;
          }
        }
        break;
      case 1:
        if (ext_mpi_read_assembler_line_s(line, &estring1, 0) >= 0) {
          if (estring1 == eisend) {
            ext_mpi_read_assembler_line_ssdddd(line, &estring1_, &estring2_, &o1_,
                                               &size_, &partner, &num_comm, 0);
            nbuffer_out += ext_mpi_write_assembler_line_ssdddd(
                buffer_out + nbuffer_out, estring1_, esendbufp, o1_, size_,
                partner, num_comm, parameters->ascii_out);
          } else {
            if ((estring1 == ewaitall)||(estring1 == ewaitany)) {
              nbuffer_out += ext_mpi_write_assembler_line_ssdsdd(
                  buffer_out + nbuffer_out, ememcpy, estring2, o1, estring3, o2,
                  size, parameters->ascii_out);
              flag2 = 2;
            }
            nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                              parameters->ascii_out);
          }
        }
        break;
      default:
        nbuffer_out +=
            ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    }
  } while (flag);
  ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
