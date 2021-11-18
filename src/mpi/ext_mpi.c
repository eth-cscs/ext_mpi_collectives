#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ext_mpi.h"
#include "constants.h"
#include "ext_mpi_native.h"
#include "prime_factors.h"
#include "read_bench.h"
#include "cost_simple_recursive.h"
#ifdef GPU_ENABLED
#include "gpu_core.h"
#endif

#define COST_LIST_LENGTH_MAX 100

int ext_mpi_bit_identical = 0;

static int is_initialised = 0;
static int copyin_method = 0;
static int alternating = 0;
int *fixed_factors_ports = NULL;
int *fixed_factors_groups = NULL;
int fixed_tile_size_row = 0;
int fixed_tile_size_column = 0;

static int read_env() {
  int mpi_comm_rank, mpi_comm_size, var, i, j;
  char *c = NULL;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_comm_size);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_TILE_SIZE_ROW")) != NULL);
    if (var) {
      sscanf(c, "%d", &fixed_tile_size_row);
      printf("fixed tile size row %d\n", fixed_tile_size_row);
    }
  }
  MPI_Bcast(&fixed_tile_size_row, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_TILE_SIZE_COLUMN")) != NULL);
    if (var) {
      sscanf(c, "%d", &fixed_tile_size_column);
      printf("fixed tile size column %d\n", fixed_tile_size_column);
    }
  }
  MPI_Bcast(&fixed_tile_size_column, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_COPYIN_METHOD")) != NULL);
    if (var) {
      if (c[0] == '1') {
        copyin_method = 1;
        printf("copy in short\n");
      }
      if (c[0] == '2') {
        copyin_method = 2;
        printf("copy in long\n");
      }
      if (c[0] == '3') {
        copyin_method = 3;
        printf("copy in very short\n");
      }
      if (c[0] == '4') {
        copyin_method = 4;
        printf("copy in unordered\n");
      }
      if (c[0] == '5') {
        copyin_method = 5;
        printf("copy in atomic\n");
      }
      if (c[0] == '6') {
        copyin_method = 6;
        printf("copy in very very small\n");
      }
    }
  }
  MPI_Bcast(&copyin_method, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_ALTERNATING")) != NULL);
    if (var) {
      if (c[0] == '1') {
        alternating = 1;
        printf("not alternating\n");
      }
      if (c[0] == '2') {
        alternating = 2;
        printf("alternating\n");
      }
    }
  }
  MPI_Bcast(&alternating, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_PORTS")) != NULL);
  }
  MPI_Bcast(&var, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (var && (fixed_factors_ports != NULL)) {
    free(fixed_factors_ports);
  }
  if (var) {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    fixed_factors_ports = (int *)malloc((2 * j + 1) * sizeof(int));
    if (!fixed_factors_ports)
      goto error;
    if (mpi_comm_rank == 0) {
      i = 0;
      j = 0;
      while (c[i] != 0) {
        sscanf(c, "%d", &fixed_factors_ports[j]);
        j++;
        while ((c[i] != 0) && (c[i] != ',')) {
          c++;
        }
        if (c[i] == ',') {
          c++;
        }
      }
    }
    fixed_factors_ports[j++] = 0;
    MPI_Bcast(&j, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(fixed_factors_ports, j, MPI_INT, 0, MPI_COMM_WORLD);
  }
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_GROUPS")) != NULL);
  }
  MPI_Bcast(&var, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (var && (fixed_factors_groups != NULL)) {
    free(fixed_factors_groups);
  }
  if (var) {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    fixed_factors_groups = (int *)malloc((2 * j + 1) * sizeof(int));
    if (!fixed_factors_groups)
      goto error;
    if (mpi_comm_rank == 0) {
      i = 0;
      j = 0;
      while (c[i] != 0) {
        sscanf(c, "%d", &fixed_factors_groups[j]);
        j++;
        while ((c[i] != 0) && (c[i] != ',')) {
          c++;
        }
        if (c[i] == ',') {
          c++;
        }
      }
    }
    fixed_factors_groups[j++] = 0;
    MPI_Bcast(&j, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(fixed_factors_groups, j, MPI_INT, 0, MPI_COMM_WORLD);
  }
  return 0;
error:
  free(fixed_factors_ports);
  free(fixed_factors_groups);
  return ERROR_MALLOC;
}

struct cost_list {
  double T;
  int depth;
  int *rarray;
  int *garray;
  struct cost_list *next;
  double T_simulated;
  double nsteps;
  double nvolume;
};

static struct cost_list *cost_list_start = NULL;
static int cost_list_length = 0;
static int cost_list_counter = 0;

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
  if (cost_list_counter == comm_rank_row) {
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
    if (cost_list_start == NULL) {
      p1->next = NULL;
      cost_list_start = p1;
    } else {
      p2 = cost_list_start;
      flag = 1;
      while ((p2->next != NULL) && flag) {
        if (T < p2->next->T) {
          p2 = p2->next;
        } else {
          flag = 0;
        }
      }
      if (p2 == cost_list_start) {
        p1->next = cost_list_start;
        cost_list_start = p1;
      } else {
        p1->next = p2->next;
        p2->next = p1;
      }
    }
    cost_list_length++;
    while (cost_list_length > COST_LIST_LENGTH_MAX) {
      p1 = cost_list_start;
      cost_list_start = p1->next;
      free(p1->garray);
      free(p1->rarray);
      free(p1);
      cost_list_length--;
    }
  }
  cost_list_counter = (cost_list_counter + 1) % comm_size_row;
  return 0;
error:
  if (p1) {
    free(p1->rarray);
    free(p1->garray);
  }
  free(p1);
  return ERROR_MALLOC;
}

static int cost_explicit(int p, double n, int depth, int fac, double T,
                         int port_max, int *rarray, int *garray, int type,
                         int comm_size_row, int comm_rank_row, int simulate) {
  double T_step, ma, mb;
  int r, i, j, k, *trarray = NULL, *tgarray = NULL, *ttgarray = NULL;
  if (port_max > ext_mpi_file_input[ext_mpi_file_input_max - 1].nports) {
    port_max = ext_mpi_file_input[ext_mpi_file_input_max - 1].nports;
  }
  if (port_max > (abs(garray[depth]) - 1) / fac + 1) {
    port_max = (abs(garray[depth]) - 1) / fac + 1;
  }
  if (port_max < 1) {
    port_max = 1;
  }
  for (i = 1; i <= port_max; i++) {
    r = i + 1;
    ma = fac;
    if (ma * r > abs(garray[depth])) {
      ma = (abs(garray[depth]) - ma) / (r - 1);
    }
    mb = n * ma;
    mb /= ext_mpi_file_input[(r - 1 - 1) * ext_mpi_file_input_max_per_core].parallel;
    j = floor(mb / (ext_mpi_file_input[1].msize - ext_mpi_file_input[0].msize)) - 1;
    k = j + 1;
    if (j < 0) {
      T_step = ext_mpi_file_input[0 + (r - 1 - 1) * ext_mpi_file_input_max_per_core].deltaT;
    } else {
      if (k >= ext_mpi_file_input_max_per_core) {
        T_step = ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                            (r - 1 - 1) * ext_mpi_file_input_max_per_core]
                     .deltaT *
                 mb /
                 ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                            (r - 1 - 1) * ext_mpi_file_input_max_per_core]
                     .msize;
      } else {
        T_step =
            ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core].deltaT +
            (mb - ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core].msize) *
                (ext_mpi_file_input[k + (r - 1 - 1) * ext_mpi_file_input_max_per_core].deltaT -
                 ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core].deltaT) /
                (ext_mpi_file_input[k + (r - 1 - 1) * ext_mpi_file_input_max_per_core].msize -
                 ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core].msize);
      }
    }
    rarray[depth] = r - 1;
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
                        simulate) < 0)
        goto error;
    } else {
      i = port_max + 1;
      if (tgarray[depth + 1]) {
        if (cost_explicit(p, n * ma, depth + 1, 1, T + T_step, port_max, rarray,
                          tgarray, type, comm_size_row, comm_rank_row,
                          simulate) < 0)
          goto error;
      } else {
        trarray = (int *)malloc((depth + 2) * 2 * sizeof(int));
        if (!trarray)
          goto error;
        ttgarray = (int *)malloc((depth + 2) * 2 * sizeof(int));
        if (!ttgarray)
          goto error;
        switch (type) {
        case 0:
          for (k = 0; k < depth + 1; k++) {
            trarray[k] = -rarray[k];
            ttgarray[k] = tgarray[k];
          }
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
              } else {
                if (tgarray[k - 1] < 0) {
                  ttgarray[depth - k] *= -1;
                }
              }
            }
          } else {
            for (k = 0; k < depth + 1; k++) {
              trarray[depth + 1 + k] = -(trarray[k] = rarray[k]);
              ttgarray[depth + 1 + k] = ttgarray[k] = tgarray[k];
            }
          }
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

