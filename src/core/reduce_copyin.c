#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read.h"
#include "reduce_copyin.h"

#define RADIX 7

int generate_reduce_copyin(char *buffer_in, char *buffer_out){
  int num_nodes=1, size, add, add2, node_rank, node_row_size=1, node_column_size=1, node_size, *counts=NULL, counts_max=0, *displs=NULL, *iocounts=NULL, iocounts_max=0, *iodispls=NULL, *lcounts=NULL, *ldispls=NULL, lrank_row, lrank_column;
  int nbuffer_out=0, nbuffer_in=0, *mcounts=NULL, *moffsets=NULL, i, j, k, m, copy_method=0;
  int size_level0=0, *size_level1=NULL, collective_type=1;
  int barriers_size, step, substep, add_local, size_local, type_size=1;
  struct data_line **data=NULL;
  struct parameters_block *parameters;
  nbuffer_in+=i=read_parameters(buffer_in+nbuffer_in, &parameters);
  if (i<0) goto error;
  nbuffer_out+=write_parameters(parameters, buffer_out+nbuffer_out);
  num_nodes=parameters->num_nodes;
  node_rank=parameters->node_rank;
  node_row_size=parameters->node_row_size;
  node_column_size=parameters->node_column_size;
  copy_method=parameters->copy_method;
  if (parameters->collective_type==collective_type_allgatherv){
    collective_type=0;
  }
  if (parameters->collective_type==collective_type_reduce_scatter){
    collective_type=2;
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
    printf("error reading algorithm reduce_copyin\n");
    exit(2);
  }
  nbuffer_out+=write_algorithm(size_level0, size_level1, data, buffer_out+nbuffer_out, parameters->ascii_out);
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
  lrank_row=node_rank/counts_max;
  lrank_column=node_rank%counts_max;
  lcounts=(int*)malloc(sizeof(int)*(node_size/counts_max));
  if (!lcounts) goto error;
  ldispls=(int*)malloc(sizeof(int)*(node_size/counts_max+1));
  if (!ldispls) goto error;
  for (i=0; i<node_size/counts_max; i++){
    lcounts[i]=(counts[lrank_column]/type_size)/(node_size/counts_max);
    if (i<(counts[lrank_column]/type_size)%(node_size/counts_max)){
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
  if ((size_level1[0]==1)&&(collective_type==0)){
    for (i=0; i<num_nodes; i++){
      moffsets[i]=moffsets[num_nodes];
    }
  }
//nbuffer_out+=write_assembler_line_ssdsdsdsdd(buffer_out+nbuffer_out, ememcpy, eshmempbuffer_offseto, buffer_counter, eshmempbuffer_offsetcp, add, eshmempbuffer_offseto, 0, eshmempbuffer_offsetcp, add2, size, parameters->as
  nbuffer_out+=write_assembler_line_sd(buffer_out+nbuffer_out, eset_num_cores, node_size, parameters->ascii_out);
  nbuffer_out+=write_assembler_line_sd(buffer_out+nbuffer_out, eset_node_rank, node_rank, parameters->ascii_out);
  nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
  if (collective_type){
    if (((collective_type==2)&&(parameters->root>=0))||(parameters->root<=-10)){
      if ((parameters->node*parameters->node_row_size+parameters->node_rank==parameters->root)||(parameters->node*parameters->node_row_size+parameters->node_rank==-10-parameters->root)){
        add=0;
        for (i=0; i<num_nodes; i++){
          j=(num_nodes+i+parameters->node)%num_nodes;
          nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ememcpy, eshmemp, add, esendbufp, moffsets[j], mcounts[j], parameters->ascii_out);
          add+=mcounts[j];
        }
      }
      nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
      nbuffer_out+=write_eof(buffer_out+nbuffer_out, parameters->ascii_out);
    }else{
    switch (copy_method){
      case 0:
      if (moffsets[num_nodes]<CACHE_LINE_SIZE){
        add=CACHE_LINE_SIZE*lrank_row+ldispls[lrank_column];
      }else{
        add=moffsets[num_nodes]*lrank_row+ldispls[lrank_column];
      }
      for (i=0; i<size_level1[0]; i++){
        size=mcounts[data[0][i].frac];
        add2=moffsets[data[0][i].frac];
        if (size){
          nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ememcpy, eshmemp, add, esendbufp, add2, size, parameters->ascii_out);
        }
        add+=size;
      }
      nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
      size=moffsets[num_nodes];
      add=0;
      if (size<=CACHE_LINE_SIZE){
        for (i=1; i<node_size/counts_max; i++){
          if (moffsets[num_nodes]<CACHE_LINE_SIZE){
            add2=add+CACHE_LINE_SIZE*i;
          }else{
            add2=add+moffsets[num_nodes]*i;
          }
          if (size){
            if (node_rank==0){
              nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, esreduce, eshmemp, add, eshmemp, add2, size, parameters->ascii_out);
            }else{
              nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, esreduc_, eshmemp, add, eshmemp, add2, size, parameters->ascii_out);
            }
          }
        }
//        nbuffer_out+=sprintf(buffer_out+nbuffer_out, " NODE_BARRIER_MASTER\n");
        nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
      }else{
        for (i=1; i<node_size/counts_max; i++){
          if (moffsets[num_nodes]<CACHE_LINE_SIZE){
            add2=add+CACHE_LINE_SIZE*i;
          }else{
            add2=add+moffsets[num_nodes]*i;
          }
          if (size){
            if (node_rank==0){
              nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ereduce, eshmemp, add, eshmemp, add2, size, parameters->ascii_out);
            }else{
              nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ereduc_, eshmemp, add, eshmemp, add2, size, parameters->ascii_out);
            }
          }
        }
        nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
      }
      nbuffer_out+=write_eof(buffer_out+nbuffer_out, parameters->ascii_out);
      break;
      case 1:
