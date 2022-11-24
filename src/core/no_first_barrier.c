#include "no_first_barrier.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_no_first_barrier(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, flag, first = 1;
  char line[1000];
  enum eassembler_type estring1;
  struct parameters_block *parameters;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if (first && (estring1 == esocket_barrier)) {
          first = 0;
        } else {
          nbuffer_out +=
              ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
        }
      }
    }
  } while (flag);
  ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
}