static int cost_simulate(int count, MPI_Datatype datatype, int comm_size_row,
                         int my_cores_per_node_row, int comm_size_column,
                         int my_cores_per_node_column, int comm_size_rowb,
                         int comm_rank_row, int simulate) {
  struct cost_list *p1;
  int handle, type_size, *num_ports = NULL, *groups = NULL, i,
                         *num_steps = NULL, r, j, k;
  void *sendbuf = NULL, *recvbuf = NULL;
  double T_step, mb, *counts = NULL;
  MPI_Type_size(datatype, &type_size);
  num_ports = (int *)malloc(2 * (comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc(2 * (comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  groups[0] = -comm_size_row / my_cores_per_node_row;
  groups[1] = 0;
  if (cost_explicit(comm_size_row / my_cores_per_node_row,
                    count * type_size * 1e0, 0, 1, 0e0,
                    my_cores_per_node_row * my_cores_per_node_column, num_ports,
                    groups, 0, comm_size_rowb, comm_rank_row, simulate) < 0)
    goto error;
  i = count / (comm_size_row / my_cores_per_node_row);
  if (i < 1) {
    i = 1;
  }
  if (cost_explicit(comm_size_row / my_cores_per_node_row, i * type_size * 1e0,
                    0, 1, 0e0, my_cores_per_node_row * my_cores_per_node_column,
                    num_ports, groups, 1, comm_size_rowb, comm_rank_row,
                    simulate) < 0)
    goto error;
  factors_minimum(comm_size_row / my_cores_per_node_row,
                  my_cores_per_node_row * my_cores_per_node_column, groups);
  for (j = 0; groups[j]; j++) {
    groups[j] *= -1;
  }
  if (cost_explicit(comm_size_row / my_cores_per_node_row,
                    count * type_size * 1e0, 0, 1, 0e0,
                    my_cores_per_node_row * my_cores_per_node_column, num_ports,
                    groups, 0, comm_size_rowb, comm_rank_row, simulate) < 0)
    goto error;
  if (cost_explicit(comm_size_row / my_cores_per_node_row, i * type_size * 1e0,
                    0, 1, 0e0, my_cores_per_node_row * my_cores_per_node_column,
                    num_ports, groups, 1, comm_size_rowb, comm_rank_row,
                    simulate) < 0)
    goto error;
  i = factor_sqrt(comm_size_row / my_cores_per_node_row);
  if ((i > 1) && (i < comm_size_row / my_cores_per_node_row)) {
    groups[0] = -(comm_size_row / my_cores_per_node_row) / i;
    groups[1] = -i;
    groups[2] = 0;
    if (cost_explicit(comm_size_row / my_cores_per_node_row,
                      sqrt(count) * type_size * 1e0, 0, 1, 0e0,
                      my_cores_per_node_row * my_cores_per_node_column,
                      num_ports, groups, 2, comm_size_rowb, comm_rank_row,
                      simulate) < 0)
      goto error;
  }
  free(groups);
  free(num_ports);
  sendbuf = (void *)malloc(count * type_size);
  if (!sendbuf)
    goto error;
  recvbuf = (void *)malloc(count * type_size);
  if (!recvbuf)
    goto error;
  p1 = cost_list_start;
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
    handle = EXT_MPI_Allreduce_init_draft(
        sendbuf, recvbuf, count, datatype, MPI_SUM,
        comm_size_row / my_cores_per_node_row, my_cores_per_node_row,
        comm_size_column / my_cores_per_node_column, my_cores_per_node_column,
        num_ports, groups, my_cores_per_node_row * my_cores_per_node_column, 0,
        ext_mpi_bit_identical);
    if (handle < 0)
      goto error;
    if (EXT_MPI_Count_native(handle, counts, num_steps) < 0)
      goto error;
    if (EXT_MPI_Done_native(handle) < 0)
      goto error;
    p1->T_simulated = 0e0;
    T_step = -1e0;
    p1->nsteps = 0e0;
    p1->nvolume = 0e0;
    for (i = 0; num_steps[i]; i++) {
      r = num_steps[i] + 1;
      mb = counts[i];
      mb /= ext_mpi_file_input[(r - 1 - 1) * ext_mpi_file_input_max_per_core].parallel;
      p1->nsteps += 1e0;
      p1->nvolume += mb;
      j = floor(mb / (ext_mpi_file_input[1].msize - ext_mpi_file_input[0].msize)) - 1;
      k = j + 1;
      if (j < 0) {
        T_step = ext_mpi_file_input[0 + (r - 1 - 1) * ext_mpi_file_input_max_per_core].deltaT;
      } else {
        if (k >= ext_mpi_file_input_max_per_core) {
          T_step = ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                              (r - 1 - 1) * ext_mpi_file_input_max_per_core]
                       .deltaT *
                   mb /
                   ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                              (r - 1 - 1) * ext_mpi_file_input_max_per_core]
                       .msize;
        } else {
          T_step =
              ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core].deltaT +
              (mb -
               ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core].msize) *
                  (ext_mpi_file_input[k + (r - 1 - 1) * ext_mpi_file_input_max_per_core]
                       .deltaT -
                   ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core]
                       .deltaT) /
                  (ext_mpi_file_input[k + (r - 1 - 1) * ext_mpi_file_input_max_per_core].msize -
                   ext_mpi_file_input[j + (r - 1 - 1) * ext_mpi_file_input_max_per_core].msize);
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

int get_num_cores_per_node(MPI_Comm comm) {
  int my_mpi_rank, num_cores, num_cores_min, num_cores_max;
  MPI_Comm comm_node;
  MPI_Info info;
  MPI_Comm_rank(comm, &my_mpi_rank);
  MPI_Info_create(&info);
  MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, my_mpi_rank, info,
                      &comm_node);
  MPI_Info_free(&info);
  MPI_Comm_size(comm_node, &num_cores);
  MPI_Comm_free(&comm_node);
  MPI_Allreduce(&num_cores, &num_cores_min, 1, MPI_INT, MPI_MIN, comm);
  MPI_Allreduce(&num_cores, &num_cores_max, 1, MPI_INT, MPI_MAX, comm);
  if (num_cores_min == num_cores_max) {
    return (num_cores);
  } else {
    return (-1);
  }
}

int EXT_MPI_Init() {
  ext_mpi_read_bench();
  read_env();
  EXT_MPI_Init_native();
  is_initialised = 1;
  return (0);
}

int EXT_MPI_Initialized(int *flag) {
  *flag = is_initialised;
  return (0);
}

int EXT_MPI_Finalize() {
  EXT_MPI_Finalize_native();
  return (0);
}

int EXT_MPI_Allgatherv_init_general(void *sendbuf, int sendcount,
                                    MPI_Datatype sendtype, void *recvbuf,
                                    int *recvcounts, int *displs,
                                    MPI_Datatype recvtype, MPI_Comm comm_row,
                                    int my_cores_per_node_row,
                                    MPI_Comm comm_column,
                                    int my_cores_per_node_column, int *handle) {
  int comm_size_row, *num_ports = NULL, *num_parallel = NULL, type_size, scount,
                     i, alt, rcount;
#ifdef DEBUG
  void *recvbuf_ref = NULL, *sendbuf_org = NULL, *recvbuf_org = NULL;
  int world_rank, comm_rank_row, max_sendcount, max_displs, j, k;
#endif
#ifdef VERBOSE
  int world_rankv;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  num_parallel = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_parallel)
    goto error;
  MPI_Allreduce(&sendcount, &scount, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &scount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (scount * type_size <= 25000000) {
    if (fixed_factors_ports == NULL) {
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           scount * type_size, 1,
                           my_cores_per_node_row * my_cores_per_node_column,
                           num_ports) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           scount * type_size, 1, 12, num_ports) < 0)
          goto error;
      }
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors_ports[i];
      } while (fixed_factors_ports[i] > 0);
    }
    i = 0;
    while (num_ports[i] != 0) {
      num_ports[i]--;
      i++;
    }
#ifdef VERBOSE
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rankv);
    if (world_rankv == 0) {
      printf("# allgatherv parameters %d %d %d %d ports ",
             comm_size_row / my_cores_per_node_row, sendcount * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column);
      i = 0;
      while (num_ports[i] > 0) {
        printf("%d ", num_ports[i]);
        i++;
      }
      printf("\n");
    }
