#include "optimise_multi_socket.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_optimise_multi_socket(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce, data_memcpy_reduce_;
  int nbuffer_out = 0, nbuffer_in = 0, flag1, flag2, flag3, flag4 = 1, line2_new = -1, nline2;
  char line[1000], line2[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1;
  memset(line, 0, 1000);
  memset(line2, 0, 1000);
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if ((estring1 == ememcpy) || (estring1 == ememcp_)) {
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
            if ((data_memcpy_reduce.type == ememcpy || data_memcpy_reduce.type == ememcp_) && data_memcpy_reduce.buffer_number2 > 0 ) {
              nline2 = flag1 = ext_mpi_read_line(buffer_in + nbuffer_in, line2, parameters->ascii_in);
              flag2 = 1;
              while (flag1 && flag2) {
                if (ext_mpi_read_assembler_line(line2, 0, "s", &estring1) >= 0) {
                  if ((estring1 == ereduce) || (estring1 == ereduc_)) {
                    ext_mpi_read_memcpy_reduce(line2, &data_memcpy_reduce_);
                  } else {
                    data_memcpy_reduce_.type = enop;
                  }
                }
                if ((data_memcpy_reduce_.type == ereduce || data_memcpy_reduce_.type == ereduc_) && data_memcpy_reduce_.buffer_type1 == eshmemo && data_memcpy_reduce_.buffer_type2 == eshmemo && data_memcpy_reduce.offset1 == data_memcpy_reduce_.offset2 && data_memcpy_reduce.size == data_memcpy_reduce_.size && data_memcpy_reduce.buffer_type1 == eshmemo && data_memcpy_reduce.buffer_type2 == eshmemo) {
                  data_memcpy_reduce.type = data_memcpy_reduce_.type;
                  data_memcpy_reduce.offset1 = data_memcpy_reduce_.offset1;
                  flag2 = 0;
                  flag4 = 0;
                  line2_new = nbuffer_in + nline2;
                } else {
                  nline2 += flag1 = ext_mpi_read_line(buffer_in + nbuffer_in + nline2, line2, parameters->ascii_in);
                  if (data_memcpy_reduce_.type == ereduce || data_memcpy_reduce_.type == ereduc_) {
                    flag2 = 0;
                  }
                }
              }
            }
          }
        }
      }
      if (nbuffer_in != line2_new && flag4) {
        nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      } else if (flag4) {
        nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
      }
      flag4 = 1;
    }
  } while (flag3);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
}
