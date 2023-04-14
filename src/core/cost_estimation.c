#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read_bench.h"
#include "prime_factors.h"
#include "cost_estimation.h"

struct cost_list *ext_mpi_cost_list_start = NULL;
int ext_mpi_cost_list_length = 0;
int ext_mpi_cost_list_counter = 0;

/*static void print_cost_list(){
  struct cost_list *p1;
  int i;
  p1=cost_list_start;
  while (p1){
    printf("T=%e", p1->T);
    for (i=0; i<p1->depth; i++){
      printf(" %d", p1->rarray[i]);
    }
    printf(" |");
    for (i=0; i<p1->depth; i++){
      printf(" %d", p1->garray[i]);
    }
    printf(" T_simulated=%e\n", p1->T_simulated);
    p1=p1->next;
  }
}*/

static int cost_put_in_pool(int depth, int *rarray, int *garray, double T,
                            int comm_size_row, int comm_rank_row) {
  struct cost_list *p1 = NULL, *p2 = NULL;
  int flag, i;
  if (ext_mpi_cost_list_counter == comm_rank_row) {
    p1 = (struct cost_list *)malloc(sizeof(struct cost_list));
    if (!p1)
      goto error;
    p1->rarray = NULL;
    p1->garray = NULL;
    p1->rarray = (int *)malloc((depth + 1) * sizeof(int));
    if (!p1->rarray)
      goto error;
    p1->garray = (int *)malloc((depth + 1) * sizeof(int));
    if (!p1->garray)
      goto error;
    p1->T = T;
    p1->depth = depth;
    for (i = 0; i < depth; i++) {
      p1->rarray[i] = rarray[i];
      p1->garray[i] = garray[i];
    }
    p1->garray[depth] = p1->rarray[depth] = 0;
    if (ext_mpi_cost_list_start == NULL) {
      p1->next = NULL;
      ext_mpi_cost_list_start = p1;
    } else {
      p2 = ext_mpi_cost_list_start;
      flag = 1;
      while ((p2->next != NULL) && flag) {
        if (T < p2->next->T) {
          p2 = p2->next;
        } else {
          flag = 0;
        }
      }
      if (p2 == ext_mpi_cost_list_start) {
        p1->next = ext_mpi_cost_list_start;
        ext_mpi_cost_list_start = p1;
      } else {
        p1->next = p2->next;
        p2->next = p1;
      }
    }
    ext_mpi_cost_list_length++;
    while (ext_mpi_cost_list_length > COST_LIST_LENGTH_MAX) {
      p1 = ext_mpi_cost_list_start;
      ext_mpi_cost_list_start = p1->next;
      free(p1->garray);
      free(p1->rarray);
      free(p1);
      ext_mpi_cost_list_length--;
    }
  }
  ext_mpi_cost_list_counter = (ext_mpi_cost_list_counter + 1) % comm_size_row;
  return 0;
error:
  if (p1) {
    free(p1->rarray);
    free(p1->garray);
  }
  free(p1);
  return ERROR_MALLOC;
}

static int insert_sockets(int depth, int *trarray, int *ttgarray, int num_sockets) {
  int i = 1, ret = 0;
  if (num_sockets > 1) {
    while (i < num_sockets) {
      ret++;
      i *= 2;
    }
    for (i = 0; i < ret; i++) {
      trarray[depth + ret + i + 1] = 1;
      ttgarray[depth + ret + i + 1] = -2;
    }
    for (i = depth + ret; i >= ret; i--) {
      trarray[i] = trarray[i - ret];
      ttgarray[i] = ttgarray[i - ret];
    }
    for (i = 0; i < ret; i++) {
      trarray[i] = -1;
      ttgarray[i] = -2;
    }
    trarray[depth + 2 * ret + 1] = ttgarray[depth + 2 * ret + 1] = 0;
  }
  return ret;
}