#endif
    for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
      num_parallel[i] = (my_cores_per_node_row * my_cores_per_node_column) /
                        (abs(num_ports[i]) + 1);
      num_ports[i] = -num_ports[i];
    }
    rcount = 0;
    for (i = 0; i < comm_size_row; i++) {
      rcount += recvcounts[i];
    }
    alt = 0;
    if (alternating >= 1) {
      alt = alternating - 1;
    } else {
      alt = (rcount < 10000000);
    }
    *handle = EXT_MPI_Allgatherv_init_native(
        sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
        comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
        num_ports, num_parallel,
        my_cores_per_node_row * my_cores_per_node_column, alt);
    if (*handle < 0)
      goto error;
  }
  free(num_parallel);
  free(num_ports);
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  max_sendcount = recvcounts[0];
  max_displs = 0;
  for (i = 0; i < comm_size_row; i++) {
    if (recvcounts[i] > max_sendcount)
      max_sendcount = recvcounts[i];
    if (displs[i] > max_displs)
      max_displs = displs[i];
  }
  j = max_displs + max_sendcount;
  recvbuf_ref = (void *)malloc(j * type_size);
  if (!recvbuf_ref)
    goto error;
  sendbuf_org = (void *)malloc(sendcount * type_size);
  if (!sendbuf_org)
    goto error;
  recvbuf_org = (void *)malloc(j * type_size);
  if (!recvbuf_org)
    goto error;
  memcpy(sendbuf_org, sendbuf, sendcount * type_size);
  memcpy(recvbuf_org, recvbuf, j * type_size);
  for (i = 0; i < (int)((sendcount * type_size) / sizeof(long int)); i++) {
    ((long int *)sendbuf)[i] = world_rank * max_sendcount + i;
  }
  MPI_Allgatherv(sendbuf, sendcount, sendtype, recvbuf_ref, recvcounts, displs,
                 recvtype, comm_row);
  if (EXT_MPI_Exec_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait_native(*handle) < 0)
    goto error;
  k = 0;
  for (j = 0; j < comm_size_row; j++) {
    for (i = 0; i < (int)((recvcounts[j] * type_size) / sizeof(long int));
         i++) {
      if (((long int *)
               recvbuf)[(displs[j] * type_size) / sizeof(long int) + i] !=
          ((long int *)
               recvbuf_ref)[(displs[j] * type_size) / sizeof(long int) + i]) {
        k = 1;
      }
    }
  }
  if (k) {
    printf("logical error in EXT_MPI_Allgatherv %d\n", world_rank);
    exit(1);
  }
  memcpy(sendbuf, sendbuf_org, sendcount * type_size);
  memcpy(recvbuf, recvbuf_org, j * type_size);
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  return 0;
error:
#ifdef DEBUG
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  free(num_parallel);
  free(num_ports);
  return ERROR_MALLOC;
}

int EXT_MPI_Gatherv_init_general(void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int *recvcounts, int *displs,
                                 MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *handle) {
  int comm_size_row, *num_ports = NULL, *num_parallel = NULL, type_size, scount,
                     i, alt, rcount;
#ifdef DEBUG
  void *recvbuf_ref = NULL, *sendbuf_org = NULL, *recvbuf_org = NULL;
  int world_rank, comm_rank_row, max_sendcount, max_displs, j, k;
#endif
#ifdef VERBOSE
  int world_rankv;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  num_parallel = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_parallel)
    goto error;
  MPI_Allreduce(&sendcount, &scount, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &scount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (scount * type_size <= 25000000) {
    if (fixed_factors_ports == NULL) {
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           scount * type_size, 1,
                           my_cores_per_node_row * my_cores_per_node_column,
                           num_ports) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           scount * type_size, 1, 12, num_ports) < 0)
          goto error;
      }
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors_ports[i];
      } while (fixed_factors_ports[i] > 0);
    }
    i = 0;
    while (num_ports[i] != 0) {
      num_ports[i]--;
      i++;
    }
#ifdef VERBOSE
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rankv);
    if (world_rankv == 0) {
      printf("# gatherv parameters %d %d %d %d ports ",
             comm_size_row / my_cores_per_node_row, sendcount * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column);
      i = 0;
      while (num_ports[i] > 0) {
        printf("%d ", num_ports[i]);
        i++;
      }
      printf("\n");
    }
#endif
    for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
      num_parallel[i] = (my_cores_per_node_row * my_cores_per_node_column) /
                        (abs(num_ports[i]) + 1);
      num_ports[i] = -num_ports[i];
    }
    rcount = 0;
    for (i = 0; i < comm_size_row; i++) {
      rcount += recvcounts[i];
    }
    alt = 0;
    if (alternating >= 1) {
      alt = alternating - 1;
    } else {
      alt = (rcount < 10000000);
    }
    *handle = EXT_MPI_Gatherv_init_native(
        sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
        root, comm_row, my_cores_per_node_row, comm_column,
        my_cores_per_node_column, num_ports, num_parallel,
        my_cores_per_node_row * my_cores_per_node_column, alt);
    if (*handle < 0)
      goto error;
  }
  free(num_parallel);
  free(num_ports);
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  max_sendcount = recvcounts[0];
  max_displs = 0;
  for (i = 0; i < comm_size_row; i++) {
    if (recvcounts[i] > max_sendcount)
      max_sendcount = recvcounts[i];
    if (displs[i] > max_displs)
      max_displs = displs[i];
  }
  j = max_displs + max_sendcount;
  recvbuf_ref = (void *)malloc(j * type_size);
  if (!recvbuf_ref)
    goto error;
  sendbuf_org = (void *)malloc(sendcount * type_size);
  if (!sendbuf_org)
    goto error;
  recvbuf_org = (void *)malloc(j * type_size);
  if (!recvbuf_org)
    goto error;
  memcpy(sendbuf_org, sendbuf, sendcount * type_size);
  memcpy(recvbuf_org, recvbuf, j * type_size);
  for (i = 0; i < (int)((sendcount * type_size) / sizeof(long int)); i++) {
    ((long int *)sendbuf)[i] = world_rank * max_sendcount + i;
  }
  MPI_Gatherv(sendbuf, sendcount, sendtype, recvbuf_ref, recvcounts, displs,
              recvtype, root, comm_row);
  if (EXT_MPI_Exec_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait_native(*handle) < 0)
    goto error;
  if (root == comm_rank_row) {
    k = 0;
    for (j = 0; j < comm_size_row; j++) {
      for (i = 0; i < (int)((recvcounts[j] * type_size) / sizeof(long int));
           i++) {
        if (((long int *)
                 recvbuf)[(displs[j] * type_size) / sizeof(long int) + i] !=
            ((long int *)
                 recvbuf_ref)[(displs[j] * type_size) / sizeof(long int) + i]) {
          k = 1;
        }
      }
    }
    if (k) {
      printf("logical error in EXT_MPI_Allgatherv %d\n", world_rank);
      exit(1);
    }
  }
  memcpy(sendbuf, sendbuf_org, sendcount * type_size);
  memcpy(recvbuf, recvbuf_org, j * type_size);
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  return 0;
error:
#ifdef DEBUG
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  free(num_parallel);
  free(num_ports);
  return ERROR_MALLOC;
}

int EXT_MPI_Allgather_init_general(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle) {
  int iret, mpi_size, *recvcounts, *displs, i;
  MPI_Comm_size(comm_row, &mpi_size);
  recvcounts = (int *)malloc(mpi_size * sizeof(int));
  displs = (int *)malloc(mpi_size * sizeof(int));
  for (i = 0; i < mpi_size; i++) {
    recvcounts[i] = recvcount;
  }
  displs[0] = 0;
  for (i = 0; i < mpi_size - 1; i++) {
    displs[i + 1] = displs[i] + recvcounts[i];
  }
  iret = EXT_MPI_Allgatherv_init_general(sendbuf, sendcount, sendtype, recvbuf,
                                         recvcounts, displs, recvtype, comm_row,
                                         my_cores_per_node_row, comm_column,
                                         my_cores_per_node_column, handle);
  free(displs);
  free(recvcounts);
  return (iret);
}

