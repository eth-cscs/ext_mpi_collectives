#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read.h"
#include "optimise_buffers2.h"

int generate_optimise_buffers2(char *buffer_in, char *buffer_out){
  int nbuffer_out=0, nbuffer_in=0, flag, flag2, flag3, o1, o2, size, o1_, size_, partner, num_comm, nline2, i;
  char line[1000], line2[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring2, estring3, estring1_, estring2_, estring1__;
  nbuffer_in+=read_parameters(buffer_in+nbuffer_in, &parameters);
  nbuffer_out+=write_parameters(parameters, buffer_out+nbuffer_out);
  nline2=-1;
  do{
    nbuffer_in+=flag3=read_line(buffer_in+nbuffer_in, line, parameters->ascii_in);
    if (flag3){
      if (read_assembler_line_s(line, &estring1, 0)>=0){
        flag=1;
        if (read_line(buffer_in+nbuffer_in, line2, parameters->ascii_in)){
          if ((estring1==eirecv)||(estring1==eirec_)){
            if (read_assembler_line_ssdddd(line, &estring1_, &estring2_, &o1_, &size_, &partner, &num_comm, 0)>=0){
              flag2=1;
              nline2=nbuffer_in;
              while (flag2&&(i=read_line(buffer_in+nline2, line2, parameters->ascii_in))){
                nline2+=i;
                if (read_assembler_line_s(line2, &estring1__, 0)>=0){
                  if (estring1__==ewaitall){
                    flag2=2;
                  }
                  if ((estring1__==ereduce)||(estring1__==ereduc_)||(estring1__==ereturn)){
                    flag2=0;
                    nline2=-1;
                  }else{
                    if (((estring1__==ememcpy)||(estring1__==ememcp_))&&(flag2==2)){
                      if (read_assembler_line_ssdsdd(line2, &estring1, &estring2, &o1, &estring3, &o2, &size, 0)>=0){
                        if ((o2==o1_)&&(size==size_)){
                          nbuffer_out+=write_assembler_line_ssdddd(buffer_out+nbuffer_out, estring1_, estring2_, o1, size_, partner, num_comm, parameters->ascii_out);
                          flag=0;
                        }
                        flag2=0;
                      }
                    }
                  }
                }
              }
            }
          }
        }
        if (flag&&(nbuffer_in!=nline2)){
          nbuffer_out+=write_line(buffer_out+nbuffer_out, line, parameters->ascii_out);
        }
      }
    }
  }while(flag3);
  nbuffer_out+=write_eof(buffer_out+nbuffer_out, parameters->ascii_out);
  return(nbuffer_out);
}
