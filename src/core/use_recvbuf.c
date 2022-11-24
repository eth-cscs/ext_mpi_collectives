#include "use_recvbuf.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_use_recvbuf(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend data_irecv_isend;
  int buffer_in_size = 0, buffer_out_size = 0, nbuffer_in = 0, nbuffer_out = 0, nbuffer_in_first, flag, flag3;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_in_first = nbuffer_in;
  do {
    nbuffer_in_first += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in_first, line, parameters->ascii_in);
    if (flag3) {
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if (estring1==ememcpy){
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
            if ((data_memcpy_reduce.buffer_type2==esendbufp)&&(data_memcpy_reduce.offset2+data_memcpy_reduce.size>buffer_in_size)){
              buffer_in_size=data_memcpy_reduce.offset2+data_memcpy_reduce.size;
            }
            if ((data_memcpy_reduce.buffer_type1==erecvbufp)&&(data_memcpy_reduce.offset1+data_memcpy_reduce.size>buffer_out_size)){
              buffer_out_size=data_memcpy_reduce.offset1+data_memcpy_reduce.size;
	    }
          }
        }
      }
    }
  } while (flag3);
  do {
    nbuffer_in += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      flag = 1;
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if ((estring1 == ememcpy) || (estring1 == ememcp_) || (estring1 == ereduce) || (estring1 == ereduc_)) {
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
            if (((data_memcpy_reduce.buffer_type2==esendbufp)&&parameters->in_place)||(data_memcpy_reduce.buffer_type1==erecvbufp)){
              flag = 0;
            }else{
              if ((data_memcpy_reduce.buffer_type1==eshmemo)&&(data_memcpy_reduce.offset1+data_memcpy_reduce.size<=buffer_out_size)){
                data_memcpy_reduce.buffer_type1=erecvbufp;
                flag = 2;
              }
              if ((data_memcpy_reduce.buffer_type2==eshmemo)&&(data_memcpy_reduce.offset2+data_memcpy_reduce.size<=buffer_out_size)){
                data_memcpy_reduce.buffer_type2=erecvbufp;
                flag = 2;
              }
              if (flag == 2){
                nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
                flag = 0;
              }
            }
          }
        }else if ((estring1 == eirecv) || (estring1 == eirec_) || (estring1 == eisend) || (estring1 == eisen_)){
          if (ext_mpi_read_irecv_isend(line, &data_irecv_isend) >= 0) {
            if ((data_irecv_isend.buffer_type==eshmemo)&&(data_irecv_isend.offset+data_irecv_isend.size<=buffer_out_size)){
              data_irecv_isend.buffer_type=erecvbufp;
              nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
              flag = 0;
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
