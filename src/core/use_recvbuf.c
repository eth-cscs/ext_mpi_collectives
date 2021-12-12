#include "use_recvbuf.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_use_recvbuf(char *buffer_in, char *buffer_out) {
  int buffer_in_size = 0, buffer_out_size = 0, nbuffer_in = 0, nbuffer_out = 0, nbuffer_in_first, flag, flag3, o1, o2, size, partner, tag;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring2, estring3;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_in_first = nbuffer_in;
  do {
    nbuffer_in_first += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in_first, line, parameters->ascii_in);
    if (flag3) {
      if (ext_mpi_read_assembler_line_ssdsdd(line, &estring1, &estring2, &o1, &estring3, &o2, &size, 0) >= 0) {
        if (estring1==ememcpy){
          if ((estring3==esendbufp)&&(o2+size>buffer_in_size)){
            buffer_in_size=o2+size;
          }
          if ((estring2==erecvbufp)&&(o1+size>buffer_out_size)){
            buffer_out_size=o1+size;
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
      if (ext_mpi_read_assembler_line_s(line, &estring1, 0) >= 0) {
        if ((estring1 == ememcpy) || (estring1 == ememcp_) || (estring1 == ereduce) || (estring1 == ereduc_)) {
          if (ext_mpi_read_assembler_line_ssdsdd(line, &estring1, &estring2, &o1, &estring3, &o2, &size, 0) >= 0) {
            if (((estring3==esendbufp)&&parameters->in_place)||(estring2==erecvbufp)){
              flag = 0;
            }else{
              if ((estring2==eshmemp)&&(o1+size<=buffer_out_size)){
                estring2=erecvbufp;
                flag = 2;
              }
              if ((estring3==eshmemp)&&(o2+size<=buffer_out_size)){
                estring3=erecvbufp;
                flag = 2;
              }
              if (flag == 2){
                nbuffer_out += ext_mpi_write_assembler_line_ssdsdd(buffer_out + nbuffer_out, estring1, estring2, o1, estring3, o2, size, parameters->ascii_out);
                flag = 0;
              }
            }
          }
        }else if ((estring1 == eirecv) || (estring1 == eirec_) || (estring1 == eisend) || (estring1 == eisen_)){
          if (ext_mpi_read_assembler_line_ssdddd(line, &estring1, &estring2, &o1, &size, &partner, &tag, 0) >= 0) {
            if ((estring2==eshmemp)&&(o1+size<=buffer_out_size)){
              nbuffer_out += ext_mpi_write_assembler_line_ssdddd(buffer_out + nbuffer_out, estring1, erecvbufp, o1, size, partner, tag, parameters->ascii_out);
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
