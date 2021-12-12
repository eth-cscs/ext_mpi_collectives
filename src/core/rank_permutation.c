#include "rank_permutation.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*static void rank_perm_heuristic__(int num_nodes, int *node_recvcounts,
                           int *rank_perm) {
  int i, i1, i2, iii;
  for (i = 0; i < num_nodes; i++) {
    rank_perm[i] = i;
  }
  //  for (i=0; i<num_nodes; i++){
  for (i = 0; i < 0; i++) {
    i1 = rand() % num_nodes;
    i2 = rand() % num_nodes;
    iii = rank_perm[i1];
    rank_perm[i1] = rank_perm[i2];
    rank_perm[i2] = iii;
  }
}*/

static int rank_perm_heuristic_compare(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}

/*static void rank_perm_heuristic_bug(int num_nodes, int *node_recvcounts,
                             int *rank_perm) {
  typedef struct node {
    int value, key;
  } node_t;
  node_t array[num_nodes];
  int lnode_recvcounts[num_nodes / 2], lrank_perm[num_nodes / 2],
      perm_array[num_nodes];
  int i;
  for (i = 0; i < num_nodes; i++) {
    array[i].value = node_recvcounts[i];
    array[i].key = i;
  }
  if (num_nodes > 1) {
    qsort(array, num_nodes, sizeof(node_t), rank_perm_heuristic_compare);
    for (i = 0; i < num_nodes / 2; i++) {
      lnode_recvcounts[i] =
          array[i].value + array[(num_nodes / 2) * 2 - 1 - i].value;
    }
    ext_mpi_rank_perm_heuristic(num_nodes / 2, lnode_recvcounts, lrank_perm);
    for (i = 0; i < num_nodes / 2; i++) {
      perm_array[i * 2] = lrank_perm[i];
      perm_array[i * 2 + 1] = (num_nodes / 2) * 2 - 1 - lrank_perm[i];
    }
    if (num_nodes % 2) {
      perm_array[num_nodes - 1] = num_nodes - 1;
    }
  } else {
    perm_array[0] = 0;
  }
  for (i = 0; i < num_nodes; i++) {
    rank_perm[array[i].key] = perm_array[i];
  }
}*/

void ext_mpi_rank_perm_heuristic(int num_nodes, int *node_recvcounts, int *rank_perm) {
  typedef struct node_sort {
    int value, key, color;
  } node_t_s;
  typedef struct node {
    int value, key, color;
  } node_t;
  node_t array_org[num_nodes], array_org2[num_nodes];
  node_t_s array[num_nodes];
  int num_colors, flag, i, j, k;
  for (i = 0; i < num_nodes; i++) {
    array_org[i].value = node_recvcounts[i];
    array_org[i].key = array_org[i].color = i;
  }
  flag = 1;
  num_colors = num_nodes;
  while (flag) {
    for (i = 0; i < num_colors; i++) {
      array[i].value = 0;
      array[i].key = i;
    }
    for (i = 0; i < num_nodes; i++) {
      array[array_org[i].color].value += array_org[i].value;
    }
    if (num_colors % 2) {
      array[0].value = 0;
    }
    qsort(array, num_colors, sizeof(node_t_s), rank_perm_heuristic_compare);
    k = 0;
    if (!(num_colors % 2)) {
      for (i = 0; i < num_colors; i++) {
        for (j = 0; j < num_nodes; j++) {
          if (i % 2) {
            if (array[num_colors - 1 - i / 2].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2;
            }
          } else {
            if (array[i / 2].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2;
            }
          }
        }
      }
    } else {
      for (j = 0; j < num_nodes; j++) {
        if (0 == array_org[j].color) {
          array_org2[k++] = array_org[j];
        }
      }
      for (i = 0; i < num_colors - 1; i++) {
        for (j = 0; j < num_nodes; j++) {
          if (i % 2) {
            if (array[num_colors - 1 - i / 2].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2 + 1;
            }
          } else {
            if (array[i / 2 + 1].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2 + 1;
            }
          }
        }
      }
    }
    for (i = 0; i < num_nodes; i++) {
      array_org[i] = array_org2[i];
    }
    num_colors = (num_colors + 1) / 2;
    flag = num_colors > 1;
  }
  for (i = 0; i < num_nodes; i++) {
    rank_perm[i] = array_org[i].key;
  }
}