//        if (collective_type==2){
//          printf("copyin not implemented\n");
//          exit(2);
//        }
        k=(node_size/counts_max)/size_level1[0];
        if ((k<1)||((node_size/counts_max)%size_level1[0]!=0)){
          add=ldispls[lrank_column];
          for (i=0; i<size_level1[0]; i++){
            size=mcounts[data[0][i].frac];
            add2=moffsets[data[0][i].frac];
            if ((node_size/counts_max+i-lrank_row)%(node_size/counts_max)==0){
              if (size){
                nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ememcpy, eshmemp, add, esendbufp, add2, size, parameters->ascii_out);
              }
            }
            add+=size;
          }
          for (i=1; i<node_size/counts_max; i++){
            nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
            add=ldispls[lrank_column];
            for (j=0; j<size_level1[0]; j++){
              size=mcounts[data[0][j].frac];
              add2=moffsets[data[0][j].frac];
              if ((node_size/counts_max+j-lrank_row)%(node_size/counts_max)==i){
                if (size){
                  nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ereduce, eshmemp, add, esendbufp, add2, size, parameters->ascii_out);
                }
              }
              add+=size;
            }
          }
        }else{
          add=ldispls[lrank_column];
          for (i=0; i<size_level1[0]; i++){
            size=mcounts[data[0][i].frac];
            add2=moffsets[data[0][i].frac];
            for (m=0; m<k; m++){
              size_local=(size/type_size/k)*type_size;
              add_local=size_local*m;
              if (m<(size/type_size)%k){
                size_local+=type_size;
                add_local+=m*type_size;
              }else{
                add_local+=((size/type_size)%k)*type_size;
              }
              if ((lrank_row<k*size_level1[0])&&((node_size/counts_max+m-lrank_row%k)%k==0)&&((node_size/counts_max+i-lrank_row/k)%size_level1[0]==0)){
                if (size_local){
                  nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ememcpy, eshmemp, add+add_local, esendbufp, add2+add_local, size_local, parameters->ascii_out);
                }
              }
            }
            add+=size;
          }
          for (i=1; i<node_size/counts_max; i++){
            nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
            add=ldispls[lrank_column];
            for (j=0; j<size_level1[0]; j++){
              size=mcounts[data[0][j].frac];
              add2=moffsets[data[0][j].frac];
              for (m=0; m<k; m++){
                size_local=(size/type_size/k)*type_size;
                add_local=size_local*m;
                if (m<(size/type_size)%k){
                  size_local+=type_size;
                  add_local+=m*type_size;
                }else{
                  add_local+=((size/type_size)%k)*type_size;
                }
                if ((lrank_row<k*size_level1[0])&&((node_size/counts_max+m-lrank_row%k)%k==i%k)&&((node_size/counts_max+j-lrank_row/k)%size_level1[0]==i/k)){
                  if (size_local){
                    nbuffer_out+=write_assembler_line_ssdsdd(buffer_out+nbuffer_out, ereduce, eshmemp, add+add_local, esendbufp, add2+add_local, size_local, parameters->ascii_out);
                  }
                }
              }
              add+=size;
            }
          }
        }
        nbuffer_out+=write_assembler_line_s(buffer_out+nbuffer_out, enode_barrier, parameters->ascii_out);
        nbuffer_out+=write_eof(buffer_out+nbuffer_out, parameters->ascii_out);
        break;
        case 2:
          if (collective_type==2){
            printf("copyin not implemented\n");
            exit(2);
          }
          if (moffsets[num_nodes]<CACHE_LINE_SIZE){
            add=CACHE_LINE_SIZE*lrank_row+ldispls[lrank_column];
          }else{
            add=moffsets[num_nodes]*lrank_row+ldispls[lrank_column];
          }
          for (i=0; i<size_level1[0]; i++){
            size=mcounts[data[0][i].frac];
            add2=moffsets[data[0][i].frac];
            if (size){
              nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
            }
            add+=size;
          }
          if (node_rank%RADIX){
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOONE SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank));
          }
          size=moffsets[num_nodes];
          for (barriers_size=1, step=RADIX; step/RADIX<node_size; barriers_size++, step*=RADIX){
            for (substep=0; substep<RADIX-1; substep++){
              if ((node_rank%step==0)&&(node_rank+step/RADIX+substep*(step/RADIX)<node_size/counts_max)){
                nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMWAIT SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank+step/RADIX+substep*(step/RADIX)+(barriers_size-1)*node_size));
              }
              if (moffsets[num_nodes]<CACHE_LINE_SIZE){
                add=CACHE_LINE_SIZE*node_rank;
                add2=add+CACHE_LINE_SIZE*(step/RADIX+substep*(step/RADIX));
              }else{
                add=moffsets[num_nodes]*node_rank;
                add2=add+moffsets[num_nodes]*(step/RADIX+substep*(step/RADIX));
              }
              if (size){
                if ((node_rank%step==0)&&(node_rank+step/RADIX+substep*(step/RADIX)<node_size/counts_max)){
                  nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SREDUCE SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
                }
                if (moffsets[num_nodes]<CACHE_LINE_SIZE){
                  add2=add+CACHE_LINE_SIZE*(node_size-1);
                }else{
                  add2=add+moffsets[num_nodes]*(node_size-1);
                }
                nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SREDUC_ SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
              }
            }
            if (step<node_size){
              nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOONE SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank+barriers_size*node_size));
            }
          }
          nbuffer_out+=sprintf(buffer_out+nbuffer_out, " NODE_BARRIER\n");
          for (barriers_size-=2; barriers_size>=0; barriers_size--){
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOZERO SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank+barriers_size*node_size));
          }
          break;
        case 3:
          if (collective_type==2){
            printf("copyin not implemented\n");
            exit(2);
          }
          if (moffsets[num_nodes]<CACHE_LINE_SIZE){
            add=CACHE_LINE_SIZE*lrank_row+ldispls[lrank_column];
          }else{
            add=moffsets[num_nodes]*lrank_row+ldispls[lrank_column];
          }
          for (i=0; i<size_level1[0]; i++){
            size=mcounts[data[0][i].frac];
            add2=moffsets[data[0][i].frac];
            if (size){
              nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
            }
            add+=size;
          }
          if (node_rank%RADIX){
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOONE SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank));
          }
          size=moffsets[num_nodes];
          nbuffer_out+=sprintf(buffer_out+nbuffer_out, " COPYIN %d %d %d %d %d\n", node_size, node_rank, counts_max, moffsets[num_nodes], RADIX);
          for (barriers_size=1, step=RADIX; step/RADIX<node_size; barriers_size++, step*=RADIX){
            for (substep=0; substep<RADIX-1; substep++){
              if ((node_rank%step==0)&&(node_rank+step/RADIX+substep*(step/RADIX)<node_size/counts_max)){
//                nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMWAIT SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank+step/RADIX+substep*(step/RADIX)+(barriers_size-1)*node_size));
              }
              if (moffsets[num_nodes]<CACHE_LINE_SIZE){
                add=CACHE_LINE_SIZE*node_rank;
                add2=add+CACHE_LINE_SIZE*(step/RADIX+substep*(step/RADIX));
              }else{
                add=moffsets[num_nodes]*node_rank;
                add2=add+moffsets[num_nodes]*(step/RADIX+substep*(step/RADIX));
              }
              if (size){
                if ((node_rank%step==0)&&(node_rank+step/RADIX+substep*(step/RADIX)<node_size/counts_max)){
//                  nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SREDUCE SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
                }
                if (moffsets[num_nodes]<CACHE_LINE_SIZE){
                  add2=add+CACHE_LINE_SIZE*(node_size-1);
                }else{
                  add2=add+moffsets[num_nodes]*(node_size-1);
                }
                nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SREDUC_ SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
              }
            }
            if (step<node_size){
//              nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOONE SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank+barriers_size*node_size));
            }
          }
          nbuffer_out+=sprintf(buffer_out+nbuffer_out, " NODE_BARRIER\n");
          for (barriers_size-=2; barriers_size>=0; barriers_size--){
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOZERO SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank+barriers_size*node_size));
          }
          break;
        case 4:
          if (collective_type==2){
            printf("copyin not implemented\n");
            exit(2);
          }
          if (lrank_row==0){
            add=ldispls[lrank_column];
            for (i=0; i<size_level1[0]; i++){
              size=mcounts[data[0][i].frac];
              add2=moffsets[data[0][i].frac];
              if (size){
                nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
              }
              add+=size;
            }
          }
          nbuffer_out+=sprintf(buffer_out+nbuffer_out, " NODE_BARRIER\n");
          if (lrank_row!=0){
            add=ldispls[lrank_column];
            for (i=0; i<size_level1[0]; i++){
              size=mcounts[data[0][i].frac];
              add2=moffsets[data[0][i].frac];
              if (size){
                nbuffer_out+=sprintf(buffer_out+nbuffer_out, " ATOMICADD SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
              }
              add+=size;
            }
          }
          nbuffer_out+=sprintf(buffer_out+nbuffer_out, " NODE_BARRIER\n");
          break;
      case 5:
      if (moffsets[num_nodes]<CACHE_LINE_SIZE){
        add=CACHE_LINE_SIZE*lrank_row+ldispls[lrank_column];
        if (moffsets[num_nodes]<CACHE_LINE_SIZE-2*type_size){
          add=-CACHE_LINE_SIZE*(1+node_rank)+type_size;
        }
      }else{
        add=moffsets[num_nodes]*lrank_row+ldispls[lrank_column];
      }
      for (i=0; i<size_level1[0]; i++){
        size=mcounts[data[0][i].frac];
        add2=moffsets[data[0][i].frac];
        if (size){
          nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
          if (node_rank!=0){
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOONE SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+node_rank));
          }
        }
        add+=size;
      }
      size=moffsets[num_nodes];
      add=0;
      for (i=1; i<node_size/counts_max; i++){
        if (moffsets[num_nodes]<CACHE_LINE_SIZE){
          add2=add+CACHE_LINE_SIZE*i;
          if (moffsets[num_nodes]<CACHE_LINE_SIZE-2*type_size){
            add2=-CACHE_LINE_SIZE*(1+i)+type_size;
          }
        }else{
          add2=add+moffsets[num_nodes]*i;
        }
        if (size){
          if (node_rank==0){
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMWAIT SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+i));
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SREDUCE SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOZERO SHMEM+ %d 1\n", -CACHE_LINE_SIZE*(1+i));
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SETTOZERO SHMEM+ %d 1\n", add2);
          }else{
            nbuffer_out+=sprintf(buffer_out+nbuffer_out, " SREDUC_ SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
          }
        }
      }
      nbuffer_out+=sprintf(buffer_out+nbuffer_out, " NODE_BARRIER_MASTER\n");
      break;
    }
    }
  }else{
    add=iodispls[node_rank];
    add2=0;
    size=iocounts[node_rank];
    if (size){
      nbuffer_out+=sprintf(buffer_out+nbuffer_out, " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
    }
    nbuffer_out+=sprintf(buffer_out+nbuffer_out, " NODE_BARRIER\n");
  }
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