int EXT_MPI_Reduce_scatter_init_general(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle) {
  int comm_size_row, *num_ports = NULL, *num_parallel = NULL, type_size, rcount,
                     i, j, k, cin_method, alt;
#ifdef VERBOSE
  int world_rankv;
#endif
#ifdef DEBUG
  void *recvbuf_ref = NULL, *sendbuf_org = NULL, *recvbuf_org = NULL;
  int world_rank, comm_rank_row, tsize;
  if ((op != MPI_SUM) || (datatype != MPI_LONG)) {
    if (EXT_MPI_Reduce_scatter_init_general(
            sendbuf, recvbuf, recvcounts, MPI_LONG, MPI_SUM, comm_row,
            my_cores_per_node_row, comm_column, my_cores_per_node_column,
            handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
  }
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(datatype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  num_parallel = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_parallel)
    goto error;
  rcount = 0;
  for (i = 0; i < comm_size_row; i++) {
    if (recvcounts[i] > rcount) {
      rcount = recvcounts[i];
    }
  }
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &rcount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (rcount * type_size <= 25000000) {
    if (fixed_factors_ports == NULL) {
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           rcount * type_size, 1,
                           my_cores_per_node_row * my_cores_per_node_column,
                           num_ports) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           rcount * type_size, 1, 12, num_ports) < 0)
          goto error;
      }
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors_ports[i];
      } while (fixed_factors_ports[i] > 0);
    }
    i = 0;
    while (num_ports[i] != 0) {
      num_ports[i]--;
      i++;
    }
#ifdef VERBOSE
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rankv);
    if (world_rankv == 0) {
      i = recvcounts[0];
      printf("# reduce_scatter parameters %d %d %d %d ports ",
             comm_size_row / my_cores_per_node_row, i * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column);
      i = 0;
      while (num_ports[i] > 0) {
        printf("%d ", num_ports[i]);
        i++;
      }
      printf("\n");
    }
#endif
    for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
      num_parallel[i] = (my_cores_per_node_row * my_cores_per_node_column) /
                        (num_ports[i] + 1);
      num_parallel[i] = 1;
    }
    for (i = 0; num_ports[i]; i++) {
    }
    for (j = 0; j < i / 2; j++) {
      k = num_ports[j];
      num_ports[j] = num_ports[i - 1 - j];
      num_ports[i - 1 - j] = k;
    }
    cin_method = 0;
    if (copyin_method >= 1) {
      cin_method = copyin_method - 1;
    } else {
      rcount = 0;
      for (i = 0; i < comm_size_row; i++) {
        rcount += recvcounts[i];
      }
      if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
        cin_method = (rcount * type_size >
                      ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
      } else {
        cin_method = (rcount * type_size >
                      ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
      }
    }
    alt = 0;
    if (alternating >= 1) {
      alt = alternating - 1;
    } else {
      alt = (rcount < 10000000);
    }
    *handle = EXT_MPI_Reduce_scatter_init_native(
        sendbuf, recvbuf, recvcounts, datatype, op, comm_row,
        my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
        num_parallel, my_cores_per_node_row * my_cores_per_node_column,
        cin_method, alt);
    if (*handle < 0)
      goto error;
  }
  free(num_parallel);
  free(num_ports);
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    tsize = 0;
    for (i = 0; i < comm_size_row; i++) {
      tsize += recvcounts[i];
    }
    recvbuf_ref =
        (long int *)malloc(recvcounts[comm_rank_row] * sizeof(long int));
    if (!recvbuf_ref)
      goto error;
    sendbuf_org = malloc(tsize * sizeof(long int));
    if (!sendbuf_org)
      goto error;
    recvbuf_org = malloc(recvcounts[comm_rank_row] * sizeof(long int));
    if (!recvbuf_org)
      goto error;
    memcpy(sendbuf_org, sendbuf, tsize * sizeof(long int));
    memcpy(recvbuf_org, recvbuf, recvcounts[comm_rank_row] * sizeof(long int));
    for (i = 0; i < tsize; i++) {
      ((long int *)sendbuf)[i] = world_rank * tsize + i;
    }
    MPI_Reduce_scatter(sendbuf, recvbuf_ref, recvcounts, MPI_LONG, MPI_SUM,
                       comm_row);
    if (EXT_MPI_Exec_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    k = recvcounts[comm_rank_row];
    j = 0;
    for (i = 0; i < k; i++) {
      if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
        j = 1;
      }
    }
    if (j) {
      printf("logical error in EXT_MPI_Reduce_scatter %d\n", world_rank);
      exit(1);
    }
    memcpy(recvbuf, recvbuf_org, recvcounts[comm_rank_row] * sizeof(long int));
    memcpy(sendbuf, sendbuf_org, tsize * sizeof(long int));
    free(recvbuf_org);
    free(sendbuf_org);
    free(recvbuf_ref);
  }
#endif
  return 0;
error:
#ifdef DEBUG
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  free(num_parallel);
  free(num_ports);
  return ERROR_MALLOC;
}

int EXT_MPI_Scatterv_init_general(void *sendbuf, int *sendcounts, int *displs,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  int root, MPI_Comm comm_row,
                                  int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *handle) {
  int comm_size_row, *num_ports = NULL, *num_parallel = NULL, type_size, rcount,
                     i, j, k, cin_method, alt;
#ifdef VERBOSE
  int world_rankv;
#endif
#ifdef DEBUG
  void *recvbuf_ref = NULL, *sendbuf_org = NULL, *recvbuf_org = NULL;
  int world_rank, comm_rank_row;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  num_parallel = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_parallel)
    goto error;
  rcount = recvcount;
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &rcount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (rcount * type_size <= 25000000) {
    if (fixed_factors_ports == NULL) {
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           rcount * type_size, 1,
                           my_cores_per_node_row * my_cores_per_node_column,
                           num_ports) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                           rcount * type_size, 1, 12, num_ports) < 0)
          goto error;
      }
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors_ports[i];
      } while (fixed_factors_ports[i] > 0);
    }
    i = 0;
    while (num_ports[i] != 0) {
      num_ports[i]--;
      i++;
    }
#ifdef VERBOSE
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rankv);
    if (world_rankv == 0) {
      printf("# scatterv parameters %d %d %d %d ports ",
             comm_size_row / my_cores_per_node_row, recvcount * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column);
      i = 0;
      while (num_ports[i] > 0) {
        printf("%d ", num_ports[i]);
        i++;
      }
      printf("\n");
    }
#endif
    for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
      num_parallel[i] = (my_cores_per_node_row * my_cores_per_node_column) /
                        (num_ports[i] + 1);
      num_parallel[i] = 1;
    }
    for (i = 0; num_ports[i]; i++) {
    }
    for (j = 0; j < i / 2; j++) {
      k = num_ports[j];
      num_ports[j] = num_ports[i - 1 - j];
      num_ports[i - 1 - j] = k;
    }
    cin_method = 0;
    if (copyin_method >= 1) {
      cin_method = copyin_method - 1;
    } else {
      rcount = 0;
      for (i = 0; i < comm_size_row; i++) {
        rcount += sendcounts[i];
      }
      if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
        cin_method = (rcount * type_size >
                      ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
      } else {
        cin_method = (rcount * type_size >
                      ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
      }
    }
    alt = 0;
    if (alternating >= 1) {
      alt = alternating - 1;
    } else {
      alt = (rcount < 10000000);
    }
    *handle = EXT_MPI_Scatterv_init_native(
        sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype,
        root, comm_row, my_cores_per_node_row, comm_column,
        my_cores_per_node_column, num_ports, num_parallel,
        my_cores_per_node_row * my_cores_per_node_column, cin_method, alt);
    if (*handle < 0)
      goto error;
  }
  free(num_parallel);
  free(num_ports);
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  recvbuf_ref = (long int *)malloc(recvcount * type_size);
  if (!recvbuf_ref)
    goto error;
  sendbuf_org = malloc(
      (displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) * type_size);
  if (!sendbuf_org)
    goto error;
  recvbuf_org = malloc(recvcount * type_size);
  if (!recvbuf_org)
    goto error;
  memcpy(sendbuf_org, sendbuf,
         (displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) *
             type_size);
  memcpy(recvbuf_org, recvbuf, recvcount * type_size);
  k = 0;
  for (i = 0; i < comm_size_row; i++) {
    for (j = 0; j < (sendcounts[i] * type_size) / sizeof(long int); j++) {
      ((long int *)((char *)sendbuf + displs[i] * type_size))[j] =
          world_rank *
              (((displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) *
                type_size) /
               sizeof(long int)) +
          k++;
    }
  }
  MPI_Scatterv(sendbuf, sendcounts, displs, MPI_LONG, recvbuf_ref, recvcount,
               MPI_LONG, root, comm_row);
  if (EXT_MPI_Exec_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait_native(*handle) < 0)
    goto error;
  j = 0;
  for (i = 0; i < (recvcount * type_size) / sizeof(long int); i++) {
    if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
      j = 1;
    }
  }
  if (j) {
    printf("logical error in EXT_MPI_Scatterv %d\n", world_rank);
    exit(1);
  }
  memcpy(recvbuf, recvbuf_org, recvcount * type_size);
  memcpy(sendbuf, sendbuf_org,
         (displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) *
             type_size);
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  return 0;
error:
#ifdef DEBUG
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  free(num_parallel);
  free(num_ports);
  return ERROR_MALLOC;
}