int ext_mpi_generate_rank_permutation_forward(char *buffer_in, char *buffer_out) {
  int num_nodes, flag, msizes_max = 0, *msizes = NULL, *rank_perm = NULL,
                       *rank_back_perm = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, i;
  struct parameters_block *parameters;
  char line[1000];
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  num_nodes = parameters->num_nodes;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  if (num_nodes != msizes_max) {
    printf("wrong number of message sizes\n");
    exit(2);
  }
  rank_perm = (int *)malloc(msizes_max * sizeof(int));
  if (!rank_perm)
    goto error;
  flag = 0;
  for (i = 0; i < msizes_max; i++) {
    if (msizes[i] != msizes[0]) {
      flag = 1;
    }
  }
  if (flag) {
    ext_mpi_rank_perm_heuristic(num_nodes, msizes, rank_perm);
  } else {
    for (i = 0; i < msizes_max; i++) {
      rank_perm[i] = i;
    }
  }
  parameters->rank_perm_max = msizes_max;
  parameters->rank_perm = rank_perm;
  parameters->message_sizes =
      (int *)malloc(sizeof(int) * parameters->message_sizes_max);
  if (!parameters->message_sizes)
    goto error;
  rank_back_perm = (int *)malloc(msizes_max * sizeof(int));
  if (!rank_back_perm)
    goto error;
  for (i = 0; i < msizes_max; i++) {
    rank_back_perm[rank_perm[i]] = i;
  }
  parameters->node = rank_back_perm[parameters->node];
  for (i = 0; i < msizes_max; i++) {
    parameters->message_sizes[i] = msizes[rank_back_perm[i]];
  }
  free(msizes);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag > 0) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
    }
  } while (flag);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(rank_back_perm);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  free(rank_back_perm);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}

int ext_mpi_generate_rank_permutation_backward(char *buffer_in, char *buffer_out) {
  int num_nodes, flag, msizes_max = 0, *msizes = NULL, *rank_perm = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, i, j, k, size_level0 = 0,
      *size_level1 = NULL;
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  char line[1000];
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  num_nodes = parameters->num_nodes;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  rank_perm = parameters->rank_perm;
  if (num_nodes != msizes_max) {
    printf("wrong number of message sizes\n");
    exit(2);
  }
  parameters->node = rank_perm[parameters->node];
  parameters->message_sizes =
      (int *)malloc(sizeof(int) * parameters->message_sizes_max);
  if (!parameters->message_sizes)
    goto error;
  for (i = 0; i < msizes_max; i++) {
    parameters->message_sizes[i] = msizes[rank_perm[i]];
  }
  free(msizes);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &size_level0,
                                           &size_level1, &data, parameters->ascii_in);
  if (i <= 0) {
    printf("error reading algorithm rank_permutation\n");
    exit(2);
  }
  for (i = 0; i < size_level0; i++) {
    for (j = 0; j < size_level1[i]; j++) {
      data[i][j].frac = rank_perm[data[i][j].frac];
      data[i][j].source = rank_perm[data[i][j].source];
      for (k = 0; k < data[i][j].to_max; k++) {
        if (data[i][j].to[k] >= 0) {
          data[i][j].to[k] = rank_perm[data[i][j].to[k]];
        }
      }
      for (k = 0; k < data[i][j].from_max; k++) {
        if (data[i][j].from_node[k] >= 0) {
          data[i][j].from_node[k] = rank_perm[data[i][j].from_node[k]];
        }
      }
    }
  }
  nbuffer_out +=
      ext_mpi_write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                              parameters->ascii_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag > 0) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
    }
  } while (flag);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
