#include "optimise_buffers2.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_optimise_buffers2(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce, data_memcpy_reduce_ref;
  struct line_irecv_isend data_irecv_isend, data_irecv_isend_ref;
  int nbuffer_out = 0, nbuffer_in = 0, nbuffer_out_start, flag, flag2, flag3,
      nline2, round = 0, flag_loop, i, j, flag4;
  char line[1000], line2[1000], *buffer_in_loop, *buffer_out_loop, *buffer_temp,
      *buffer_in_start, *buffer_out_start;
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring1__;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_out_start = nbuffer_out;
  buffer_in_start = buffer_in + nbuffer_in;
  buffer_out_start = buffer_out + nbuffer_out;
  buffer_temp = (char *)malloc(MAX_BUFFER_SIZE);
  if (buffer_temp == NULL)
    goto error;
  do {
    flag_loop = 0;
    if (round == 0) {
      buffer_in_loop = buffer_in_start;
      buffer_out_loop = buffer_out_start;
    } else if (round % 2) {
      buffer_in_loop = buffer_out_start;
      buffer_out_loop = buffer_temp;
    } else {
      buffer_in_loop = buffer_temp;
      buffer_out_loop = buffer_out_start;
    }
    nbuffer_in = nbuffer_out = 0;
    memset(&data_irecv_isend, 0, sizeof(struct line_irecv_isend));
    memset(&data_irecv_isend_ref, 0, sizeof(struct line_irecv_isend));
    memset(&data_memcpy_reduce, 0, sizeof(struct line_memcpy_reduce));
    memset(&data_memcpy_reduce_ref, 0, sizeof(struct line_memcpy_reduce));
    flag4 = 1;
    do {
      nbuffer_in += flag3 =
          ext_mpi_read_line(buffer_in_loop + nbuffer_in, line, parameters->ascii_in);
      if (flag3) {
        flag = flag2 = 1;
        if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
          if (flag4 && ((estring1 == eirecv) || (estring1 == eirec_))) {
              if (ext_mpi_read_irecv_isend(line, &data_irecv_isend) >= 0) {
                flag2 = 1;
                nline2 = nbuffer_in;
		data_memcpy_reduce.buffer_type1 = enop;
                while (flag4 && flag2 && (i = ext_mpi_read_line(buffer_in_loop + nline2, line2, parameters->ascii_in))) {
                  nline2 += i;
                  if (ext_mpi_read_assembler_line(line2, 0, "s", &estring1__) >= 0) {
                    if (estring1__ == ewaitall) {
                      flag2 = 2;
                    } else if ((estring1__ == ereduce) || (estring1__ == ereduc_) ||
                        (estring1__ == ereturn)) {
                      flag2 = 0;
                    } else if (data_memcpy_reduce.buffer_type1 == erecvbufp){
                      flag2 = 0;
                    } else {
                      if (((estring1__ == ememcpy) || (estring1__ == ememcp_)) && (flag2 == 2)) {
                        j = ext_mpi_read_memcpy_reduce(line2, &data_memcpy_reduce);
                        if ((j >= 0) && (data_irecv_isend.buffer_number == data_memcpy_reduce.buffer_number2) && (data_irecv_isend.offset == data_memcpy_reduce.offset2) &&
                            (data_irecv_isend.size == data_memcpy_reduce.size) && (data_memcpy_reduce.buffer_type1 == eshmemo) && (data_memcpy_reduce.buffer_type2 == eshmemo) &&
                            (data_irecv_isend.offset_number == data_memcpy_reduce.offset_number2)) {
                          data_irecv_isend.buffer_number = data_memcpy_reduce.buffer_number1;
                          data_irecv_isend.offset_number = data_memcpy_reduce.offset_number1;
                          data_irecv_isend.offset = data_memcpy_reduce.offset1;
                          nbuffer_out += ext_mpi_write_irecv_isend(buffer_out_loop + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
                          flag = flag2 = flag4 = 0;
			  flag_loop = 1;
			  memcpy(&data_irecv_isend_ref, &data_irecv_isend, sizeof(struct line_irecv_isend));
			  memcpy(&data_memcpy_reduce_ref, &data_memcpy_reduce, sizeof(struct line_memcpy_reduce));
                        }
                      }
                    }
                  }
		}
              }
          }
        }
	if (flag4) {
          nbuffer_out += ext_mpi_write_line(buffer_out_loop + nbuffer_out, line, parameters->ascii_out);
	} else {
       	  if (estring1 == ememcpy || estring1 == ememcp_) {
	    ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce);
	    if (data_memcpy_reduce.buffer_type1 != data_irecv_isend_ref.buffer_type || data_memcpy_reduce.buffer_number1 != data_irecv_isend_ref.buffer_number ||
                data_memcpy_reduce.offset_number1 != data_irecv_isend_ref.offset_number || data_memcpy_reduce.offset1 != data_irecv_isend_ref.offset ||
		data_memcpy_reduce.size != data_irecv_isend_ref.size) {
	      nbuffer_out += ext_mpi_write_line(buffer_out_loop + nbuffer_out, line, parameters->ascii_out);
	    } else {
	      flag4 = 1;
	    }
	  } else if ((estring1 == eirecv) || (estring1 == eirec_)){
	    ext_mpi_read_irecv_isend(line, &data_irecv_isend);
	    if (data_irecv_isend.buffer_number != data_memcpy_reduce_ref.buffer_number2 || data_irecv_isend.offset != data_memcpy_reduce_ref.offset2 ||
	        data_irecv_isend.size != data_memcpy_reduce_ref.size || data_irecv_isend.buffer_type != eshmemo || data_irecv_isend.offset_number != data_memcpy_reduce_ref.offset_number2) {
              nbuffer_out += ext_mpi_write_line(buffer_out_loop + nbuffer_out, line, parameters->ascii_out);
	    }
	  } else {
            nbuffer_out += ext_mpi_write_line(buffer_out_loop + nbuffer_out, line, parameters->ascii_out);
          }
        }
      }
    } while (flag3);
    nbuffer_out +=
        ext_mpi_write_eof(buffer_out_loop + nbuffer_out, parameters->ascii_out);
    round++;
  } while (flag_loop);
  if (!(round % 2)) {
    buffer_in_loop = buffer_temp;
    buffer_out_loop = buffer_out_start;
    nbuffer_in = nbuffer_out = 0;
    do {
      nbuffer_in += flag =
          ext_mpi_read_line(buffer_in_loop + nbuffer_in, line, parameters->ascii_in);
      if (flag) {
        nbuffer_out += ext_mpi_write_line(buffer_out_loop + nbuffer_out, line,
                                          parameters->ascii_out);
      }
    } while (flag);
    nbuffer_out +=
        ext_mpi_write_eof(buffer_out_loop + nbuffer_out, parameters->ascii_out);
  }
  free(buffer_temp);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out + nbuffer_out_start;
error:
  free(buffer_temp);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