int EXT_MPI_Reduce_scatter_block_init_general(
    void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle) {
  int iret, *recvcounts, mpi_size, i;
  MPI_Comm_size(comm_row, &mpi_size);
  recvcounts = (int *)malloc(mpi_size * sizeof(int));
  for (i = 0; i < mpi_size; i++) {
    recvcounts[i] = recvcount;
  }
  iret = EXT_MPI_Reduce_scatter_init_general(
      sendbuf, recvbuf, recvcounts, datatype, op, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
  free(recvcounts);
  return (iret);
}

int EXT_MPI_Allreduce_init_general(void *sendbuf, void *recvbuf, int count,
                                   MPI_Datatype datatype, MPI_Op op,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle) {
  int comm_size_row, comm_rank_row, i, cin_method, alt, comm_size_column;
  int message_size, type_size;
  int *num_ports = NULL, *groups = NULL;
  double d1;
  struct cost_list *p1, *p2;
  struct {
    double value;
    int rank;
  } composition;
#ifdef VERBOSE
  int world_rank;
#endif
#ifdef DEBUG
  int j;
#ifdef GPU_ENABLED
  void *sendbuf_h = NULL, *recvbuf_h = NULL, *recvbuf_ref_h = NULL;
#endif
  int world_rankd;
  void *recvbuf_ref = NULL, *sendbuf_org = NULL, *recvbuf_org = NULL;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
#ifdef DEBUG
  if ((op != MPI_SUM) || (datatype != MPI_LONG)) {
    if (EXT_MPI_Allreduce_init_general(
            sendbuf, recvbuf, (count * type_size) / sizeof(long int), MPI_LONG,
            MPI_SUM, comm_row, my_cores_per_node_row, comm_column,
            my_cores_per_node_column, handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
  }
#endif
  if (sendbuf == MPI_IN_PLACE) {
    sendbuf = recvbuf;
  }
  message_size = type_size * count;
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &message_size, 1, MPI_INT, MPI_SUM,
                  comm_column);
    MPI_Comm_size(comm_column, &comm_size_column);
  } else {
    comm_size_column = 1;
  }
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (i = 0; i < comm_size_row; i++) {
    num_ports[i] = groups[i] = 0;
  }
  if (fixed_factors_ports == NULL) {
    /*    d1 = ext_mpi_cost_simple_recursive(comm_size_row/my_cores_per_node_row, message_size,
       1, my_cores_per_node_row*my_cores_per_node_column, num_ports); i =
       message_size/(comm_size_row/my_cores_per_node_row); if (i<type_size){
          i=type_size;
        }
        d2 = 2*ext_mpi_cost_simple_recursive(comm_size_row/my_cores_per_node_row, i, 1,
       my_cores_per_node_row*my_cores_per_node_column, num_ports); j=(d1<d2); if
       (j){ ext_mpi_cost_simple_recursive(comm_size_row/my_cores_per_node_row, message_size, 1,
       my_cores_per_node_row*my_cores_per_node_column, num_ports);
        }
        i = 0;
        while (num_ports[i]!=0){
          num_ports[i]--;
          i++;
        }
        for (i=0; num_ports[i]; i++){}
        if (!j){
          for (j=0; j<i; j++){
            num_ports[j+i]=-num_ports[j];
          }
          for (j=0; j<i/2; j++){
            k=num_ports[j]; num_ports[j]=num_ports[i-1-j]; num_ports[i-1-j]=k;
          }
          num_ports[2*i]=0;
        }else{
          for (j=0; j<i; j++){
            num_ports[j]=-num_ports[j];
          }
        }
        for (i=0; num_ports[i]; i++){
          groups[i] = comm_size_row/my_cores_per_node_row;
        }
        groups[i-1] = -groups[i-1];*/
    if (comm_size_row / my_cores_per_node_row > 1) {
      if (my_cores_per_node_row == 1) {
        if (cost_simulate(count, datatype, comm_size_row * 12, 12,
                          comm_size_column, my_cores_per_node_column,
                          comm_size_row, comm_rank_row, 0) < 0)
          goto error;
      } else {
        if (cost_simulate(count, datatype, comm_size_row, my_cores_per_node_row,
                          comm_size_column, my_cores_per_node_column,
                          comm_size_row, comm_rank_row, 0) < 0)
          goto error;
      }
      p1 = cost_list_start;
      if (p1) {
        d1 = p1->T_simulated;
        for (i = 0; p1->rarray[i]; i++) {
          num_ports[i] = p1->rarray[i];
          groups[i] = p1->garray[i];
        }
        groups[i] = num_ports[i] = 0;
      } else {
        d1 = 1e90;
      }
      while (p1) {
        if (p1->T_simulated < d1) {
          d1 = p1->T_simulated;
          for (i = 0; p1->rarray[i]; i++) {
            num_ports[i] = p1->rarray[i];
            groups[i] = p1->garray[i];
          }
          groups[i] = num_ports[i] = 0;
        }
        p2 = p1;
        p1 = p1->next;
        free(p2->garray);
        free(p2->rarray);
        free(p2);
      }
      cost_list_start = NULL;
      cost_list_length = 0;
      cost_list_counter = 0;
      composition.value = d1;
      composition.rank = comm_rank_row;
      MPI_Allreduce(MPI_IN_PLACE, &composition, 1, MPI_DOUBLE_INT, MPI_MINLOC,
                    comm_row);
      d1 = composition.value;
      for (i = 0; num_ports[i]; i++)
        ;
      MPI_Bcast(&i, 1, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(num_ports, i, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(groups, i, MPI_INT, composition.rank, comm_row);
      groups[i] = num_ports[i] = 0;
    } else {
      groups[0] = num_ports[0] = -1;
      groups[1] = num_ports[1] = 0;
    }
    // FIXME comm_column
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
      groups[i] = fixed_factors_groups[i];
    } while (fixed_factors_ports[i]);
  }
#ifdef VERBOSE
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  if (world_rank == 0) {
    printf("# allreduce parameters %d %d %d %d ports ",
           comm_size_row / my_cores_per_node_row, count * message_size, 1,
           my_cores_per_node_row * my_cores_per_node_column);
    i = 0;
    while (num_ports[i]) {
      printf("%d ", num_ports[i]);
      i++;
    }
    printf("groups ");
    i = 0;
    while (groups[i]) {
      printf("%d ", groups[i]);
      i++;
    }
    printf("\n");
  }
#endif
  cin_method = 0;
  if (copyin_method >= 1) {
    cin_method = copyin_method - 1;
  } else {
    if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      cin_method = (count * type_size >
                    ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      cin_method =
          (count * type_size > ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = (count * type_size < 10000000);
  }
  *handle = EXT_MPI_Allreduce_init_native(
      sendbuf, recvbuf, count, datatype, op, comm_row, my_cores_per_node_row,
      comm_column, my_cores_per_node_column, num_ports, groups,
      my_cores_per_node_row * my_cores_per_node_column, cin_method, alt,
      ext_mpi_bit_identical);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
#ifdef GPU_ENABLED
    if (gpu_is_device_pointer(sendbuf)) {
      gpu_malloc(&recvbuf_ref, count * type_size);
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      recvbuf_h = (long int *)malloc(count * type_size);
      if (!recvbuf_h)
        goto error;
      recvbuf_ref_h = (long int *)malloc(count * type_size);
      if (!recvbuf_ref_h)
        goto error;
      sendbuf_org = (long int *)malloc(count * type_size);
      if (!sendbuf_org)
        goto error;
      recvbuf_org = (long int *)malloc(count * type_size);
      if (!recvbuf_org)
        goto error;
      gpu_memcpy_dh(sendbuf_org, sendbuf, count * type_size);
      gpu_memcpy_dh(recvbuf_org, recvbuf, count * type_size);
      for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rankd * count + i;
      }
      gpu_memcpy_hd(sendbuf, sendbuf_h, count * type_size);
      MPI_Allreduce(sendbuf, recvbuf_ref, count, MPI_LONG, MPI_SUM, comm_row);
      if (EXT_MPI_Exec_native(*handle) < 0)
        goto error;
      if (EXT_MPI_Wait_native(*handle) < 0)
        goto error;
      gpu_memcpy_dh(recvbuf_h, recvbuf, count * type_size);
      gpu_memcpy_dh(recvbuf_ref_h, recvbuf_ref, count * type_size);
      j = 0;
      for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
        if (((long int *)recvbuf_h)[i] != ((long int *)recvbuf_ref_h)[i]) {
          j = 1;
        }
      }
      gpu_memcpy_hd(recvbuf, recvbuf_org, count * type_size);
      gpu_memcpy_hd(sendbuf, sendbuf_org, count * type_size);
      free(sendbuf_org);
      free(recvbuf_org);
      free(recvbuf_ref_h);
      free(recvbuf_h);
      free(sendbuf_h);
      gpu_free(recvbuf_ref);
    } else {
#endif
      recvbuf_ref = (long int *)malloc(count * type_size);
      if (!recvbuf_ref)
        goto error;
      sendbuf_org = (long int *)malloc(count * type_size);
      if (!sendbuf_org)
        goto error;
      recvbuf_org = (long int *)malloc(count * type_size);
      if (!recvbuf_org)
        goto error;
      memcpy(sendbuf_org, sendbuf, count * type_size);
      memcpy(recvbuf_org, recvbuf, count * type_size);
      for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
        ((long int *)sendbuf)[i] = world_rankd * count + i;
      }
      MPI_Allreduce(sendbuf, recvbuf_ref,
                    (count * type_size) / sizeof(long int), MPI_LONG, MPI_SUM,
                    comm_row);
      if (EXT_MPI_Exec_native(*handle) < 0)
        goto error;
      if (EXT_MPI_Wait_native(*handle) < 0)
        goto error;
      j = 0;
      for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
        if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
          j = 1;
        }
      }
      memcpy(recvbuf, recvbuf_org, count * type_size);
      memcpy(sendbuf, sendbuf_org, count * type_size);
      free(recvbuf_org);
      free(sendbuf_org);
      free(recvbuf_ref);
#ifdef GPU_ENABLED
    }