static int cost_explicit(int p, double n, int depth, int fac, double T,
                         int port_max, int *rarray, int *garray, int type,
                         int comm_size_row, int comm_rank_row, int simulate, int num_sockets) {
  double T_step, ma, mb;
  int r, rrr, i, j, k, *trarray = NULL, *tgarray = NULL, *ttgarray = NULL;
  if (port_max > ext_mpi_file_input[ext_mpi_file_input_max - 1].nports) {
    port_max = ext_mpi_file_input[ext_mpi_file_input_max - 1].nports;
  }
  if (port_max > (abs(garray[depth]) * num_sockets - 1) / fac + 1) {
    port_max = (abs(garray[depth]) * num_sockets - 1) / fac + 1;
  }
  if (port_max < 1) {
    port_max = 1;
  }
  for (i = 1; i <= port_max; i++) {
    r = i + 1;
    rrr = (r - 1) * num_sockets - 1;
    if (rrr > port_max - 1) {
      rrr = port_max - 1;
    }
    r = (rrr + 1) / num_sockets + 1;
    if (r < 2) r = 2;
    ma = fac;
    if (ma * r > abs(garray[depth])) {
      ma = (abs(garray[depth]) - ma) / (r - 1);
    }
    mb = n * ma;
    mb /= ext_mpi_file_input[rrr * ext_mpi_file_input_max_per_core].parallel;
    j = floor(mb / (ext_mpi_file_input[1].msize - ext_mpi_file_input[0].msize)) - 1;
    k = j + 1;
    if (j < 0) {
      T_step = ext_mpi_file_input[0 + rrr * ext_mpi_file_input_max_per_core].deltaT;
    } else {
      if (k >= ext_mpi_file_input_max_per_core) {
        T_step = ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                            rrr * ext_mpi_file_input_max_per_core]
                     .deltaT *
                 mb /
                 ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                            rrr * ext_mpi_file_input_max_per_core]
                     .msize;
      } else {
        T_step =
            ext_mpi_file_input[j + rrr * ext_mpi_file_input_max_per_core].deltaT +
            (mb - ext_mpi_file_input[j + rrr * ext_mpi_file_input_max_per_core].msize) *
                (ext_mpi_file_input[k + rrr * ext_mpi_file_input_max_per_core].deltaT -
                 ext_mpi_file_input[j + rrr * ext_mpi_file_input_max_per_core].deltaT) /
                (ext_mpi_file_input[k + rrr * ext_mpi_file_input_max_per_core].msize -
                 ext_mpi_file_input[j + rrr * ext_mpi_file_input_max_per_core].msize);
      }
    }
    rarray[depth] = -(r - 1);
    for (j = 0; garray[j]; j++)
      ;
    tgarray = (int *)malloc((j + depth + 2) * 2 * sizeof(int));
    if (!tgarray)
      goto error;
    for (j = 0; garray[j]; j++) {
      tgarray[j] = garray[j];
    }
    tgarray[j] = garray[j];
    if (fac * r < abs(tgarray[depth])) {
      for (j = depth; tgarray[j]; j++)
        ;
      for (; j >= depth; j--) {
        tgarray[j + 1] = tgarray[j];
      }
      tgarray[depth] = abs(tgarray[depth + 1]);
      if (cost_explicit(p, n, depth + 1, fac * r, T + T_step, port_max, rarray,
                        tgarray, type, comm_size_row, comm_rank_row,
                        simulate, num_sockets) < 0)
        goto error;
    } else {
      i = port_max + 1;
      if (tgarray[depth + 1]) {
        if (cost_explicit(p, n * ma, depth + 1, 1, T + T_step, port_max, rarray,
                          tgarray, type, comm_size_row, comm_rank_row,
                          simulate, num_sockets) < 0)
          goto error;
      } else {
        trarray = (int *)malloc((depth + 2 + 2 * num_sockets * num_sockets) * 2 * sizeof(int));
        if (!trarray)
          goto error;
        ttgarray = (int *)malloc((depth + 2 + 2 * num_sockets * num_sockets) * 2 * sizeof(int));
        if (!ttgarray)
          goto error;
        switch (type) {
        case 0:
          for (k = 0; k < depth + 1; k++) {
            trarray[k] = -rarray[k];
            ttgarray[k] = tgarray[k];
          }
	  depth += 2 * insert_sockets(depth, trarray, ttgarray, num_sockets);
          if (simulate) {
            if (cost_put_in_pool(depth + 1, trarray, ttgarray, T + T_step, 1,
                                 0) < 0)
              goto error;
          } else {
            if (cost_put_in_pool(depth + 1, trarray, ttgarray, T + T_step,
                                 comm_size_row, comm_rank_row) < 0)
              goto error;
          }
          break;
        case 1:
          j = 0;
          for (k = 0; k < depth + 1; k++) {
            if (tgarray[k] < 0) {
              j++;
            }
          }
          if (j <= 1) {
            for (k = 0; k < depth + 1; k++) {
              trarray[depth + 1 + k] = -(trarray[depth - k] = rarray[k]);
              ttgarray[depth - k] = abs(ttgarray[depth + 1 + k] = tgarray[k]);
              if (k == 0) {
                ttgarray[depth - k] *= -1;
              } else if (tgarray[k - 1] < 0) {
                ttgarray[depth - k] *= -1;
              }
            }
	    ttgarray[depth] = abs(ttgarray[depth]);
          } else {
            for (k = 0; k < depth + 1; k++) {
              trarray[depth + 1 + k] = -(trarray[k] = rarray[k]);
              ttgarray[depth + 1 + k] = ttgarray[k] = tgarray[k];
            }
          }
	  depth += insert_sockets(depth * 2 + 1, trarray, ttgarray, num_sockets);
          if (simulate) {
            if (cost_put_in_pool(depth * 2 + 2, trarray, ttgarray,
                                 (T + T_step) * 2, 1, 0) < 0)
              goto error;
          } else {
            if (cost_put_in_pool(depth * 2 + 2, trarray, ttgarray,
                                 (T + T_step) * 2, comm_size_row,
                                 comm_rank_row) < 0)
              goto error;
          }
          break;
        case 2:
          for (j = depth - 1; tgarray[j] > 0; j--)
            ;
          j = depth - j;
          for (k = 0; k < depth + 1; k++) {
            trarray[k + j] = -rarray[k];
            ttgarray[k + j] = tgarray[k];
          }
          for (k = 0; k < j; k++) {
            trarray[k] = rarray[depth - j + 1 + k];
            ttgarray[k] = tgarray[depth - j + 1 + k];
          }
	  depth += 2 * insert_sockets(depth + j, trarray, ttgarray, num_sockets);
          if (simulate) {
            if (cost_put_in_pool(depth + 1 + j, trarray, ttgarray, T + T_step,
                                 1, 0) < 0)
              goto error;
          } else {
            if (cost_put_in_pool(depth + 1 + j, trarray, ttgarray, T + T_step,
                                 comm_size_row, comm_rank_row) < 0)
              goto error;
          }
          break;
        }
        free(ttgarray);
        free(trarray);
      }
    }
    free(tgarray);
  }
  return 0;
