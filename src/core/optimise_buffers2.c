#include "optimise_buffers2.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int generate_optimise_buffers2(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, flag, flag2, flag3, o1, o2,bo1,bo2 ,size, o1_,o2_,
      size_, partner, num_comm, nline2, i;
  char line[1000], line2[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring2, estring3,estring4,estring5, estring1_, estring2_,estring3_,estring1__;
  int size_level0 = 0, *size_level1 = NULL;
  struct data_line **data = NULL;
  nbuffer_in += read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
    nbuffer_in += i = read_algorithm(buffer_in + nbuffer_in, &size_level0,
                                   &size_level1, &data, parameters->ascii_in);
  /*
  if (i == ERROR_MALLOC)
    goto error;
  */
  if (i <= 0) {
    printf("error reading algorithm reduce_copyin\n");
    exit(2);
  }
  nbuffer_out +=
      write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                      parameters->ascii_out);
  nline2 = -1;
  int nlines[(parameters->num_nodes)-1];
  int irecvs=(parameters->num_nodes)-1;
  int memcpys=(parameters->num_nodes)-1;
  //do while line reads in something
  do {
    //read line in ascii characters
    nbuffer_in += flag3 =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      //read first string of line
      if (read_assembler_line_s(line, &estring1, 0) >= 0) {
        flag = 1;
        //read line2, the consecutive of line
        if (read_line(buffer_in + nbuffer_in, line2, parameters->ascii_in)) {
          //check if line was an irecv 
          if ((estring1 == eirecv) || (estring1 == eirec_)) {
            //irecvs=irecvs-1;
	    //read first and second string and integers of line
	    /*
            if (read_assembler_line_ssdddd(line, &estring1_, &estring2_, &o1_,
                                           &size_, &partner, &num_comm,
                                           0) >= 0) {
	    */
	    if (read_assembler_line_ssdsdddd(line, &estring1_, &estring2_, &o1_,&estring3_,&o2_,
                                           &size_, &partner, &num_comm,
                                           0) >= 0) {
              flag2 = 1;
              nline2 = nbuffer_in;
	      //nbuffer_out += sprintf(buffer_out + nbuffer_out, "o1_:%d,o2_:%d\n",o1_,o2_);
              //keep looping until line2 is ereduce (or memcpy?)  and progress in lines
              while (flag2 && (i = read_line(buffer_in + nline2, line2,
                                             parameters->ascii_in))) {
                nline2 += i;
                //read first string of line2
                if (read_assembler_line_s(line2, &estring1__, 0) >= 0) {
                  if (estring1__ == ewaitall) {
                    flag2 = 2;
                  }
                  if ((estring1__ == ereduce) || (estring1__ == ereduc_) ||
                      (estring1__ == ereturn)) {
                    flag2 = 0;
                    nline2 = -1;
                  } else {
                    if (((estring1__ == ememcpy) || (estring1__ == ememcp_)) &&
                        (flag2 == 2)) {
			//nbuffer_out += sprintf(buffer_out + nbuffer_out, "memcopy case\n");
                      //if (read_assembler_line_ssdsdd(line2, &estring1,
                      //                               &estring2, &o1, &estring3,
                      //                               &o2, &size, 0) >= 0) {
                        if (read_assembler_line_ssdsdsdsdd(line2, &estring1, &estring2, &bo1,
                                           &estring3, &o1, &estring4, &bo2,
                                           &estring5, &o2, &size, 0)>=0){
 			  //nbuffer_out += sprintf(buffer_out + nbuffer_out, "bo1:%d,o1:%d,bo2:%d,o2:%d,o1_:%d\n",bo1,o1,bo2,o2,o1_);
			  if ((bo2 == o1_) && (size == size_)&& (o2==o2_)) {
                          nbuffer_out += write_assembler_line_ssdsdddd(
                              buffer_out + nbuffer_out, estring1_, estring2_,
                              bo1,estring3_,o1, size_, partner, num_comm,
                              parameters->ascii_out);
		          nbuffer_out += sprintf(buffer_out + nbuffer_out, "rewrited this irecv\n");
			  nlines[irecvs]=nline2;
			  flag = 0;
		          flag2 = 0;
  			  irecvs=irecvs-1;
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        //if line does not need to be substituted, just print it out
        if (flag && (nbuffer_in != nlines[memcpys])) {
          nbuffer_out +=
              write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
        }
        if (nbuffer_in == nlines[memcpys]&&memcpys>0){
        memcpys=memcpys-1;
        }
      }
    }
  } while (flag3);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  delete_algorithm(size_level0,size_level1,data);
  delete_parameters(parameters);
  return (nbuffer_out);
}
