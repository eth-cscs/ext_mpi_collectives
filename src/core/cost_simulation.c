#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read_write.h"
#include "read_bench.h"
#include "count_instructions.h"
#include "ports_groups.h"
#include "cost_estimation.h"
#include "cost_simulation.h"

int ext_mpi_cost_simulation(int count, int type_size, int comm_size_row,
                            int my_cores_per_node_row, int comm_size_column,
                            int my_cores_per_node_column, int comm_size_rowb,
                            int comm_rank_row, int simulate, int bit_identical, int num_sockets) {
  struct cost_list *p1;
  int *num_ports = NULL, *groups = NULL, i,
      *num_steps = NULL, r, j, k;
  void *sendbuf = NULL, *recvbuf = NULL;
  double T_step, mb, *counts = NULL;
  char *code_address;
  ext_mpi_cost_estimation(count, type_size, comm_size_row,
                          my_cores_per_node_row, comm_size_column,
                          my_cores_per_node_column, comm_size_rowb,
                          comm_rank_row, simulate, num_sockets);
  sendbuf = (void *)malloc(count * type_size);
  if (!sendbuf)
    goto error;
  recvbuf = (void *)malloc(count * type_size);
  if (!recvbuf)
    goto error;
  p1 = ext_mpi_cost_list_start;
  while (p1) {
    num_ports = (int *)malloc((p1->depth + 1) * sizeof(int));
    if (!num_ports)
      goto error;
    groups = (int *)malloc((p1->depth + 1) * sizeof(int));
    if (!groups)
      goto error;
    num_steps = (int *)malloc((p1->depth + 1) * sizeof(int));
    if (!num_steps)
      goto error;
    counts = (double *)malloc((p1->depth + 1) * sizeof(double));
    if (!counts)
      goto error;
    for (i = 0; i < p1->depth; i++) {
      num_ports[i] = p1->rarray[i];
      groups[i] = p1->garray[i];
    }
    num_ports[p1->depth] = groups[p1->depth] = 0;
    if (ext_mpi_allreduce_init_draft(
        sendbuf, recvbuf, count, type_size,
        comm_size_row / my_cores_per_node_row, my_cores_per_node_row,
        comm_size_column / my_cores_per_node_column, my_cores_per_node_column,
        num_ports, groups, my_cores_per_node_row * my_cores_per_node_column,
        bit_identical, &code_address) < 0)
      goto error;
    if (ext_mpi_count_native(code_address, counts, num_steps) < 0)
      goto error;
    free(code_address);
    p1->T_simulated = 0e0;
    T_step = -1e0;
    p1->nsteps = 0e0;
    p1->nvolume = 0e0;
    for (i = 0; num_steps[i]; i++) {
      r = num_steps[i] + 1;
      mb = counts[i] * num_sockets;
      mb /= ext_mpi_file_input[((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].parallel;
      p1->nsteps += 1e0;
      p1->nvolume += mb;
      j = floor(mb / (ext_mpi_file_input[1].msize - ext_mpi_file_input[0].msize)) - 1;
      k = j + 1;
      if (j < 0) {
        T_step = ext_mpi_file_input[0 + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].deltaT;
      } else {
        if (k >= ext_mpi_file_input_max_per_core) {
          T_step = ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                              ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core]
                       .deltaT *
                   mb /
                   ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                              ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core]
                       .msize;
        } else {
          T_step =
              ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].deltaT +
              (mb -
               ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].msize) *
                  (ext_mpi_file_input[k + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core]
                       .deltaT -
                   ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core]
                       .deltaT) /
                  (ext_mpi_file_input[k + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].msize -
                   ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].msize);
        }
      }
      p1->T_simulated += T_step;
    }
    free(counts);
    free(num_steps);
    free(groups);
    free(num_ports);
    p1 = p1->next;
  }
  // print_cost_list();
  free(recvbuf);
  free(sendbuf);
  return 0;
error:
  free(counts);
  free(num_steps);
  free(groups);
  free(num_ports);
  free(recvbuf);
  free(sendbuf);
  return ERROR_MALLOC;
}

int ext_mpi_allreduce_simulate(int count, int type_size,
                               int comm_size_row, int my_cores_per_node_row,
                               int comm_size_column,
                               int my_cores_per_node_column, int bit_identical, int num_sockets) {
  int comm_rank_row = 0, i;
  int *num_ports = NULL, *groups = NULL;
  struct cost_list *p1, *p2;
  char *str;
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (i = 0; i < comm_size_row; i++) {
    num_ports[i] = groups[i] = 0;
  }
  if (comm_size_row / my_cores_per_node_row > 1) {
    if (my_cores_per_node_row == 1) {
      if (ext_mpi_cost_simulation(count, type_size, comm_size_row * 12, 12,
                                  comm_size_column, my_cores_per_node_column,
                                  comm_size_row, comm_rank_row, 1, bit_identical, num_sockets) < 0)
        goto error;
    } else {
      if (ext_mpi_cost_simulation(count, type_size, comm_size_row, my_cores_per_node_row,
                                  comm_size_column, my_cores_per_node_column,
                                  comm_size_row, comm_rank_row, 1, bit_identical, num_sockets) < 0)
        goto error;
    }
    p1 = ext_mpi_cost_list_start;
    while (p1) {
      for (i = 0; p1->rarray[i]; i++) {
        num_ports[i] = p1->rarray[i];
        groups[i] = p1->garray[i];
      }
      groups[i] = num_ports[i] = 0;
      str=ext_mpi_print_ports_groups(num_ports, groups);
      printf("%s | %e %e\n", str, p1->nsteps, p1->nvolume);
      free(str);
      p2 = p1;
      p1 = p1->next;
      free(p2->garray);
      free(p2->rarray);
      free(p2);
    }
    ext_mpi_cost_list_start = NULL;
    ext_mpi_cost_list_length = 0;
    ext_mpi_cost_list_counter = 0;
    for (i = 0; num_ports[i]; i++)
      ;
    groups[i] = num_ports[i] = 0;
  } else {
    groups[0] = num_ports[0] = -1;
    groups[1] = num_ports[1] = 0;
  }
  free(groups);
  free(num_ports);
  return 0;
error:
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}
