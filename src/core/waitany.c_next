#include "waitany.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_waitany(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, flag, flag2, flag3, o1, o2,bo1,bo2 ,size, o1_,o2_,
      size_, partner, num_comm, nline2, i,waitanyline,num_handles;
  char line[1000], line2[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring2, estring3,estring4,estring5, estring1_, estring2_,estring3_,estring1__;
   int size_level0 = 0, *size_level1 = NULL;
  struct data_line **data = NULL;
  nbuffer_in += read_parameters(buffer_in + nbuffer_in, &parameters);
  nline2 = -1;
  int *nlines;
  int irecvs=(parameters->num_nodes)-1;
  int handle=0;
  int count=0;
  //do while line reads in something
  int max_reduce=0;
  int current_reduce=0;
  int nbuffer_start=nbuffer_in;
  nlines=(int*)malloc((parameters->num_nodes-1)*sizeof(int));
  do {
    //read line in ascii characters
    nbuffer_in += flag3 =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      //read first string of line
      if (read_assembler_line_s(line, &estring1, 0) >= 0) {
         if ((estring1 == eirecv) || (estring1 == eirec_)) {
            if (read_assembler_line_ssdsdddd(line, &estring1_, &estring2_, &o1_,&estring3_,&o2_,
                                           &size_, &partner, &num_comm,
                                           0) >= 0) {
            irecvs=irecvs-1;
            nlines[irecvs]=o2_;
            //nbuffer_out += sprintf(buffer_out + nbuffer_out, "put this in list:%d\n",o2_);
          }
          }
            if (estring1 == ewaitall) {
  	    if (read_assembler_line_sd(line, &estring1_,&num_handles,
                                           0) >= 0) {
 	   }
            waitanyline=nbuffer_out;
         }                    
          if (estring1 != ewaitall) {
           //nbuffer_out += sprintf(buffer_out + nbuffer_out, "not waitall or ereduce:%d\n",count);
           
        if ((estring1==ereduce) || (estring1==ereduc_)){
          //nbuffer_out += sprintf(buffer_out + nbuffer_out, "ereduce case:%d\n",count);
          if (read_assembler_line_ssdsdsdsdd(line, &estring1, &estring2, &bo1,
                                           &estring3, &o1, &estring4, &bo2,
                                           &estring5, &o2, &size, 0)>=0){
           //buffer_out += sprintf(buffer_out + nbuffer_out, "in list:%d,buffer: %d,index:%d\n",nlines[(parameters->num_nodes)-handle],o1,(parameters->num_nodes)-handle-1);
           current_reduce+=1;
           if(nlines[handle]==o1){
           if (current_reduce>max_reduce){
           max_reduce=current_reduce; 
           }
           current_reduce=0;
           //buffer_out += sprintf(buffer_out + nbuffer_out, "if statement match, in list:%d,buffer: %d\n",nlines[(parameters->num_nodes)-handle],bo2);
           //nbuffer_out +=write_assembler_line_sd(buffer_out + nbuffer_out,ewaitany,handle,
           //                   parameters->ascii_out);
           //buffer_out += sprintf(buffer_out + nbuffer_out, "ATTACHED: %d\n",handle);
           handle=handle+1;   
           }
           }  
        }  
        else{
        }     
        if (read_line(buffer_in + nbuffer_in, line, parameters->ascii_in)==0){
        if (current_reduce>max_reduce){
           max_reduce=current_reduce;
           current_reduce=0;
        }  
        }                     
        }                     
       count=count+1;
      }
    } 
  } while (flag3);
  nbuffer_in=nbuffer_start;
  irecvs=(parameters->num_nodes)-1;
  handle=0;
  count=0;
  parameters->locmem_max=max_reduce*num_handles*(sizeof(int)+2*sizeof(char*))*max_reduce;
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_out +=      
      write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                      parameters->ascii_out);
  do {
    //read line in ascii characters
    nbuffer_in += flag3 =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      //read first string of line
      if (read_assembler_line_s(line, &estring1, 0) >= 0) {
	 if ((estring1 == eirecv) || (estring1 == eirec_)) {
            if (read_assembler_line_ssdsdddd(line, &estring1_, &estring2_, &o1_,&estring3_,&o2_,
                                           &size_, &partner, &num_comm,
                                           0) >= 0) {
            irecvs=irecvs-1;
	    nlines[irecvs]=o2_;
            //nbuffer_out += sprintf(buffer_out + nbuffer_out, "put this in list:%d\n",o2_);
	  }
          }
          if (estring1 == ewaitall) {
            if (read_assembler_line_sdsd(line, &estring1_, &o1_,&estring2_,&o2_,
                                           0) >= 0) {
            //nbuffer_out += sprintf(buffer_out + nbuffer_out, "waitall case:%d\n",count);
            waitanyline=nbuffer_out;
            nbuffer_out +=write_assembler_line_ssddd(buffer_out + nbuffer_out,ewaitany,estring2_,o2_,o1_,max_reduce,
                              parameters->ascii_out);
         }
         }
          if (estring1 != ewaitall) {
	   //nbuffer_out += sprintf(buffer_out + nbuffer_out, "not waitall or ereduce:%d\n",count);
        
        if ((estring1==ereduce) || (estring1==ereduc_)){
          //nbuffer_out += sprintf(buffer_out + nbuffer_out, "ereduce case:%d\n",count);
       	  if (read_assembler_line_ssdsdsdsdd(line, &estring1, &estring2, &bo1,
                                           &estring3, &o1, &estring4, &bo2,
                                           &estring5, &o2, &size, 0)>=0){
           //buffer_out += sprintf(buffer_out + nbuffer_out, "in list:%d,buffer: %d,index:%d\n",nlines[(parameters->num_nodes)-handle],o1,(parameters->num_nodes)-handle-1);
           //current_reduce+=1;
           if(nlines[handle]==o1){
           //if (current_reduce>max_reduce){
           //max_reduce=current_reduce; 
           //}
           //current_reduce=0;
           //buffer_out += sprintf(buffer_out + nbuffer_out, "if statement match, in list:%d,buffer: %d\n",nlines[(parameters->num_nodes)-handle],bo2);
           //nbuffer_out +=write_assembler_line_sd(buffer_out + nbuffer_out,ewaitany,handle,
           //                   parameters->ascii_out);
           //buffer_out += sprintf(buffer_out + nbuffer_out, "ATTACHED: %d\n",handle);
	   nbuffer_out +=write_assembler_line_s(buffer_out + nbuffer_out,eattached,
                              parameters->ascii_out);
	   handle=handle+1;
           }
           nbuffer_out +=
              write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
           }
        }
        else{
        nbuffer_out +=
              write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
        }
        if (read_line(buffer_in + nbuffer_in, line, parameters->ascii_in)==0){
        //if (current_reduce>max_reduce){
        //   max_reduce=current_reduce;
        //   current_reduce=0;
        //}
	nbuffer_out +=write_assembler_line_s(buffer_out + nbuffer_out,eattached,
                              parameters->ascii_out);
        }
        }
       count=count+1;
      }
    }
  } while (flag3);
  //write_assembler_line_sd(buffer_out + waitanyline,ewaitany,max_reduce,parameters->ascii_out);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(nlines);
  return (nbuffer_out);
}
