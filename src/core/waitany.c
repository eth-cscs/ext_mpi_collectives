#include "waitany.h"
#include "constants.h"
#include "read.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_waitany(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, i, flag, nwait=0, o1, nattached=0, flag2=0, o1__=-1, size__, partner, num_comm, nreduce, flag3, o1_, o2_, size_;
  char line[1000];
  enum eassembler_type estring1, estring2, estring1_, estring2_, estring3_;
  struct parameters_block *parameters;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  if (parameters->node_row_size*parameters->node_column_size != 1){
    printf("ext_mpi_generate_waitany only for 1 task per node\n");
    exit(2);
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      if (ext_mpi_read_assembler_line_s(line, &estring1, 0) >= 0) {
        if ((estring1 == ewaitall)&&(nattached == nwait)) {
          ext_mpi_read_assembler_line_sdsd(line, &estring1, &nwait, &estring2, &o1, 0);
          nreduce = 1;
          flag3 = 0;
          do{
            flag3+=ext_mpi_read_line(buffer_in + nbuffer_in + flag + flag3, line, parameters->ascii_in);
            ext_mpi_read_assembler_line_s(line, &estring1_, 0);
            if (estring1_ == ereturn){
              flag3 = 0;
            } else if (estring1_ == ereduce){
              ext_mpi_read_assembler_line_ssdsdd(line, &estring1_, &estring2_, &o1_, &estring3_, &o2_, &size_, 0);
              flag3 = 0;
              if (o1__==o1_){
                nreduce = -1;
              }
            }
          } while (flag3);
          nbuffer_out += ext_mpi_write_assembler_line_sddsd(
                                  buffer_out + nbuffer_out, ewaitany, nwait, nreduce, estring2, o1,
                                  parameters->ascii_out);
          nattached=0;
	  flag2=1;
        }else if (flag2 && ((estring1 == ememcpy)||(estring1 == ereturn)||(estring1 == eirecv)||(estring1 == eisend)||(estring1 == ewaitall))) {
          for (i=nattached; i<nwait; i++){
            nbuffer_out += ext_mpi_write_assembler_line_s(
                                buffer_out + nbuffer_out, eattached,
                                parameters->ascii_out);
          }
          nattached=nwait;
	    flag2=0;
          nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                    parameters->ascii_out);
        }else if ((estring1 != ereduce) || (!flag2)) {
          nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                    parameters->ascii_out);
          if (estring1 == eisend){
            ext_mpi_read_assembler_line_ssdddd(line, &estring1, &estring2, &o1__, &size__, &partner, &num_comm, 0);
          }
        }else if (estring1 == ereduce){
          nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                    parameters->ascii_out);
          nbuffer_out += ext_mpi_write_assembler_line_s(
                                  buffer_out + nbuffer_out, eattached,
                                  parameters->ascii_out);
          nattached++;
          if (nattached==nwait){
	    flag2=0;
          }
        }
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
