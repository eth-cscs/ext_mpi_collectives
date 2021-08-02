#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read.h"
#include "reduce_copyout.h"

static int overlapp(int dest_start, int dest_end, int source_start, int source_end, int *add){
  int start, end;
  start=dest_start;
  if (source_start>start) start=source_start;
  end=dest_end;
  if (source_end<end) end=source_end;
  *add=start;
  if (end-start>0){
    return(end-start);
  }else{
    return(0);
  }
}

int generate_reduce_copyout(char *buffer_in, char *buffer_out){
  int num_nodes=1, size, add, add2, node_rank, node_row_size=1, node_column_size=1, node_size, *counts=NULL, counts_max=0, *iocounts=NULL, iocounts_max=0, *displs=NULL, *iodispls=NULL, *lcounts=NULL, *ldispls=NULL, lrank_column;
  int nbuffer_out=0, nbuffer_in=0, *mcounts=NULL, *moffsets=NULL, i, j, k, allreduce=1, flag;
  int size_level0=0, *size_level1=NULL, type_size=1;
  struct data_line **data=NULL;
  struct parameters_block *parameters;
  char cline[1000];
  nbuffer_in+=i=read_parameters(buffer_in+nbuffer_in, &parameters);
  if (i<0) goto error;
  nbuffer_out+=write_parameters(parameters, buffer_out+nbuffer_out);
  num_nodes=parameters->num_nodes;
  node_rank=parameters->node_rank;
//FIXME
//  node_row_size=parameters->node_row_size;
//  node_column_size=parameters->node_column_size;
  if (parameters->collective_type==collective_type_reduce_scatter){
    allreduce=0;
  }
  mcounts=parameters->message_sizes;
  counts_max=parameters->counts_max;
  counts=parameters->counts;
  iocounts_max=parameters->iocounts_max;
  iocounts=parameters->iocounts;
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
  moffsets = (int*) malloc((num_nodes+1)*sizeof(int));
  if (!moffsets) goto error;
  node_size=node_row_size*node_column_size;
  nbuffer_in+=i=read_algorithm(buffer_in+nbuffer_in, &size_level0, &size_level1, &data, parameters->ascii_in);
  if (i==ERROR_MALLOC) goto error;
  if (i<=0){
    printf("error reading algorithm reduce_copyout\n");
    exit(2);
  }
  do{
    nbuffer_in+=flag=read_line(buffer_in+nbuffer_in, cline, parameters->ascii_in);
    if (flag>0){
      nbuffer_out+=write_line(buffer_out+nbuffer_out, cline, parameters->ascii_out);
    }
  }while(flag);
  displs=(int*)malloc((counts_max+1)*sizeof(int));
  if (!displs) goto error;
  displs[0]=0;
  for (i=0; i<counts_max; i++){
    displs[i+1]=displs[i]+counts[i];
  }
  iodispls=(int*)malloc((iocounts_max+1)*sizeof(int));
  if (!iodispls) goto error;
  iodispls[0]=0;
  for (i=0; i<iocounts_max; i++){
    iodispls[i+1]=iodispls[i]+iocounts[i];
  }
  lrank_column=node_rank%counts_max;
  lcounts=(int*)malloc(sizeof(int)*(node_size/counts_max));
  if (!lcounts) goto error;
  ldispls=(int*)malloc(sizeof(int)*(node_size/counts_max+1));
  for (i=0; i<node_size/counts_max; i++){
    lcounts[i]=(counts[lrank_column]/type_size)/(node_size/counts_max);
    if (lrank_column<(counts[lrank_column]/type_size)%(node_size/counts_max)){
      lcounts[i]++;
    }
    lcounts[i]*=type_size;
  }
  ldispls[0]=0;
  for (i=0; i<node_size/counts_max; i++){
    ldispls[i+1]=ldispls[i]+lcounts[i];
  }
  moffsets[0]=0;
  for (i=0; i<num_nodes; i++){
    moffsets[i+1]=moffsets[i]+mcounts[i];
  }
  nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
  if (allreduce){
    if ((parameters->root==-1)||((parameters->root<0)&&(-10-parameters->root!=parameters->node*parameters->node_row_size+parameters->node_rank%parameters->node_row_size))||(parameters->root==parameters->node*parameters->node_row_size+parameters->node_rank%parameters->node_row_size)){
      add2=0;
      for (i=0; i<size_level1[size_level0-1]; i++){
        k=0;
        for (j=0; j<data[size_level0-1][i].to_max; j++){
          if (data[size_level0-1][i].to[j]==-1){
            k=1;
          }
        }
        if (k){
          if ((size=overlapp(ldispls[lrank_column], ldispls[lrank_column+1], moffsets[data[size_level0-1][i].frac], moffsets[data[size_level0-1][i].frac+1], &add))){
            nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ememcpy, erecvbufp, add, eshmemp, add2, size, parameters->ascii_out);
          }
        }
        add2+=mcounts[data[size_level0-1][i].frac];
      }
    }
  }else{
    add=0;
    add2=iodispls[node_rank];
    size=iocounts[node_rank];
    if (size){
      nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ememcpy, erecvbufp, add, eshmemp, add2, size, parameters->ascii_out);
    }
  }
  nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, ereturn, parameters->ascii_out);
  nbuffer_out+=write_eof(buffer_out+nbuffer_out, parameters->ascii_out);
  delete_algorithm(size_level0, size_level1, data);
  free(ldispls);
  free(lcounts);
  free(iodispls);
  free(displs);
  free(moffsets);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  delete_algorithm(size_level0, size_level1, data);
  free(ldispls);
  free(lcounts);
  free(iodispls);
  free(displs);
  free(moffsets);
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
