#include "raw_code_invreduce.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_raw_code_invreduce(char *buffer_in, char *buffer_out) {
  int *nodes_recv = NULL, *nodes_send = NULL,
      *rank_perm = NULL, *rank_back_perm = NULL, msizes_max = -1, *temp_reduce,
      num_nodes, nbuffer_out = 0, nbuffer_in = 0, flag, i, j, k, l;
  struct data_algorithm data;
  struct parameters_block *parameters;
  memset(&data, 0, sizeof(struct data_algorithm));
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  num_nodes = parameters->num_nodes;
  msizes_max = parameters->rank_perm_max;
  rank_perm = parameters->rank_perm;
  if (!rank_perm) {
    msizes_max = num_nodes;
    rank_perm = (int *)malloc(msizes_max * sizeof(int));
    if (!rank_perm)
      goto error;
    for (i = 0; i < msizes_max; i++) {
      rank_perm[i] = i;
    }
  }
  rank_back_perm = (int *)malloc(msizes_max * sizeof(int));
  if (!rank_back_perm)
    goto error;
  for (i = 0; i < msizes_max; i++) {
    rank_back_perm[rank_perm[i]] = i;
  }
  nodes_recv = (int *)malloc(sizeof(int) * num_nodes);
  if (!nodes_recv)
    goto error;
  nodes_send = (int *)malloc(sizeof(int) * num_nodes);
  if (!nodes_send)
    goto error;
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code_tasks_node_master\n");
    exit(2);
  }
  for (i = 0; i < data.num_blocks; i++) {
    for (j = 0; j < data.blocks[i].num_lines; j++) {
      if (data.blocks[i].lines[j].reducefrom_max) {
	data.blocks[i].lines[j].reducefrom_max++;
	temp_reduce = (int*)malloc(data.blocks[i].lines[j].reducefrom_max * sizeof(int));
	temp_reduce[0] = j;
        for (k = 1; k < data.blocks[i].lines[j].reducefrom_max; k++) {
	  temp_reduce[k] = data.blocks[i].lines[j].reducefrom[k - 1];
	}
	free(data.blocks[i].lines[j].reducefrom);
	data.blocks[i].lines[j].reducefrom = temp_reduce;
	temp_reduce = (int*)malloc(data.blocks[i].lines[j].reducefrom_max * sizeof(int));
	for (k = 0; k < data.blocks[i].lines[j].reducefrom_max; k++) {
	  temp_reduce[k] = -1;
	  for (l = i, flag = 1; l >= 0 && flag; l--) {
	    if (data.blocks[l].lines[data.blocks[i].lines[j].reducefrom[k]].recvfrom_max) {
	      if (data.blocks[l].lines[data.blocks[i].lines[j].reducefrom[k]].recvfrom_node[0] == -1) {
		temp_reduce[k] = parameters->node;
		flag = 0;
	      }
	      if (data.blocks[l].lines[data.blocks[i].lines[j].reducefrom[k]].recvfrom_node[0] >= 0) {
		temp_reduce[k] = data.blocks[l].lines[data.blocks[i].lines[j].reducefrom[k]].recvfrom_node[0];
                flag = 0;
	      }
	    }
	  }
	}
	flag = 1;
	while (flag) {
	  flag = 0;
	  for (k = 0; k < data.blocks[i].lines[j].reducefrom_max - 1; k++) {
	    if (temp_reduce[k] > temp_reduce[k + 1]) {
	      l = temp_reduce[k]; temp_reduce[k] = temp_reduce[k + 1]; temp_reduce[k + 1] = l;
	      l = data.blocks[i].lines[j].reducefrom[k]; data.blocks[i].lines[j].reducefrom[k] = data.blocks[i].lines[j].reducefrom[k + 1]; data.blocks[i].lines[j].reducefrom[k + 1] = l;
	      flag = 1;
	    }
	  }
	}
	free(temp_reduce);
      }
    }
  }
  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  free(rank_back_perm);
  free(rank_perm);
  parameters->rank_perm = NULL;
  parameters->rank_perm_max = 0;
  ext_mpi_delete_parameters(parameters);
  free(nodes_send);
  free(nodes_recv);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  free(rank_back_perm);
  free(rank_perm);
  if (parameters) {
    parameters->rank_perm = NULL;
    parameters->rank_perm_max = 0;
  }
  ext_mpi_delete_parameters(parameters);
  free(nodes_send);
  free(nodes_recv);
  return ERROR_MALLOC;
}
