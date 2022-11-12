#include "raw_code_merge.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_raw_code_merge(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce, data_memcpy_reduce_, data_memcpy_reduce__;
  int nbuffer_out = 0, nbuffer_in = 0, i, nin_new, flag, flag3, flag4, flag5;
  char line[1000], line2[1000];
  enum eassembler_type estring1;
  struct parameters_block *parameters;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if ((flag3 > 0) &&
        (flag4 = ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0)) {
      flag = 1;
      if ((estring1 == ememcpy) || (estring1 == ereduce) ||
          (estring1 == ememcp_) || (estring1 == ereduc_)) {
        ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce);
        nin_new = nbuffer_in;
        flag5 =
            ext_mpi_read_line(buffer_in + nbuffer_in, line2, parameters->ascii_in);
        if (ext_mpi_read_memcpy_reduce(line2, &data_memcpy_reduce_) >= 0) {
          while ((flag5 > 0) && (data_memcpy_reduce.type == data_memcpy_reduce_.type) && (data_memcpy_reduce.offset_number1 == data_memcpy_reduce_.offset_number1) &&
                 (data_memcpy_reduce.offset_number2 == data_memcpy_reduce_.offset_number2) && (data_memcpy_reduce.buffer_number1 == data_memcpy_reduce_.buffer_number1) && 
                 (data_memcpy_reduce.buffer_number2 == data_memcpy_reduce_.buffer_number2) && (data_memcpy_reduce.offset1 + data_memcpy_reduce.size == data_memcpy_reduce_.offset1) &&
                 (data_memcpy_reduce.offset2 + data_memcpy_reduce.size == data_memcpy_reduce_.offset2) &&
                 (data_memcpy_reduce.offset1 != data_memcpy_reduce_.offset2)) {
            data_memcpy_reduce.size += data_memcpy_reduce_.size;
            nin_new += flag5;
            flag5 =
                ext_mpi_read_line(buffer_in + nin_new, line2, parameters->ascii_in);
            flag = 0;
            if (flag5 > 0) {
              if (ext_mpi_read_memcpy_reduce(line2, &data_memcpy_reduce_) < 0) {
                data_memcpy_reduce_.offset1 = data_memcpy_reduce_.offset2 = 0;
              }
            }
          }
          nbuffer_in = nin_new;
          if (!flag) {
            nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
          } else {
            while ((flag5 > 0) && (data_memcpy_reduce.type == data_memcpy_reduce_.type) && (data_memcpy_reduce.offset_number1 == data_memcpy_reduce_.offset_number1) &&
                   (data_memcpy_reduce.offset_number2 == data_memcpy_reduce_.offset_number2) && (data_memcpy_reduce.buffer_number1 == data_memcpy_reduce_.buffer_number1) &&
                   (data_memcpy_reduce.buffer_number2 == data_memcpy_reduce_.buffer_number2) && (data_memcpy_reduce.offset1 - data_memcpy_reduce.size == data_memcpy_reduce_.offset1) &&
                   (data_memcpy_reduce.offset2 - data_memcpy_reduce.size == data_memcpy_reduce_.offset2)) {
              data_memcpy_reduce.size += data_memcpy_reduce_.size;
              nin_new += flag5;
              flag5 =
                  ext_mpi_read_line(buffer_in + nin_new, line2, parameters->ascii_in);
              flag = 0;
              data_memcpy_reduce__.offset1 = data_memcpy_reduce_.offset1;
              data_memcpy_reduce__.offset2 = data_memcpy_reduce_.offset2;
              if (flag5 > 0) {
                if (ext_mpi_read_memcpy_reduce(line2, &data_memcpy_reduce_) < 0) {
                  data_memcpy_reduce_.offset1 = data_memcpy_reduce_.offset2 = 0;
                }
              }
            }
            nbuffer_in = nin_new;
            if (!flag) {
              i = data_memcpy_reduce.offset1; data_memcpy_reduce.offset1 = data_memcpy_reduce__.offset1; data_memcpy_reduce__.offset1 = i;
              i = data_memcpy_reduce.offset2; data_memcpy_reduce.offset2 = data_memcpy_reduce__.offset2; data_memcpy_reduce__.offset2 = i;
              nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
              i = data_memcpy_reduce.offset1; data_memcpy_reduce.offset1 = data_memcpy_reduce__.offset1; data_memcpy_reduce__.offset1 = i;
              i = data_memcpy_reduce.offset2; data_memcpy_reduce.offset2 = data_memcpy_reduce__.offset2; data_memcpy_reduce__.offset2 = i;
            }
          }
        }
      }
      if (flag) {
        nbuffer_out +=
            ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    } else {
      flag = 0;
    }
  } while (flag3);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return (nbuffer_out);
error:
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
