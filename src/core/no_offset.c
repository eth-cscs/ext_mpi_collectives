#include "no_offset.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_no_offset(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, bo1, bo2, o1, o2, *buffer_offset,
      buffer_offset_max, flag, i, j, k;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring2, estring3, estring4, estring5;
  nbuffer_in += read_parameters(buffer_in + nbuffer_in, &parameters);
  buffer_offset_max = parameters->shmem_buffer_offset_max;
  buffer_offset = parameters->shmem_buffer_offset;
  parameters->shmem_max = buffer_offset[buffer_offset_max - 1];
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag > 0) {
      if (read_assembler_line_s(line, &estring1, 0) >= 0) {
        if (((estring1 == eirecv) || (estring1 == eisend) ||
             (estring1 == eirec_) || (estring1 == eisen_)) &&
            (read_assembler_line_ssdsdddd(line, &estring1, &estring2, &bo1,
                                          &estring3, &o1, &i, &j, &k,
                                          0) >= 0)) {
          nbuffer_out += write_assembler_line_ssdddd(
              buffer_out + nbuffer_out, estring1, eshmemp,
              buffer_offset[bo1] + o1, i, j, k, parameters->ascii_out);
        } else {
          if (((estring1 == ememcpy) || (estring1 == ereduce) ||
               (estring1 == ememcp_) || (estring1 == ereduc_) ||
               (estring1 == esreduce)) &&
              (read_assembler_line_ssdsdsdsdd(line, &estring1, &estring2, &bo1,
                                              &estring3, &o1, &estring4, &bo2,
                                              &estring5, &o2, &i, 0) >= 0)) {
            nbuffer_out += write_assembler_line_ssdsdd(
                buffer_out + nbuffer_out, estring1, eshmemp,
                buffer_offset[bo1] + o1, eshmemp, buffer_offset[bo2] + o2, i,
                parameters->ascii_out);
          } else {
            nbuffer_out += write_line(buffer_out + nbuffer_out, line,
                                      parameters->ascii_out);
          }
        }
      }
    }
  } while (flag > 0);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  delete_parameters(parameters);
  return nbuffer_out;
}