#endif
    if (j) {
      printf("logical error in EXT_MPI_Allreduce %d\n", world_rankd);
      exit(1);
    }
  }
#endif
  return 0;
error:
#ifdef DEBUG
#ifdef GPU_ENABLED
  free(sendbuf_org);
  free(recvbuf_org);
  free(recvbuf_ref_h);
  free(recvbuf_h);
  free(sendbuf_h);
#endif
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  return ERROR_MALLOC;
}

int EXT_MPI_Reduce_init_general(void *sendbuf, void *recvbuf, int count,
                                MPI_Datatype datatype, MPI_Op op, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle) {
  int comm_size_row, comm_rank_row, i, cin_method, alt, comm_size_column;
  int message_size, type_size;
  int *num_ports = NULL, *groups = NULL;
  double d1;
  struct cost_list *p1, *p2;
  struct {
    double value;
    int rank;
  } composition;
#ifdef VERBOSE
  int world_rank;
#endif
#ifdef DEBUG
  int j;
#ifdef GPU_ENABLED
  void *sendbuf_h = NULL, *recvbuf_h = NULL, *recvbuf_ref_h = NULL;
#endif
  int world_rankd;
  void *recvbuf_ref = NULL, *sendbuf_org = NULL, *recvbuf_org = NULL;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
#ifdef DEBUG
  if ((op != MPI_SUM) || (datatype != MPI_LONG)) {
    if (EXT_MPI_Reduce_init_general(
            sendbuf, recvbuf, (count * type_size) / sizeof(long int), MPI_LONG,
            MPI_SUM, root, comm_row, my_cores_per_node_row, comm_column,
            my_cores_per_node_column, handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
  }
#endif
  if (sendbuf == MPI_IN_PLACE) {
    sendbuf = recvbuf;
  }
  message_size = type_size * count;
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &message_size, 1, MPI_INT, MPI_SUM,
                  comm_column);
    MPI_Comm_size(comm_column, &comm_size_column);
  } else {
    comm_size_column = 1;
  }
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (i = 0; i < comm_size_row; i++) {
    num_ports[i] = groups[i] = 0;
  }
  if (fixed_factors_ports == NULL) {
    if (comm_size_row / my_cores_per_node_row > 1) {
      if (my_cores_per_node_row == 1) {
        if (cost_simulate(count, datatype, comm_size_row * 12, 12,
                          comm_size_column, my_cores_per_node_column,
                          comm_size_row, comm_rank_row, 0) < 0)
          goto error;
      } else {
        if (cost_simulate(count, datatype, comm_size_row, my_cores_per_node_row,
                          comm_size_column, my_cores_per_node_column,
                          comm_size_row, comm_rank_row, 0) < 0)
          goto error;
      }
      p1 = cost_list_start;
      if (p1) {
        d1 = p1->T_simulated;
        for (i = 0; p1->rarray[i]; i++) {
          num_ports[i] = p1->rarray[i];
          groups[i] = p1->garray[i];
        }
        groups[i] = num_ports[i] = 0;
      } else {
        d1 = 1e90;
      }
      while (p1) {
        if (p1->T_simulated < d1) {
          d1 = p1->T_simulated;
          for (i = 0; p1->rarray[i]; i++) {
            num_ports[i] = p1->rarray[i];
            groups[i] = p1->garray[i];
          }
          groups[i] = num_ports[i] = 0;
        }
        p2 = p1;
        p1 = p1->next;
        free(p2->garray);
        free(p2->rarray);
        free(p2);
      }
      cost_list_start = NULL;
      cost_list_length = 0;
      cost_list_counter = 0;
      composition.value = d1;
      composition.rank = comm_rank_row;
      MPI_Allreduce(MPI_IN_PLACE, &composition, 1, MPI_DOUBLE_INT, MPI_MINLOC,
                    comm_row);
      d1 = composition.value;
      for (i = 0; num_ports[i]; i++)
        ;
      MPI_Bcast(&i, 1, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(num_ports, i, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(groups, i, MPI_INT, composition.rank, comm_row);
      groups[i] = num_ports[i] = 0;
    } else {
      groups[0] = num_ports[0] = -1;
      groups[1] = num_ports[1] = 0;
    }
    // FIXME comm_column
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
      groups[i] = fixed_factors_groups[i];
    } while (fixed_factors_ports[i]);
  }
#ifdef VERBOSE
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  if (world_rank == 0) {
    printf("# reduce parameters %d %d %d %d ports ",
           comm_size_row / my_cores_per_node_row, count * message_size, 1,
           my_cores_per_node_row * my_cores_per_node_column);
    i = 0;
    while (num_ports[i]) {
      printf("%d ", num_ports[i]);
      i++;
    }
    printf("groups ");
    i = 0;
    while (groups[i]) {
      printf("%d ", groups[i]);
      i++;
    }
    printf("\n");
  }
#endif
  cin_method = 0;
  if (copyin_method >= 1) {
    cin_method = copyin_method - 1;
  } else {
    if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      cin_method = (count * type_size >
                    ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      cin_method =
          (count * type_size > ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = (count * type_size < 10000000);
  }
  *handle = EXT_MPI_Reduce_init_native(
      sendbuf, recvbuf, count, datatype, op, root, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, my_cores_per_node_row * my_cores_per_node_column, cin_method, alt,
      0);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
#ifdef GPU_ENABLED
    if (gpu_is_device_pointer(sendbuf)) {
      gpu_malloc(&recvbuf_ref, count * type_size);
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      recvbuf_h = (long int *)malloc(count * type_size);
      if (!recvbuf_h)
        goto error;
      recvbuf_ref_h = (long int *)malloc(count * type_size);
      if (!recvbuf_ref_h)
        goto error;
      sendbuf_org = (long int *)malloc(count * type_size);
      if (!sendbuf_org)
        goto error;
      recvbuf_org = (long int *)malloc(count * type_size);
      if (!recvbuf_org)
        goto error;
      gpu_memcpy_dh(sendbuf_org, sendbuf, count * type_size);
      gpu_memcpy_dh(recvbuf_org, recvbuf, count * type_size);
      for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rankd * count + i;
      }
      gpu_memcpy_hd(sendbuf, sendbuf_h, count * type_size);
      MPI_Reduce(sendbuf, recvbuf_ref, count, MPI_LONG, MPI_SUM, root,
                 comm_row);
      EXT_MPI_Exec_native(*handle);
      EXT_MPI_Wait_native(*handle);
      gpu_memcpy_dh(recvbuf_h, recvbuf, count * type_size);
      gpu_memcpy_dh(recvbuf_ref_h, recvbuf_ref, count * type_size);
      j = 0;
      if (root == comm_rank_row) {
        for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
          if (((long int *)recvbuf_h)[i] != ((long int *)recvbuf_ref_h)[i]) {
            j = 1;
          }
        }
      }
      gpu_memcpy_hd(recvbuf, recvbuf_org, count * type_size);
      gpu_memcpy_hd(sendbuf, sendbuf_org, count * type_size);
      free(sendbuf_org);
      free(recvbuf_org);
      free(recvbuf_ref_h);
      free(recvbuf_h);
      free(sendbuf_h);
      gpu_free(recvbuf_ref);
    } else {
#endif
      recvbuf_ref = (long int *)malloc(count * type_size);
      if (!recvbuf_ref)
        goto error;
      sendbuf_org = (long int *)malloc(count * type_size);
      if (!sendbuf_org)
        goto error;
      recvbuf_org = (long int *)malloc(count * type_size);
      if (!recvbuf_org)
        goto error;
      memcpy(sendbuf_org, sendbuf, count * type_size);
      memcpy(recvbuf_org, recvbuf, count * type_size);
      for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
        ((long int *)sendbuf)[i] = world_rankd * count + i;
      }
      MPI_Reduce(sendbuf, recvbuf_ref, count, MPI_LONG, MPI_SUM, root,
                 comm_row);
      if (EXT_MPI_Exec_native(*handle) < 0)
        goto error;
      if (EXT_MPI_Wait_native(*handle) < 0)
        goto error;
      j = 0;
      if (root == comm_rank_row) {
        for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
          if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
            j = 1;
          }
        }
      }
      memcpy(recvbuf, recvbuf_org, count * type_size);
      memcpy(sendbuf, sendbuf_org, count * type_size);
      free(recvbuf_org);
      free(sendbuf_org);
      free(recvbuf_ref);
