#include "optimise_buffers.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_optimise_buffers(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce, data_memcpy_reduce_;
  struct line_irecv_isend data_irecv_isend;
  int nbuffer_out = 0, nbuffer_in = 0, flag, flag2, flag3, line2_new, nline2, i;
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
      flag = 1;
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if ((estring1 == ememcpy) || (estring1 == ememcp_)) {
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
            line2_new = ((nline2 = ext_mpi_read_line(buffer_in + nbuffer_in, line2,
						     parameters->ascii_in)) > 0);
            flag2 = 1;
            while (line2_new && flag2) {
              ext_mpi_read_memcpy_reduce(line2, &data_memcpy_reduce_);
              if (((data_memcpy_reduce_.type == ememcpy) || (data_memcpy_reduce_.type == ememcp_)) && (data_memcpy_reduce_.buffer_type1 == eshmemo)) {
                if ((data_memcpy_reduce.offset1 + data_memcpy_reduce.size == data_memcpy_reduce_.offset1) && (data_memcpy_reduce.offset2 + data_memcpy_reduce.size == data_memcpy_reduce_.offset2) &&
                    (data_memcpy_reduce_.buffer_type1 == eshmemo) && (data_memcpy_reduce_.buffer_type2 == eshmemo)) {
                  data_memcpy_reduce.size += data_memcpy_reduce_.size;
                  i = ext_mpi_read_line(buffer_in + nbuffer_in + nline2, line2,
                                        parameters->ascii_in);
                  line2_new = (i > 0);
                  nline2 += i;
                } else {
                  flag2 = 0;
                }
              } else {
                if (ext_mpi_read_assembler_line(line2, 0, "s", &estring1) >= 0) {
                  if (estring1 == esocket_barrier) {
                    i = ext_mpi_read_line(buffer_in + nbuffer_in + nline2, line2,
                                          parameters->ascii_in);
                    line2_new = (i > 0);
                    nline2 += i;
                    if (line2_new) {
                      if (ext_mpi_read_irecv_isend(line2, &data_irecv_isend) >= 0) {
		        if (((data_irecv_isend.type == eisend) || (data_irecv_isend.type == eisen_)) && (data_irecv_isend.buffer_type == eshmemo)){
                          if ((data_memcpy_reduce.offset1 == data_irecv_isend.offset) && (data_memcpy_reduce.offset_number1 == data_irecv_isend.offset_number) && (data_memcpy_reduce.size == data_irecv_isend.size)) {
                            data_irecv_isend.offset = data_memcpy_reduce.offset2;
                            data_irecv_isend.offset_number = data_memcpy_reduce.offset_number2;
                            nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
                            nbuffer_in += nline2;
                            flag = 0;
                          }
                        }
                      }
                    }
                  }
                }
                flag2 = 0;
              }
            }
          }
        }
      }
      if (flag) {
        nbuffer_out +=
            ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    }
  } while (flag3);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
}
