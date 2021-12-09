#include "waitany.h"
#include "constants.h"
#include "read.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_waitany(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, i, flag, nwait, o1, nattached=0;
  char line[1000];
  enum eassembler_type estring1, estring2;
  struct parameters_block *parameters;
  nbuffer_in += i = read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  if (parameters->node_row_size*parameters->node_column_size != 1){
    printf("ext_mpi_generate_waitany only for 1 task per node\n");
    exit(2);
  }
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      if (read_assembler_line_s(line, &estring1, 0) >= 0) {
        if (estring1 == ewaitall) {
          read_assembler_line_sdsd(line, &estring1, &nwait, &estring2, &o1, 0);
          nbuffer_out += write_assembler_line_sddsd(
                                  buffer_out + nbuffer_out, ewaitany, nwait, 1, estring2, o1,
                                  parameters->ascii_out);
          nattached=0;
        }else if (estring1 == ememcpy) {
          for (i=nattached; i<nwait; i++){
            nbuffer_out += write_assembler_line_s(
                                buffer_out + nbuffer_out, eattached,
                                parameters->ascii_out);
          }
          nattached=nwait;
          nbuffer_out += write_line(buffer_out + nbuffer_out, line,
                                    parameters->ascii_out);
        }else if (estring1 != ereduce) {
          nbuffer_out += write_line(buffer_out + nbuffer_out, line,
                                    parameters->ascii_out);
        }else{
          nbuffer_out += write_line(buffer_out + nbuffer_out, line,
                                    parameters->ascii_out);
          nbuffer_out += write_assembler_line_s(
                                  buffer_out + nbuffer_out, eattached,
                                  parameters->ascii_out);
          nattached++;
          if (nattached == nwait/2){
            for (i=nattached; i<nwait; i++){
              nbuffer_out += write_assembler_line_s(
                                  buffer_out + nbuffer_out, eattached,
                                  parameters->ascii_out);
            }
            nattached=nwait;
          }
        }
      }
    }
  } while (flag);
  write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