#ifdef GPU_ENABLED
    }
#endif
    if (j) {
      printf("logical error in EXT_MPI_Reduce %d\n", world_rankd);
      exit(1);
    }
  }
#endif
  return 0;
error:
#ifdef DEBUG
#ifdef GPU_ENABLED
  free(recvbuf_ref_h);
  free(recvbuf_h);
  free(sendbuf_h);
#endif
  free(recvbuf_org);
  free(sendbuf_org);
  free(recvbuf_ref);
#endif
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}

int EXT_MPI_Bcast_init_general(void *buffer, int count, MPI_Datatype datatype,
                               int root, MPI_Comm comm_row,
                               int my_cores_per_node_row, MPI_Comm comm_column,
                               int my_cores_per_node_column, int *handle) {
  int comm_size_row, comm_rank_row, i, cin_method, alt, comm_size_column;
  int message_size, type_size;
  int *num_ports = NULL, *groups = NULL;
  double d1;
  struct cost_list *p1, *p2;
  struct {
    double value;
    int rank;
  } composition;
#ifdef VERBOSE
  int world_rank;
#endif
#ifdef DEBUG
  int j;
#ifdef GPU_ENABLED
  void *sendbuf_h = NULL, *recvbuf_h = NULL, *recvbuf_ref_h = NULL;
#endif
  int world_rankd;
  void *buffer_ref = NULL, *buffer_org = NULL;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  message_size = type_size * count;
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &message_size, 1, MPI_INT, MPI_SUM,
                  comm_column);
    MPI_Comm_size(comm_column, &comm_size_column);
  } else {
    comm_size_column = 1;
  }
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (i = 0; i < comm_size_row; i++) {
    num_ports[i] = groups[i] = 0;
  }
  if (fixed_factors_ports == NULL) {
    /*    d1 = ext_mpi_cost_simple_recursive(comm_size_row/my_cores_per_node_row, message_size,
       1, my_cores_per_node_row*my_cores_per_node_column, num_ports); i =
       message_size/(comm_size_row/my_cores_per_node_row); if (i<type_size){
          i=type_size;
        }
        d2 = 2*ext_mpi_cost_simple_recursive(comm_size_row/my_cores_per_node_row, i, 1,
       my_cores_per_node_row*my_cores_per_node_column, num_ports); j=(d1<d2); if
       (j){ ext_mpi_cost_simple_recursive(comm_size_row/my_cores_per_node_row, message_size, 1,
       my_cores_per_node_row*my_cores_per_node_column, num_ports);
        }
        i = 0;
        while (num_ports[i]!=0){
          num_ports[i]--;
          i++;
        }
        for (i=0; num_ports[i]; i++){}
        if (!j){
          for (j=0; j<i; j++){
            num_ports[j+i]=-num_ports[j];
          }
          for (j=0; j<i/2; j++){
            k=num_ports[j]; num_ports[j]=num_ports[i-1-j]; num_ports[i-1-j]=k;
          }
          num_ports[2*i]=0;
        }else{
          for (j=0; j<i; j++){
            num_ports[j]=-num_ports[j];
          }
        }
        for (i=0; num_ports[i]; i++){
          groups[i] = comm_size_row/my_cores_per_node_row;
        }
        groups[i-1] = -groups[i-1];*/
    if (comm_size_row / my_cores_per_node_row > 1) {
      if (my_cores_per_node_row == 1) {
        if (cost_simulate(count, datatype, comm_size_row * 12, 12,
                          comm_size_column, my_cores_per_node_column,
                          comm_size_row, comm_rank_row, 0) < 0)
          goto error;
      } else {
        if (cost_simulate(count, datatype, comm_size_row, my_cores_per_node_row,
                          comm_size_column, my_cores_per_node_column,
                          comm_size_row, comm_rank_row, 0) < 0)
          goto error;
      }
      p1 = cost_list_start;
      if (p1) {
        d1 = p1->T_simulated;
        for (i = 0; p1->rarray[i]; i++) {
          num_ports[i] = p1->rarray[i];
          groups[i] = p1->garray[i];
        }
        groups[i] = num_ports[i] = 0;
      } else {
        d1 = 1e90;
      }
      while (p1) {
        if (p1->T_simulated < d1) {
          d1 = p1->T_simulated;
          for (i = 0; p1->rarray[i]; i++) {
            num_ports[i] = p1->rarray[i];
            groups[i] = p1->garray[i];
          }
          groups[i] = num_ports[i] = 0;
        }
        p2 = p1;
        p1 = p1->next;
        free(p2->garray);
        free(p2->rarray);
        free(p2);
      }
      cost_list_start = NULL;
      cost_list_length = 0;
      cost_list_counter = 0;
      composition.value = d1;
      composition.rank = comm_rank_row;
      MPI_Allreduce(MPI_IN_PLACE, &composition, 1, MPI_DOUBLE_INT, MPI_MINLOC,
                    comm_row);
      d1 = composition.value;
      for (i = 0; num_ports[i]; i++)
        ;
      MPI_Bcast(&i, 1, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(num_ports, i, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(groups, i, MPI_INT, composition.rank, comm_row);
      groups[i] = num_ports[i] = 0;
    } else {
      groups[0] = num_ports[0] = -1;
      groups[1] = num_ports[1] = 0;
    }
    // FIXME comm_column
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
      groups[i] = fixed_factors_groups[i];
    } while (fixed_factors_ports[i]);
  }
#ifdef VERBOSE
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  if (world_rank == 0) {
    printf("# bcast parameters %d %d %d %d ports ",
           comm_size_row / my_cores_per_node_row, message_size, 1,
           my_cores_per_node_row * my_cores_per_node_column);
    i = 0;
    while (num_ports[i]) {
      printf("%d ", num_ports[i]);
      i++;
    }
    printf("groups ");
    i = 0;
    while (groups[i]) {
      printf("%d ", groups[i]);
      i++;
    }
    printf("\n");
  }
#endif
  cin_method = 0;
  if (copyin_method >= 1) {
    cin_method = copyin_method - 1;
  } else {
    if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      cin_method = (count * type_size >
                    ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      cin_method =
          (count * type_size > ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = (count * type_size < 10000000);
  }
  *handle = EXT_MPI_Bcast_init_native(
      buffer, count, datatype, root, comm_row, my_cores_per_node_row,
      comm_column, my_cores_per_node_column, num_ports, groups,
      my_cores_per_node_row * my_cores_per_node_column, cin_method, alt);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(sendbuf)) {
    gpu_malloc(&recvbuf_ref, count * type_size);
    sendbuf_h = (long int *)malloc(count * type_size);
    if (!sendbuf_h)
      goto error;
    recvbuf_h = (long int *)malloc(count * type_size);
    if (!recvbuf_h)
      goto error;
    recvbuf_ref_h = (long int *)malloc(count * type_size);
    if (!recvbuf_ref_h)
      goto error;
    sendbuf_org = (long int *)malloc(count * type_size);
    if (!sendbuf_org)
      goto error;
    recvbuf_org = (long int *)malloc(count * type_size);
    if (!recvbuf_org)
      goto error;
    gpu_memcpy_dh(sendbuf_org, sendbuf, count * type_size);
    gpu_memcpy_dh(recvbuf_org, recvbuf, count * type_size);
    for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
      ((long int *)sendbuf_h)[i] = world_rankd * count + i;
    }
    gpu_memcpy_hd(sendbuf, sendbuf_h, count * type_size);
    MPI_Allreduce(sendbuf, recvbuf_ref, count, MPI_LONG, MPI_SUM, comm_row);
    if (EXT_MPI_Exec_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    gpu_memcpy_dh(recvbuf_h, recvbuf, count * type_size);
    gpu_memcpy_dh(recvbuf_ref_h, recvbuf_ref, count * type_size);
    j = 0;
    for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
      if (((long int *)recvbuf_h)[i] != ((long int *)recvbuf_ref_h)[i]) {
        j = 1;
      }
    }
    gpu_memcpy_hd(recvbuf, recvbuf_org, count * type_size);
    gpu_memcpy_hd(sendbuf, sendbuf_org, count * type_size);
    free(sendbuf_org);
    free(recvbuf_org);
    free(recvbuf_ref_h);
    free(recvbuf_h);
    free(sendbuf_h);
    gpu_free(recvbuf_ref);
  } else {
#endif
    buffer_ref = (long int *)malloc(count * type_size);
    if (!buffer_ref)
      goto error;
    buffer_org = (long int *)malloc(count * type_size);
    if (!buffer_org)
      goto error;
    memcpy(buffer_org, buffer, count * type_size);
    for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
      ((long int *)buffer_ref)[i] = ((long int *)buffer)[i] =
          world_rankd * count + i + 1000;
    }
    MPI_Bcast(buffer_ref, count, datatype, root, comm_row);
    if (EXT_MPI_Exec_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    j = 0;
    for (i = 0; i < (count * type_size) / sizeof(long int); i++) {
      if (((long int *)buffer)[i] != ((long int *)buffer_ref)[i]) {
        j = 1;
      }
    }
    memcpy(buffer, buffer_org, count * type_size);
    free(buffer_org);
    free(buffer_ref);
#ifdef GPU_ENABLED
  }
#endif
  if (j) {
    printf("logical error in EXT_MPI_Bcast %d\n", world_rankd);
    exit(1);
  }
#endif
  return 0;
error:
#ifdef DEBUG
#ifdef GPU_ENABLED
  free(sendbuf_org);
  free(recvbuf_org);
  free(recvbuf_ref_h);
  free(recvbuf_h);
  free(sendbuf_h);
#endif
  free(buffer_org);
  free(buffer_ref);
#endif
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}

