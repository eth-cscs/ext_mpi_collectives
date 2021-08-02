#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read.h"
#include "forward_interpreter.h"

int generate_forward_interpreter(char *buffer_in, char *buffer_out, MPI_Comm comm_row){
  int **values=NULL, **recv_values=NULL, max_lines;
  int nbuffer_out=0, nbuffer_in=0, i, j, k, l, size_level0=0, *size_level1=NULL;
  struct data_line **data=NULL;
  struct parameters_block *parameters;
  MPI_Request *request=NULL;
  nbuffer_in+=i=read_parameters(buffer_in+nbuffer_in, &parameters);
  MPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i<0) goto error;
  nbuffer_out+=write_parameters(parameters, buffer_out+nbuffer_out);
  i=read_algorithm(buffer_in+nbuffer_in, &size_level0, &size_level1, &data, parameters->ascii_in);
  MPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i==ERROR_MALLOC) goto error;
  if (i<=0){
    printf("error reading algorithm forward_interpreter\n");
    exit(2);
  }
  values=(int**)malloc(sizeof(int*)*size_level0);
  i=0;
  if (!values) i=ERROR_MALLOC;
  MPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i<0) goto error;
  for (i=0; i<size_level0; i++){
    values[i]=NULL;
  }
  j=0;
  for (i=0; i<size_level0; i++){
    values[i]=(int*)malloc(sizeof(int)*size_level1[i]);
    if (!values[i]) j=ERROR_MALLOC;
  }
  MPI_Allreduce(MPI_IN_PLACE, &j, 1, MPI_INT, MPI_MIN, comm_row);
  if (j<0) goto error;
  for (i=0; i<size_level0; i++){
    for (j=0; j<size_level1[i]; j++){
      values[i][j]=0;
    }
  }
  for (i=0; i<size_level1[0]; i++){
    for (j=0; j<data[0][i].from_max; j++){
      k=parameters->node==parameters->root/parameters->node_row_size;
      if (parameters->root<=-10){
        k=parameters->node==(-10-parameters->root)/parameters->node_row_size;
      }
      if ((data[0][i].from_node[j]==-1)&&k){
        values[0][i]=1;
      }
    }
  }
  max_lines=0;
  for (i=0; i<size_level0; i++){
    if (size_level1[i]>max_lines){
      max_lines=size_level1[i];
    }
  }
  request=(MPI_Request*)malloc(sizeof(MPI_Request)*max_lines*2);
  i=0;
  if (!request) i=ERROR_MALLOC;
  MPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i<0) goto error;
  recv_values=(int**)malloc(sizeof(int*)*max_lines);
  i=0;
  if (!recv_values) i=ERROR_MALLOC;
  MPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i<0) goto error;
  for (i=0; i<max_lines; i++){
    recv_values[i]=NULL;
  }
  j=0;
  for (i=0; i<max_lines; i++){
    recv_values[i]=(int*)malloc(sizeof(int)*parameters->num_nodes);
    if (!recv_values[i]) j=ERROR_MALLOC;
  }
  MPI_Allreduce(MPI_IN_PLACE, &j, 1, MPI_INT, MPI_MIN, comm_row);
  if (j<0) goto error;
  for (i=1; i<size_level0; i++){
    l=0;
    for (j=0; j<size_level1[i]; j++){
      for (k=0; k<data[i][j].from_max; k++){
        if (data[i][j].from_node[k]==parameters->node){
          if ((data[i][j].from_line[k]<j)&&(data[i][j].from_line[k]>=0)){
            if (values[i][j]){
              values[i][data[i][j].from_line[k]]=1;
            }
          }
        }else{
          MPI_Irecv(&recv_values[j][data[i][j].from_node[k]], 1, MPI_INT, data[i][j].from_node[k]*parameters->node_row_size+parameters->node_rank%parameters->node_row_size, 0, comm_row, &request[l++]);
        }
      }
    }
    for (j=0; j<size_level1[i-1]; j++){
      for (k=0; k<data[i-1][j].to_max; k++){
        if (data[i-1][j].to[k]!=parameters->node){
          MPI_Isend(&values[i-1][j], 1, MPI_INT, data[i-1][j].to[k]*parameters->node_row_size+parameters->node_rank%parameters->node_row_size, 0, comm_row, &request[l++]);
        }
      }
    }
    for (j=0; (j<size_level1[i-1])&&(j<size_level1[i]); j++){
      if (values[i-1][j]){
        values[i][j]=1;
      }
    }
    MPI_Waitall(l, request, MPI_STATUSES_IGNORE);
    for (j=0; j<size_level1[i-1]; j++){
      for (k=0; k<data[i-1][j].to_max; k++){
        if (data[i-1][j].to[k]!=parameters->node){
          if (!values[i-1][j]){
            if (parameters->node_row_size*parameters->node_column_size==1){
              if (data[i-1][j].to_max==1){
                data[i-1][j].to[k]=parameters->node;
              }else{
                for (l=k; l<data[i-1][j].to_max-1; l++){
                  data[i-1][j].to[l]=data[i-1][j].to[l+1];
                }
                data[i-1][j].to_max--; k--;
              }
            }else{
              data[i-1][j].to[k]=-10-data[i-1][j].to[k];
            }
          }
        }
      }
    }
    for (j=0; j<size_level1[i]; j++){
      for (k=0; k<data[i][j].from_max; k++){
        if (data[i][j].from_node[k]!=parameters->node){
          if (!recv_values[j][data[i][j].from_node[k]]){
            if (parameters->node_row_size*parameters->node_column_size==1){
              if (data[i][j].from_max==1){
                data[i][j].from_node[k]=parameters->node;
              }else{
                for (l=k; l<data[i][j].from_max-1; l++){
                  data[i][j].from_node[l]=data[i][j].from_node[l+1];
                  data[i][j].from_line[l]=data[i][j].from_line[l+1];
                }
                data[i][j].from_max--; k--;
              }
            }else{
              data[i][j].from_node[k]=-10-data[i][j].from_node[k];
            }
          }else{
            values[i][j]=1;
          }
        }
      }
    }
    for (j=0; j<size_level1[i]; j++){
      for (k=0; k<data[i][j].from_max; k++){
        if (data[i][j].from_node[k]==parameters->node){
          if ((data[i][j].from_line[k]<j)&&(values[i][data[i][j].from_line[k]])){
            data[i][j].from_line[k]=-10-data[i][j].from_line[k];
            values[i][j]=1;
          }else{
            if (parameters->node_row_size*parameters->node_column_size==1){
              if (data[i][j].from_max==1){
                data[i][j].from_node[k]=parameters->node;
              }else{
                for (l=k; l<data[i][j].from_max-1; l++){
                  data[i][j].from_node[l]=data[i][j].from_node[l+1];
                  data[i][j].from_line[l]=data[i][j].from_line[l+1];
                }
                data[i][j].from_max--; k--;
              }
            }else{
              data[i][j].from_node[k]=-10-data[i][j].from_node[k];
            }
          }
        }
      }
    }
  }
  nbuffer_out+=write_algorithm(size_level0, size_level1, data, buffer_out+nbuffer_out, parameters->ascii_out);
  nbuffer_out+=write_eof(buffer_out+nbuffer_out, parameters->ascii_out);
  free(request);
  for (i=max_lines-1; i>=0; i--){
    free(recv_values[i]);
  }
  free(recv_values);
  for (i=size_level0-1; i>=0; i--){
    free(values[i]);
  }
  free(values);
  delete_algorithm(size_level0, size_level1, data);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  free(request);
  if (recv_values){
    for (i=max_lines-1; i>=0; i--){
      free(recv_values[i]);
    }
  }
  free(recv_values);
  if (values){
    for (i=size_level0-1; i>=0; i--){
      free(values[i]);
    }
  }
  free(values);
  delete_algorithm(size_level0, size_level1, data);
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
