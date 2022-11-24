#include "move_first_memcpy.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_move_first_memcpy(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend data_irecv_isend;
  int nbuffer_out = 0, nbuffer_in = 0, i, flag, flag2 = 0;
  char line[1000];
  enum eassembler_type estring1;
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
        if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
          if (data_memcpy_reduce.type != ememcpy) {
            nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                              parameters->ascii_out);
          } else {
            flag2 = 1;
          }
        }
        break;
      case 1:
        if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
          if (estring1 == eisend) {
            ext_mpi_read_irecv_isend(line, &data_irecv_isend);
            data_irecv_isend.buffer_type = esendbufp;
            ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
          } else {
            if ((estring1 == ewaitall)||(estring1 == ewaitany)) {
              nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
              flag2 = 2;
            }
            nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
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