error:
  free(trarray);
  free(tgarray);
  free(ttgarray);
  return ERROR_MALLOC;
}

int ext_mpi_cost_estimation(int count, int type_size, int comm_size_row,
                            int my_cores_per_node_row, int comm_size_column,
                            int my_cores_per_node_column, int comm_size_rowb,
                            int comm_rank_row, int simulate, int num_sockets) {
  int *num_ports = NULL, *groups = NULL, *num_steps = NULL, i, j;
  double *counts = NULL;
  num_ports = (int *)malloc(2 * (comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc(2 * (comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  groups[0] = -comm_size_row / my_cores_per_node_row / num_sockets;
  groups[1] = 0;
  if (cost_explicit(comm_size_row / my_cores_per_node_row / num_sockets,
                    count * type_size * 1e0, 0, 1, 0e0,
                    my_cores_per_node_row * my_cores_per_node_column, num_ports,
                    groups, 0, comm_size_rowb, comm_rank_row, simulate, num_sockets) < 0)
    goto error;
  i = count / (comm_size_row / my_cores_per_node_row);
  if (i < 1) {
    i = 1;
  }
  if (cost_explicit(comm_size_row / my_cores_per_node_row / num_sockets, i * type_size * 1e0,
                    0, 1, 0e0, my_cores_per_node_row * my_cores_per_node_column,
                    num_ports, groups, 1, comm_size_rowb, comm_rank_row,
                    simulate, num_sockets) < 0)
    goto error;
  ext_mpi_factors_minimum(comm_size_row / my_cores_per_node_row / num_sockets,
                          my_cores_per_node_row * my_cores_per_node_column, groups);
  for (j = 0; groups[j]; j++) {
    groups[j] *= -1;
  }
  if (cost_explicit(comm_size_row / my_cores_per_node_row / num_sockets,
                    count * type_size * 1e0, 0, 1, 0e0,
                    my_cores_per_node_row * my_cores_per_node_column, num_ports,
                    groups, 0, comm_size_rowb, comm_rank_row, simulate, num_sockets) < 0)
    goto error;
  if (cost_explicit(comm_size_row / my_cores_per_node_row / num_sockets, i * type_size * 1e0,
                    0, 1, 0e0, my_cores_per_node_row * my_cores_per_node_column,
                    num_ports, groups, 1, comm_size_rowb, comm_rank_row,
                    simulate, num_sockets) < 0)
    goto error;
  i = ext_mpi_factor_sqrt(comm_size_row / my_cores_per_node_row / num_sockets);
  if ((i > 1) && (i < comm_size_row / my_cores_per_node_row)) {
    groups[0] = -(comm_size_row / my_cores_per_node_row) / num_sockets / i;
    groups[1] = -i;
    groups[2] = 0;
    if (cost_explicit(comm_size_row / my_cores_per_node_row / num_sockets,
                      sqrt(count) * type_size * 1e0, 0, 1, 0e0,
                      my_cores_per_node_row * my_cores_per_node_column,
                      num_ports, groups, 2, comm_size_rowb, comm_rank_row,
                      simulate, num_sockets) < 0)
      goto error;
  }
  free(groups);
  free(num_ports);
  return 0;
error:
  free(counts);
  free(num_steps);
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}