/*int EXT_MPI_Bcast_init_general (void *sendbuf, int count, MPI_Datatype
datatype, int root, MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm
comm_column, int my_cores_per_node_column, int *handle){ int *recvcounts,
*displs; int comm_row_size, comm_row_rank, i; int iret; MPI_Comm_size(comm_row,
&comm_row_size); MPI_Comm_rank(comm_row, &comm_row_rank); recvcounts = (int
*)malloc(comm_row_size*sizeof(int)); for (i=0; i<comm_row_size; i++){
    recvcounts[i] = 0;
  }
  recvcounts[root] = count;
  displs = (int *)malloc(comm_row_size*sizeof(int));
  displs[0] = 0;
  for (i=0; i<comm_row_size-1; i++){
    displs[i+1] = displs[i]+recvcounts[i];
  }
  count = recvcounts[comm_row_rank];
  iret = EXT_MPI_Allgatherv_init_general(sendbuf, count, datatype, sendbuf,
recvcounts, displs, datatype, comm_row, my_cores_per_node_row, comm_column,
my_cores_per_node_column, handle); free(displs); free(recvcounts); return(iret);
}*/

/*int EXT_MPI_Reduce_init_general (void *sendbuf, void *recvbuf, int count,
MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm_row, int
my_cores_per_node_row, MPI_Comm comm_column, int my_cores_per_node_column, int
*handle){ int *recvcounts; int comm_row_size, i; int iret;
  MPI_Comm_size(comm_row, &comm_row_size);
  recvcounts = (int *)malloc(comm_row_size*sizeof(int));
  for (i=0; i<comm_row_size; i++){
    recvcounts[i] = 0;
  }
  recvcounts[root] = count;
  iret = EXT_MPI_Reduce_scatter_init_general(sendbuf, recvbuf, recvcounts,
datatype, op, comm_row, my_cores_per_node_row, comm_column,
my_cores_per_node_column, handle); free(recvcounts); return(iret);
}*/

int EXT_MPI_Allgatherv_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                            void *recvbuf, int *recvcounts, int *displs,
                            MPI_Datatype recvtype, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allgatherv_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
        comm, num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Allgather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allgather_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm,
        num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Reduce_scatter_init(void *sendbuf, void *recvbuf, int *recvcounts,
                                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                                int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_scatter_init_general(
        sendbuf, recvbuf, recvcounts, datatype, op, comm, num_core_per_node,
        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Reduce_scatter_block_init(void *sendbuf, void *recvbuf,
                                      int recvcount, MPI_Datatype datatype,
                                      MPI_Op op, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_scatter_block_init_general(
        sendbuf, recvbuf, recvcount, datatype, op, comm, num_core_per_node,
        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Allreduce_init(void *sendbuf, void *recvbuf, int count,
                           MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                           int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allreduce_init_general(sendbuf, recvbuf, count, datatype,
                                           op, comm, num_core_per_node,
                                           MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Bcast_init(void *sendbuf, int count, MPI_Datatype datatype,
                       int root, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Bcast_init_general(sendbuf, count, datatype, root, comm,
                                       num_core_per_node, MPI_COMM_NULL, 1,
                                       handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Reduce_init(void *sendbuf, void *recvbuf, int count,
                        MPI_Datatype datatype, MPI_Op op, int root,
                        MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_init_general(sendbuf, recvbuf, count, datatype, op,
                                        root, comm, num_core_per_node,
                                        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Exec(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Exec_native(handle));
  } else {
    return (0);
  }
}

int EXT_MPI_Test(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Test_native(handle));
  } else {
    return (0);
  }
}

int EXT_MPI_Progress() { return (EXT_MPI_Progress_native()); }

int EXT_MPI_Wait(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Wait_native(handle));
  } else {
    return (0);
  }
}

int EXT_MPI_Done(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Done_native(handle));
  } else {
    return (0);
  }
}

int EXT_MPI_Allreduce_simulate(int count, MPI_Datatype datatype, MPI_Op op,
                               int comm_size_row, int my_cores_per_node_row,
                               int comm_size_column,
                               int my_cores_per_node_column) {
  int comm_rank_row, i;
  int type_size;
  int *num_ports = NULL, *groups = NULL;
  struct cost_list *p1, *p2;
  comm_rank_row = 0;
  MPI_Type_size(datatype, &type_size);
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
      if (cost_simulate(count, datatype, comm_size_row * 12, 12,
                        comm_size_column, my_cores_per_node_column,
                        comm_size_row, comm_rank_row, 1) < 0)
        goto error;
    } else {
      if (cost_simulate(count, datatype, comm_size_row, my_cores_per_node_row,
                        comm_size_column, my_cores_per_node_column,
                        comm_size_row, comm_rank_row, 1) < 0)
        goto error;
    }
    p1 = cost_list_start;
    while (p1) {
      for (i = 0; p1->rarray[i]; i++) {
        num_ports[i] = p1->rarray[i];
        groups[i] = p1->garray[i];
      }
      groups[i] = num_ports[i] = 0;
      for (i = 0; groups[i]; i++) {
        printf(" %d ", groups[i]);
      }
      printf("|");
      for (i = 0; num_ports[i]; i++) {
        printf(" %d ", num_ports[i]);
      }
      printf("| %e %e\n", p1->nsteps, p1->nvolume);
      p2 = p1;
      p1 = p1->next;
      free(p2->garray);
      free(p2->rarray);
      free(p2);
    }
    cost_list_start = NULL;
    cost_list_length = 0;
    cost_list_counter = 0;
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
