#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read.h"
#include "parallel_memcpy.h"

int generate_parallel_memcpy(char *buffer_in, char *buffer_out){
  int size, add, flag, flag3, node_rank, node_row_size=1, node_column_size=1, node_size;
  int nbuffer_out=0, nbuffer_in=0, i, os1=-1, os2=-1, reset=1, size_old=-1, os1_old=-1, type_size=1, p1=-1, os1_=-1, os2_=-1, size_=-1;
  char line[1000], line2[1000];
  enum eassembler_type estring1, estring2, estring3, estring1_, estring2_, estring3_;
  struct parameters_block *parameters;
  nbuffer_in+=i=read_parameters(buffer_in+nbuffer_in, &parameters);
  if (i<0) goto error;
  nbuffer_out+=write_parameters(parameters, buffer_out+nbuffer_out);
  node_row_size=parameters->node_row_size;
  node_column_size=parameters->node_column_size;
  node_rank=parameters->node_rank;
  switch (parameters->data_type){
    case data_type_char:
      type_size=sizeof(char);
      break;
    case data_type_int:
      type_size=sizeof(int);
      break;
    case data_type_float:
      type_size=sizeof(float);
      break;
    case data_type_long_int:
      type_size=sizeof(long int);
      break;
    case data_type_double:
      type_size=sizeof(double);
      break;
  }
  node_size=node_row_size*node_column_size;
  do{
    flag3=read_line(buffer_in+nbuffer_in, line, parameters->ascii_in);
    read_assembler_line_ssdsdd(line, &estring1, &estring2, &os1, &estring3, &os2, &size, 0);
    estring1_=estring1;
    estring2_=estring2;
    estring3_=estring3;
    os1_=os1;
    os2_=os2;
    size_=size;
    if ((flag3>0)&&(read_assembler_line_s(line, &estring1, 0)>=0)){
      reset=0;
      flag=0;
      if (estring1==enode_barrier){
        reset=1;
        flag=1;
      }
      if (os1_old!=os1){
        os1_old=os1;
        reset=1;
      }
      if (reset){
        p1=0;
        if (flag){
          p1+=read_line(buffer_in+nbuffer_in+p1, line2, parameters->ascii_in);
        }
        flag=1;
        size_old=0;
        while (flag){
          p1+=flag=read_line(buffer_in+nbuffer_in+p1, line2, parameters->ascii_in);
          read_assembler_line_ssdsdd(line2, &estring1, &estring2, &os1, &estring3, &os2, &size, 0);
          if ((flag>0)&&(read_assembler_line_s(line2, &estring1, 0)>=0)){
            if (estring1==enode_barrier){
              flag=0;
            }
            if (os1_old!=os1){
              flag=0;
            }
            if (flag){
              if ((estring1==ememcpy)||(estring1==ememcp_)||(estring1==ereduce)||(estring1==ereduc_)){
                if ((estring2==eshmemp)&&(estring3==eshmemp)){
                  if (size>size_old){
                    size_old=size;
                  }
                }
              }
            }
          }else{
            flag=0;
          }
        }
        estring1=estring1_;
        estring2=estring2_;
        estring3=estring3_;
        os1=os1_;
        os2=os2_;
        size=size_;
      }
      flag=1;
      if ((estring1==ememcpy)||(estring1==ememcp_)||(estring1==ereduce)||(estring1==ereduc_)){
        if ((estring2==eshmemp)&&(estring3==eshmemp)){
          if (node_rank<node_size){
            add=((size_old/type_size)/node_size)*node_rank;
            i=(size_old/type_size)/node_size;
            if (node_rank<(size_old/type_size)%node_size){
              add+=node_rank;
              i++;
            }else{
              add+=(size_old/type_size)%node_size;
            }
            if (add*type_size>size){
              i=0;
            }else{
              if ((add+i)*type_size>size){
                i=size/type_size-add;
              }
            }
          }else{
            i=add=0;
          }
          size=i*type_size;
          os1+=add*type_size;
          os2+=add*type_size;
          if (estring1==ememcp_){
            estring1=ememcpy;
          }
          if (estring1==ereduc_){
            estring1=ereduce;
          }
          if (size){
            nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, estring1, estring2, os1, estring3, os2, size, parameters->ascii_out);
          }
          flag=0;
        }
      }
      if (flag3&&flag){
        nbuffer_out+=write_line(buffer_out+nbuffer_out, line, parameters->ascii_out);
      }
    }
    buffer_in+=flag3;
  }while(flag3);
  nbuffer_out+=write_eof(buffer_out+nbuffer_out, parameters->ascii_out);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
