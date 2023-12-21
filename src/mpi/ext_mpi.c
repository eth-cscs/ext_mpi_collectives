#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ext_mpi.h"
#include "constants.h"
#include "ext_mpi_native.h"
#include "ext_mpi_native_blocking.h"
#include "read_write.h"
#include "read_bench.h"
#include "cost_simple_recursive.h"
#include "cost_estimation.h"
#include "count_instructions.h"
#include "ports_groups.h"
#include "cost_copyin_measurement.h"
#include "recursive_factors.h"
#include "num_ports_factors.h"
#include "debug_persistent.h"
#ifdef GPU_ENABLED
#include "gpu_core.h"
#endif

int ext_mpi_blocking = 0;

int ext_mpi_alternating = -1;
int ext_mpi_num_tasks_per_socket = -1;
int ext_mpi_num_sockets_per_node = -1;
int ext_mpi_bit_reproducible = -1;
int ext_mpi_bit_identical = -1;
//parameter for minimum computation set
int ext_mpi_minimum_computation = -1;
int *ext_mpi_fixed_factors_ports = NULL;
int *ext_mpi_fixed_factors_groups = NULL;
int ext_mpi_not_recursive = -1;
int ext_mpi_copyin_method = -1;
int *ext_mpi_copyin_factors = NULL;

int ext_mpi_verbose = 0;
int ext_mpi_debug = 1;
static int is_initialised = 0;
// FIXME: should be 0 if feature is present

static int read_env() {
  int mpi_comm_rank, mpi_comm_size, var, i, j;
  char *c = NULL;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_comm_size);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_NUM_SOCKETS_PER_NODE")) != NULL);
    if (var) {
      if (sscanf(c, "%d", &var) >= 1){
        ext_mpi_num_sockets_per_node = var;
      }
    }
  }
  MPI_Bcast(&ext_mpi_num_sockets_per_node, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_NOT_RECURSIVE")) != NULL);
    if (var) {
      ext_mpi_not_recursive = 1;
      printf("# EXT_MPI not recursive\n");
    }
  }
  MPI_Bcast(&ext_mpi_not_recursive, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_VERBOSE")) != NULL);
    if (var) {
      ext_mpi_verbose = 1;
      printf("# EXT_MPI verbose\n");
    }
  }
  MPI_Bcast(&ext_mpi_verbose, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_DEBUG")) != NULL);
    if (var) {
      if (sscanf(c, "%d", &var) == 0){
        ext_mpi_debug = 0;
      }
      if (ext_mpi_verbose) {
        printf("# EXT_MPI debug %d\n", ext_mpi_debug);
      }
    }
  }
  MPI_Bcast(&ext_mpi_verbose, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_ALTERNATING")) != NULL);
    if (var) {
      if (c[0] == '0') {
        ext_mpi_alternating = 0;
        if (ext_mpi_verbose) {
          printf("# EXT_MPI not alternating\n");
        }
      }
      if (c[0] == '1') {
        ext_mpi_alternating = 1;
        if (ext_mpi_verbose) {
          printf("# EXT_MPI alternating\n");
        }
      }
    }
  }
  MPI_Bcast(&ext_mpi_alternating, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_NUM_TASKS_PER_SOCKET")) != NULL);
    if (var) {
      if (sscanf(c, "%d", &var) >= 1){
        ext_mpi_num_tasks_per_socket = var;
      }
    }
  }
  MPI_Bcast(&ext_mpi_num_tasks_per_socket, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_NUM_PORTS")) != NULL);
  }
  MPI_Bcast(&var, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (var && (ext_mpi_fixed_factors_ports != NULL)) {
    free(ext_mpi_fixed_factors_ports);
    free(ext_mpi_fixed_factors_groups);
    ext_mpi_fixed_factors_ports=ext_mpi_fixed_factors_groups=0;
  }
  if (var) {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    if (mpi_comm_rank == 0) {
      ext_mpi_scan_ports_groups(c, &ext_mpi_fixed_factors_ports, &ext_mpi_fixed_factors_groups);
      for (i=0; ext_mpi_fixed_factors_ports[i]; i++);
      i++;
    }else{
      ext_mpi_fixed_factors_ports = (int *)malloc((2 * j + 1) * sizeof(int));
      if (!ext_mpi_fixed_factors_ports)
        goto error;
      ext_mpi_fixed_factors_groups = (int *)malloc((2 * j + 1) * sizeof(int));
      if (!ext_mpi_fixed_factors_groups)
        goto error;
    }
    MPI_Bcast(&i, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(ext_mpi_fixed_factors_ports, i, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(ext_mpi_fixed_factors_groups, i, MPI_INT, 0, MPI_COMM_WORLD);
  }
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_COPYIN")) != NULL);
  }
  MPI_Bcast(&var, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (var && (ext_mpi_copyin_factors != NULL)) {
    free(ext_mpi_copyin_factors);
    ext_mpi_copyin_method = -1;
    ext_mpi_copyin_factors = NULL;
  }
  if (var) {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    if (mpi_comm_rank == 0) {
      ext_mpi_scan_copyin(c, &ext_mpi_copyin_method, &ext_mpi_copyin_factors);
      for (i=0; ext_mpi_copyin_factors[i]; i++);
      i++;
    }else{
      ext_mpi_copyin_factors = (int *)malloc((2 * j + 1) * sizeof(int));
      if (!ext_mpi_copyin_factors)
        goto error;
    }
    MPI_Bcast(&i, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&ext_mpi_copyin_method, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(ext_mpi_copyin_factors, i, MPI_INT, 0, MPI_COMM_WORLD);
  }
  return 0;
error:
  free(ext_mpi_fixed_factors_ports);
  free(ext_mpi_fixed_factors_groups);
  return ERROR_MALLOC;
}

static int delete_env() {
  free(ext_mpi_fixed_factors_groups);
  free(ext_mpi_fixed_factors_ports);
  return 0;
}

int ext_mpi_get_num_tasks_per_socket(MPI_Comm comm, int num_sockets_per_node) {
  int my_mpi_size = -1, my_mpi_rank = -1, num_cores = -1, *all_num_cores, i, j;
  MPI_Comm comm_node;
  if (ext_mpi_num_tasks_per_socket > 0) {
    return ext_mpi_num_tasks_per_socket;
  }
  MPI_Comm_size(comm, &my_mpi_size);
  MPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, my_mpi_rank, MPI_INFO_NULL,
                       &comm_node);
  MPI_Comm_size(comm_node, &num_cores);
  PMPI_Comm_free(&comm_node);
  all_num_cores = (int*)malloc((my_mpi_size+1)*sizeof(int));
  for (i=0; i<my_mpi_size+1; i++) {
    all_num_cores[i] = 0;
  }
  if (!all_num_cores) goto error;
  PMPI_Allgather(&num_cores, 1, MPI_INT, all_num_cores, 1, MPI_INT, comm);
  for (j=all_num_cores[0]; j>=1; j--){
    for (i=0; (!(all_num_cores[i]%j))&&(i<my_mpi_size); i++);
    if (i==my_mpi_size){
      free(all_num_cores);
      return j/num_sockets_per_node;
    }
  }
  free(all_num_cores);
  return -1;
error:
  free(all_num_cores);
  return ERROR_MALLOC;
}

int ext_mpi_is_rank_zero(MPI_Comm comm_row, MPI_Comm comm_column){
  int comm_rank_row, comm_rank_column=0;
  if (comm_row == MPI_COMM_NULL){
    return 0;
  }
  MPI_Comm_rank(comm_row, &comm_rank_row);
  if (comm_column != MPI_COMM_NULL){
    MPI_Comm_rank(comm_column, &comm_rank_column);
  }
  return (!comm_rank_row)&&(!comm_rank_column);
}

int ext_mpi_allgatherv_init_general(const void *sendbuf, int sendcount,
                                    MPI_Datatype sendtype, void *recvbuf,
                                    const int *recvcounts, const int *displs,
                                    MPI_Datatype recvtype, MPI_Comm comm,
                                    MPI_Info info, int *handle) {
  int comm_rank_row, comm_size_row, *num_ports = NULL, *groups = NULL, type_size, scount,
        alt, rcount, group_size, my_cores_per_node_row, my_cores_per_node_column, not_recursive, num_sockets_per_node, minimum_computation, i;
  char *str;
  if (ext_mpi_debug) {
    i = ext_mpi_debug;
    ext_mpi_debug = 0;
    ext_mpi_allgatherv_init_debug(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, info, handle);
    ext_mpi_debug = i;
  }
  MPI_Comm_rank(comm, &comm_rank_row);
  MPI_Comm_size(comm, &comm_size_row);
  num_sockets_per_node = ext_mpi_get_param(ext_mpi_num_sockets_per_node, comm, info, "ext_mpi_num_sockets_per_node", 1);
  my_cores_per_node_row = ext_mpi_get_param(ext_mpi_num_tasks_per_socket, comm, info, "ext_mpi_num_tasks_per_socket", ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node));
  my_cores_per_node_column = 1;
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  PMPI_Allreduce(&sendcount, &scount, 1, MPI_INT, MPI_MAX, comm);
  minimum_computation = ext_mpi_get_param(ext_mpi_minimum_computation, comm, info, "ext_mpi_minimum_computation", 0);
  group_size = ext_mpi_num_ports_factors_env(comm, ext_mpi_fixed_factors_ports, ext_mpi_fixed_factors_groups, num_ports, groups);
  if (group_size < 0) {
    free(groups);
    free(num_ports);
    group_size = ext_mpi_num_ports_factors_info(comm, info, &num_ports, &groups);
  }
  if (group_size < 0) {
    group_size = ext_mpi_num_ports_factors(scount * type_size, 1, comm, my_cores_per_node_row, num_sockets_per_node, minimum_computation, &num_ports, &groups);
  }
  rcount = 0;
  for (i = 0; i < comm_size_row; i++) {
    rcount += recvcounts[i];
  }
  if (groups[0] == -1 && groups[1] == 0) num_ports[0] = 1;
  alt = ext_mpi_get_param(ext_mpi_alternating, comm, info, "ext_mpi_alternating", rcount < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1);
  not_recursive = ext_mpi_get_param(ext_mpi_not_recursive, comm, info, "ext_mpi_not_recursive", (group_size!=comm_size_row/my_cores_per_node_row));
  if (ext_mpi_verbose) {
    if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
      printf("# EXT_MPI MPI tasks per socket: %d\n", my_cores_per_node_row);
      if (!not_recursive){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI allgatherv parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, sendcount * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  *handle = EXT_MPI_Allgatherv_init_native(
      sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
      comm, my_cores_per_node_row, MPI_COMM_NULL, my_cores_per_node_column,
      num_ports, groups,
      alt, not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
  return 0;
error:
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}

int ext_mpi_gatherv_init_general(const void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 const int *recvcounts, const int *displs,
                                 MPI_Datatype recvtype, int root,
                                 MPI_Comm comm, MPI_Info info, int *handle) {
  int comm_size_row, *num_ports = NULL, *groups = NULL, type_size, scount,
      alt, rcount, group_size, my_cores_per_node_row, my_cores_per_node_column, not_recursive, num_sockets_per_node, minimum_computation, i;
  char *str;
  if (ext_mpi_debug) {
    i = ext_mpi_debug;
    ext_mpi_debug = 0;
    ext_mpi_gatherv_init_debug(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm, info, handle);
    ext_mpi_debug = i;
  }
  MPI_Comm_size(comm, &comm_size_row);
  num_sockets_per_node = ext_mpi_get_param(ext_mpi_num_sockets_per_node, comm, info, "ext_mpi_num_sockets_per_node", 1);
  my_cores_per_node_row = ext_mpi_get_param(ext_mpi_num_tasks_per_socket, comm, info, "ext_mpi_num_tasks_per_socket", ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node));
  my_cores_per_node_column = 1;
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  PMPI_Allreduce(&sendcount, &scount, 1, MPI_INT, MPI_MAX, comm);
  minimum_computation = ext_mpi_get_param(ext_mpi_minimum_computation, comm, info, "ext_mpi_minimum_computation", 0);
  group_size = ext_mpi_num_ports_factors_env(comm, ext_mpi_fixed_factors_ports, ext_mpi_fixed_factors_groups, num_ports, groups);
  if (group_size < 0) {
    free(groups);
    free(num_ports);
    group_size = ext_mpi_num_ports_factors_info(comm, info, &num_ports, &groups);
  }
  if (group_size < 0) {
    group_size = ext_mpi_num_ports_factors(scount * type_size, 1, comm, my_cores_per_node_row, num_sockets_per_node, minimum_computation, &num_ports, &groups);
  }
/*  for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
    num_ports[i] = -num_ports[i];
  }*/
  rcount = 0;
  for (i = 0; i < comm_size_row; i++) {
    rcount += recvcounts[i];
  }
  alt = ext_mpi_get_param(ext_mpi_alternating, comm, info, "ext_mpi_alternating", rcount < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1);
  not_recursive = ext_mpi_get_param(ext_mpi_not_recursive, comm, info, "ext_mpi_not_recursive", (group_size!=comm_size_row/my_cores_per_node_row));
  if (ext_mpi_verbose) {
    if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
      printf("# EXT_MPI MPI tasks per socket: %d\n", my_cores_per_node_row);
      if (!not_recursive){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI gatherv parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, sendcount * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  *handle = EXT_MPI_Gatherv_init_native(
      sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
      root, comm, my_cores_per_node_row, MPI_COMM_NULL,
      my_cores_per_node_column, num_ports, groups,
      alt, not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
  return 0;
error:
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}

int ext_mpi_allgather_init_general(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm,
                                   MPI_Info info, int *handle) {
  int iret, mpi_size, *recvcounts, *displs, i;
  MPI_Comm_size(comm, &mpi_size);
  recvcounts = (int *)malloc(mpi_size * sizeof(int));
  displs = (int *)malloc(mpi_size * sizeof(int));
  for (i = 0; i < mpi_size; i++) {
    recvcounts[i] = recvcount;
  }
  displs[0] = 0;
  for (i = 0; i < mpi_size - 1; i++) {
    displs[i + 1] = displs[i] + recvcounts[i];
  }
  iret = ext_mpi_allgatherv_init_general(sendbuf, sendcount, sendtype, recvbuf,
                                         recvcounts, displs, recvtype, comm,
                                         info, handle);
  free(displs);
  free(recvcounts);
  return (iret);
}

void ext_mpi_revert_num_ports(int *num_ports, int *groups) {
  int i, j, k;
  for (i = 0; num_ports[i]; i++){
    num_ports[i] *= -1;
  }
  for (j = 0; j < i / 2; j++) {
    k = num_ports[j];
    num_ports[j] = num_ports[i - 1 - j];
    num_ports[i - 1 - j] = k;
    k = groups[j];
    groups[j] = groups[i - 1 - j];
    groups[i - 1 - j] = k;
  }
  groups[0] = abs(groups[0]);
  for (i = 1; groups[i]; i++){
    if (groups[i] < 0) {
      groups[i] = abs(groups[i]);
      groups[i - 1] = -abs(groups[i - 1]);
    }
  }
  groups[i - 1] = -abs(groups[i - 1]);
}

int ext_mpi_reduce_scatter_init_general(
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm, MPI_Info info, int *handle) {
  int comm_size_row, *num_ports = NULL, *groups = NULL, type_size, rcount, acount, not_recursive,
      alt, group_size, copyin_method = -1, *copyin_factors = NULL, num_sockets_per_node, my_cores_per_node, num_sockets_per_node_, minimum_computation, i, j, k;
  char *str;
  if (ext_mpi_debug) {
    i = ext_mpi_debug;
    ext_mpi_debug = 0;
    ext_mpi_reduce_scatter_init_debug(sendbuf, recvbuf, recvcounts, datatype, op, comm, info, handle);
    ext_mpi_debug = i;
  }
  MPI_Comm_size(comm, &comm_size_row);
  num_sockets_per_node_ = ext_mpi_get_param(ext_mpi_num_sockets_per_node, comm, info, "ext_mpi_num_sockets_per_node", 1);
  my_cores_per_node = ext_mpi_get_param(ext_mpi_num_tasks_per_socket, comm, info, "ext_mpi_num_tasks_per_socket", ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node_));
  MPI_Type_size(datatype, &type_size);
  copyin_factors = (int*) malloc(sizeof(int) * (comm_size_row + 1));
  if (!copyin_factors)
    goto error;
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  rcount = 0;
  for (i = 0; i < comm_size_row; i++) {
    if (recvcounts[i] > rcount) {
      rcount = recvcounts[i];
    }
  }
  minimum_computation = ext_mpi_get_param(ext_mpi_minimum_computation, comm, info, "ext_mpi_minimum_computation", 0);
  group_size = ext_mpi_num_ports_factors_env(comm, ext_mpi_fixed_factors_ports, ext_mpi_fixed_factors_groups, num_ports, groups);
  if (group_size < 0) {
    free(groups);
    free(num_ports);
    group_size = ext_mpi_num_ports_factors_info(comm, info, &num_ports, &groups);
  }
  if (group_size < 0) {
    group_size = ext_mpi_num_ports_factors(rcount * type_size, 1, comm, my_cores_per_node, num_sockets_per_node_, minimum_computation, &num_ports, &groups);
  }
  for (i = 0; num_ports[i]; i++);
  for (j = 0; j < i/2; j++) {
    k = num_ports[j]; num_ports[j] = -num_ports[i - j - 1]; num_ports[i - j - 1] = -k;
  }
  if (i % 2) num_ports[i / 2] = -num_ports[i / 2];
  alt = ext_mpi_get_param(ext_mpi_alternating, comm, info, "ext_mpi_alternating", rcount < 10000000 && my_cores_per_node > 1);
  not_recursive = ext_mpi_get_param(ext_mpi_not_recursive, comm, info, "ext_mpi_not_recursive", (group_size!=comm_size_row/my_cores_per_node));
  acount = 0;
  for (i = 0; i < comm_size_row; i++) {
    acount += recvcounts[i];
  }
  num_sockets_per_node = num_sockets_per_node_;
  if (ext_mpi_copyin_method >= 0) {
    copyin_method = ext_mpi_copyin_method;
    for (i = 0; ext_mpi_copyin_factors[i]; i++) {
      copyin_factors[i] = ext_mpi_copyin_factors[i];
    }
    copyin_factors[i] = 0;
  } else {
    free(copyin_factors);
    if (ext_mpi_copyin_info(comm, info, &copyin_method, &copyin_factors) < 0) {
      EXT_MPI_Allreduce_measurement(
          sendbuf, recvbuf, acount, datatype, op, comm, &my_cores_per_node,
          MPI_COMM_NULL, 1,
          my_cores_per_node, &copyin_method, &copyin_factors, &num_sockets_per_node);
    }
  }
  if (num_sockets_per_node_ == 1) {
    ext_mpi_set_ports_single_node(num_sockets_per_node, num_ports, groups);
  }
  if (ext_mpi_verbose) {
    if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
      printf("# EXT_MPI MPI tasks per socket: %d\n", my_cores_per_node);
      if (!not_recursive){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      i = recvcounts[0];
      printf("# EXT_MPI reduce_scatter parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node, i * type_size, 1,
             my_cores_per_node, str);
      free(str);
      str = ext_mpi_print_copyin(copyin_method, copyin_factors);
      printf("# EXT_MPI reduce_scatter parameters copyin %s\n", str);
      free(str);
    }
  }
  *handle = EXT_MPI_Reduce_scatter_init_native(
      sendbuf, recvbuf, recvcounts, datatype, op, comm,
      my_cores_per_node, MPI_COMM_NULL, 1, num_ports,
      groups, copyin_method, copyin_factors, alt, not_recursive,
      ext_mpi_blocking, num_sockets_per_node, 0, NULL, NULL);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
  free(copyin_factors);
  return 0;
error:
  free(groups);
  free(num_ports);
  free(copyin_factors);
  return ERROR_MALLOC;
}

int ext_mpi_scatterv_init_general(const void *sendbuf, const int *sendcounts, const int *displs,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  int root, MPI_Comm comm,
                                  MPI_Info info, int *handle) {
  int comm_size_row, *num_ports = NULL, *groups = NULL, type_size, rcount, my_cores_per_node_row, my_cores_per_node_column,
                     alt, group_size, copyin_method = -1, not_recursive, num_sockets_per_node, minimum_computation, i, j, k;
  char *str;
  if (ext_mpi_debug) {
    i = ext_mpi_debug;
    ext_mpi_debug = 0;
    ext_mpi_scatterv_init_debug(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm, info, handle);
    ext_mpi_debug = i;
  }
  num_sockets_per_node = ext_mpi_get_param(ext_mpi_num_sockets_per_node, comm, info, "ext_mpi_num_sockets_per_node", 1);
  my_cores_per_node_row = ext_mpi_get_param(ext_mpi_num_tasks_per_socket, comm, info, "ext_mpi_num_tasks_per_socket", ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node));
  my_cores_per_node_column = 1;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  rcount = recvcount;
  minimum_computation = ext_mpi_get_param(ext_mpi_minimum_computation, comm, info, "ext_mpi_minimum_computation", 0);
  group_size = ext_mpi_num_ports_factors_env(comm, ext_mpi_fixed_factors_ports, ext_mpi_fixed_factors_groups, num_ports, groups);
  if (group_size < 0) {
    free(groups);
    free(num_ports);
    group_size = ext_mpi_num_ports_factors_info(comm, info, &num_ports, &groups);
  }
  if (group_size < 0) {
    group_size = ext_mpi_num_ports_factors(rcount * type_size, 1, comm, my_cores_per_node_row, num_sockets_per_node, minimum_computation, &num_ports, &groups);
  }
  for (i = 0; num_ports[i]; i++);
  for (j = 0; j < i / 2; j++) {
    k = num_ports[j];
    num_ports[j] = num_ports[i - 1 - j];
    num_ports[i - 1 - j] = k;
  }
  alt = ext_mpi_get_param(ext_mpi_alternating, comm, info, "ext_mpi_alternating", rcount < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1);
  not_recursive = ext_mpi_get_param(ext_mpi_not_recursive, comm, info, "ext_mpi_not_recursive", (group_size!=comm_size_row/my_cores_per_node_row));
  if (ext_mpi_verbose) {
    if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
      printf("# EXT_MPI MPI tasks per socket: %d\n", my_cores_per_node_row);
      if (!not_recursive){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI scatterv parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, recvcount * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  *handle = EXT_MPI_Scatterv_init_native(
      sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype,
      root, comm, my_cores_per_node_row, MPI_COMM_NULL,
      my_cores_per_node_column, num_ports, groups,
      copyin_method, alt, not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
  return 0;
error:
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}

int ext_mpi_reduce_scatter_block_init_general(
    void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm, MPI_Info info, int *handle) {
  int iret, *recvcounts, mpi_size, i;
  MPI_Comm_size(comm, &mpi_size);
  recvcounts = (int *)malloc(mpi_size * sizeof(int));
  for (i = 0; i < mpi_size; i++) {
    recvcounts[i] = recvcount;
  }
  iret = ext_mpi_reduce_scatter_init_general(
      sendbuf, recvbuf, recvcounts, datatype, op, comm, info, handle);
  free(recvcounts);
  return (iret);
}

int ext_mpi_allreduce_init_general(const void *sendbuf, void *recvbuf, int count,
                                   MPI_Datatype datatype, MPI_Op op,
                                   MPI_Comm comm, MPI_Info info, int *handle) {
  int comm_size_row, comm_rank_row, alt, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method, *copyin_factors = NULL, num_sockets_per_node, my_cores_per_node_row, my_cores_per_node_column, not_recursive, bit_identical, bit_reproducible, num_sockets_per_node_, minimum_computation, i;
  char *str;
  if (ext_mpi_debug) {
    i = ext_mpi_debug;
    ext_mpi_debug = 0;
    ext_mpi_allreduce_init_debug(sendbuf, recvbuf, count, datatype, op, comm, info, handle);
    ext_mpi_debug = i;
  }
  num_sockets_per_node_ = ext_mpi_get_param(ext_mpi_num_sockets_per_node, comm, info, "ext_mpi_num_sockets_per_node", 1);
  my_cores_per_node_row = ext_mpi_get_param(ext_mpi_num_tasks_per_socket, comm, info, "ext_mpi_num_tasks_per_socket", ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node_));
  my_cores_per_node_column = 1;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
  copyin_factors = (int*) malloc(sizeof(int) * (comm_size_row + 1));
  if (!copyin_factors)
    goto error;
  MPI_Type_size(datatype, &type_size);
  if (sendbuf == MPI_IN_PLACE) {
    sendbuf = recvbuf;
  }
  message_size = type_size * count;
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (i = 0; i < comm_size_row; i++) {
    num_ports[i] = groups[i] = 0;
  }
  minimum_computation = ext_mpi_get_param(ext_mpi_minimum_computation, comm, info, "ext_mpi_minimum_computation", 0);
  group_size = ext_mpi_num_ports_factors_env(comm, ext_mpi_fixed_factors_ports, ext_mpi_fixed_factors_groups, num_ports, groups);
  if (group_size < 0) {
    free(groups);
    free(num_ports);
    group_size = ext_mpi_num_ports_factors_info(comm, info, &num_ports, &groups);
  }
  if (group_size < 0) {
    group_size = ext_mpi_num_ports_factors(message_size, 0, comm, my_cores_per_node_row, num_sockets_per_node_, minimum_computation, &num_ports, &groups);
  }
  alt = ext_mpi_get_param(ext_mpi_alternating, comm, info, "ext_mpi_alternating", count * type_size < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1);
  not_recursive = ext_mpi_get_param(ext_mpi_not_recursive, comm, info, "ext_mpi_not_recursive", (group_size!=comm_size_row/my_cores_per_node_row));
  bit_identical = ext_mpi_get_param(ext_mpi_bit_identical, comm, info, "ext_mpi_bit_identical", 0);
  bit_reproducible = ext_mpi_get_param(ext_mpi_bit_reproducible, comm, info, "ext_mpi_bit_reproducible", 1);
  num_sockets_per_node = num_sockets_per_node_;
  if (ext_mpi_copyin_method >= 0) {
    copyin_method = ext_mpi_copyin_method;
    for (i = 0; ext_mpi_copyin_factors[i]; i++) {
      copyin_factors[i] = ext_mpi_copyin_factors[i];
    }
    copyin_factors[i] = 0;
  } else {
    free(copyin_factors);
    if (ext_mpi_copyin_info(comm, info, &copyin_method, &copyin_factors) < 0) {
      EXT_MPI_Allreduce_measurement(
          sendbuf, recvbuf, count, datatype, op, comm, &my_cores_per_node_row,
          MPI_COMM_NULL, my_cores_per_node_column,
          my_cores_per_node_row * my_cores_per_node_column, &copyin_method, &copyin_factors, &num_sockets_per_node);
    }
  }
  if (num_sockets_per_node_ == 1) {
    ext_mpi_set_ports_single_node(num_sockets_per_node, num_ports, groups);
  }
  if (ext_mpi_verbose) {
    if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)) {
      printf("# EXT_MPI MPI tasks per socket: %d\n", my_cores_per_node_row);
      if (!not_recursive){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI allreduce parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, message_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
      str = ext_mpi_print_copyin(copyin_method, copyin_factors);
      printf("# EXT_MPI allreduce parameters copyin %s\n", str);
      free(str);
    }
  }
  *handle = EXT_MPI_Allreduce_init_native(
      sendbuf, recvbuf, count, datatype, op, comm, my_cores_per_node_row,
      MPI_COMM_NULL, my_cores_per_node_column, num_ports, groups,
      copyin_method, copyin_factors, alt,
      bit_identical, !bit_reproducible, not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL, NULL);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
  free(copyin_factors);
  return 0;
error:
  free(groups);
  free(num_ports);
  free(copyin_factors);
  return ERROR_MALLOC;
}

int ext_mpi_reduce_init_general(const void *sendbuf, void *recvbuf, int count,
                                MPI_Datatype datatype, MPI_Op op, int root,
                                MPI_Comm comm, MPI_Info info, int *handle) {
  int comm_size_row, comm_rank_row, alt, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method, *copyin_factors, num_sockets_per_node, my_cores_per_node, not_recursive, bit_reproducible, num_sockets_per_node_, minimum_computation, i;
  char *str;
  if (ext_mpi_debug) {
    i = ext_mpi_debug;
    ext_mpi_debug = 0;
    ext_mpi_reduce_init_debug(sendbuf, recvbuf, count, datatype, op, root, comm, info, handle);
    ext_mpi_debug = i;
  }
  num_sockets_per_node_ = ext_mpi_get_param(ext_mpi_num_sockets_per_node, comm, info, "ext_mpi_num_sockets_per_node", 1);
  my_cores_per_node = ext_mpi_get_param(ext_mpi_num_tasks_per_socket, comm, info, "ext_mpi_num_tasks_per_socket", ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node_));
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
  copyin_factors = (int*) malloc(sizeof(int) * (comm_size_row + 1));
  MPI_Type_size(datatype, &type_size);
  if (sendbuf == MPI_IN_PLACE) {
    sendbuf = recvbuf;
  }
  message_size = type_size * count;
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  minimum_computation = ext_mpi_get_param(ext_mpi_minimum_computation, comm, info, "ext_mpi_minimum_computation", 0);
  group_size = ext_mpi_num_ports_factors_env(comm, ext_mpi_fixed_factors_ports, ext_mpi_fixed_factors_groups, num_ports, groups);
  if (group_size < 0) {
    free(groups);
    free(num_ports);
    group_size = ext_mpi_num_ports_factors_info(comm, info, &num_ports, &groups);
  }
  if (group_size < 0) {
    group_size = ext_mpi_num_ports_factors(message_size, 0, comm, my_cores_per_node, num_sockets_per_node_, minimum_computation, &num_ports, &groups);
  }
  alt = ext_mpi_get_param(ext_mpi_alternating, comm, info, "ext_mpi_alternating", count * type_size < 10000000 && my_cores_per_node > 1);
  not_recursive = ext_mpi_get_param(ext_mpi_not_recursive, comm, info, "ext_mpi_not_recursive", (group_size!=comm_size_row/my_cores_per_node));
  bit_reproducible = ext_mpi_get_param(ext_mpi_bit_reproducible, comm, info, "ext_mpi_bit_reproducible", 1);
  num_sockets_per_node = num_sockets_per_node_;
  if (ext_mpi_copyin_method >= 0) {
    copyin_method = ext_mpi_copyin_method;
    for (i = 0; ext_mpi_copyin_factors[i]; i++) {
      copyin_factors[i] = ext_mpi_copyin_factors[i];
    }
    copyin_factors[i] = 0;
  } else {
    free(copyin_factors);
    if (ext_mpi_copyin_info(comm, info, &copyin_method, &copyin_factors) < 0) {
      EXT_MPI_Allreduce_measurement(
          sendbuf, recvbuf, count, datatype, op, comm, &my_cores_per_node,
          MPI_COMM_NULL, 1,
          my_cores_per_node, &copyin_method, &copyin_factors, &num_sockets_per_node);
    }
  }
  if (ext_mpi_verbose) {
    if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
      printf("# EXT_MPI MPI tasks per socket: %d\n", my_cores_per_node);
      if (!not_recursive){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI reduce parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node, count * message_size, 1,
             my_cores_per_node, str);
      free(str);
      str = ext_mpi_print_copyin(copyin_method, copyin_factors);
      printf("# EXT_MPI reduce parameters copyin %s\n", str);
      free(str);
    }
  }
  *handle = EXT_MPI_Reduce_init_native(
      sendbuf, recvbuf, count, datatype, op, root, comm,
      my_cores_per_node, MPI_COMM_NULL, 1, num_ports,
      groups, copyin_method, copyin_factors, alt,
      0, !bit_reproducible, not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL, NULL);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
  free(copyin_factors);
  return 0;
error:
  free(groups);
  free(num_ports);
  free(copyin_factors);
  return ERROR_MALLOC;
}

int ext_mpi_bcast_init_general(void *buffer, int count, MPI_Datatype datatype,
                               int root, MPI_Comm comm, MPI_Info info,
                               int *handle) {
  int comm_size_row, comm_rank_row, alt, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method, *copyin_factors, num_sockets_per_node, my_cores_per_node_row, my_cores_per_node_column, not_recursive, num_sockets_per_node_, minimum_computation, i;
  char *str;
  if (ext_mpi_debug) {
    i = ext_mpi_debug;
    ext_mpi_debug = 0;
    ext_mpi_bcast_init_debug(buffer, count, datatype, root, comm, info, handle);
    ext_mpi_debug = i;
  }
  num_sockets_per_node_ = ext_mpi_get_param(ext_mpi_num_sockets_per_node, comm, info, "ext_mpi_num_sockets_per_node", 1);
  my_cores_per_node_row = ext_mpi_get_param(ext_mpi_num_tasks_per_socket, comm, info, "ext_mpi_num_tasks_per_socket", ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node_));
  my_cores_per_node_column = 1;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  message_size = type_size * count;
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  minimum_computation = ext_mpi_get_param(ext_mpi_minimum_computation, comm, info, "ext_mpi_minimum_computation", 0);
  group_size = ext_mpi_num_ports_factors_env(comm, ext_mpi_fixed_factors_ports, ext_mpi_fixed_factors_groups, num_ports, groups);
  if (group_size < 0) {
    free(groups);
    free(num_ports);
    group_size = ext_mpi_num_ports_factors_info(comm, info, &num_ports, &groups);
  }
  if (group_size < 0) {
    group_size = ext_mpi_num_ports_factors(count * type_size, 1, comm, my_cores_per_node_row, num_sockets_per_node_, minimum_computation, &num_ports, &groups);
  }
  alt = ext_mpi_get_param(ext_mpi_alternating, comm, info, "ext_mpi_alternating", count * type_size < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1);
  not_recursive = ext_mpi_get_param(ext_mpi_not_recursive, comm, info, "ext_mpi_not_recursive", (group_size!=comm_size_row/my_cores_per_node_row));
  if (ext_mpi_verbose) {
    if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
      printf("# EXT_MPI MPI tasks per socket: %d\n", my_cores_per_node_row);
      if (!not_recursive){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI bcast parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, message_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  num_sockets_per_node = num_sockets_per_node_;
  copyin_method = 0;
  copyin_factors = NULL;
  *handle = EXT_MPI_Bcast_init_native(
      buffer, count, datatype, root, comm, my_cores_per_node_row,
      MPI_COMM_NULL, my_cores_per_node_column, num_ports, groups,
      copyin_method, copyin_factors, alt, not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL);
  if (*handle < 0)
    goto error;
  free(groups);
  free(num_ports);
  return 0;
error:
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

int EXT_MPI_Allgatherv_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                            void *recvbuf, const int *recvcounts, const int *displs,
                            MPI_Datatype recvtype, MPI_Comm comm, MPI_Info info, int *handle) {
  return ext_mpi_allgatherv_init_general(
      sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
      comm, info, handle);
}

int EXT_MPI_Allgather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, MPI_Info info, int *handle) {
  return ext_mpi_allgather_init_general(
      sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, info,
      handle);
}

int EXT_MPI_Reduce_scatter_init(const void *sendbuf, void *recvbuf, const int *recvcounts,
                                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                                MPI_Info info, int *handle) {
  return ext_mpi_reduce_scatter_init_general(
      sendbuf, recvbuf, recvcounts, datatype, op, comm, info, handle);
}

int EXT_MPI_Reduce_scatter_block_init(void *sendbuf, void *recvbuf,
                                      int recvcount, MPI_Datatype datatype,
                                      MPI_Op op, MPI_Comm comm, MPI_Info info, int *handle) {
  return ext_mpi_reduce_scatter_block_init_general(
      sendbuf, recvbuf, recvcount, datatype, op, comm, info, handle);
}

int EXT_MPI_Allreduce_init(const void *sendbuf, void *recvbuf, int count,
                           MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                           MPI_Info info, int *handle) {
  return ext_mpi_allreduce_init_general(sendbuf, recvbuf, count, datatype,
                                        op, comm, info, handle);
}

int EXT_MPI_Bcast_init(void *sendbuf, int count, MPI_Datatype datatype,
                       int root, MPI_Comm comm, MPI_Info info, int *handle) {
  return ext_mpi_bcast_init_general(sendbuf, count, datatype, root, comm,
		                    info, handle);
}

int EXT_MPI_Reduce_init(const void *sendbuf, void *recvbuf, int count,
                        MPI_Datatype datatype, MPI_Op op, int root,
                        MPI_Comm comm, MPI_Info info, int *handle) {
  return ext_mpi_reduce_init_general(sendbuf, recvbuf, count, datatype, op,
                                     root, comm, info, handle);
}

int EXT_MPI_Gatherv_init(const void *sendbuf, int sendcount,
                         MPI_Datatype sendtype, void *recvbuf,
                         const int *recvcounts, const int *displs,
                         MPI_Datatype recvtype, int root,
                         MPI_Comm comm, MPI_Info info, int *handle){
  return ext_mpi_gatherv_init_general(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
                                      root, comm, info, handle);
}

int EXT_MPI_Scatterv_init(const void *sendbuf, const int *sendcounts, const int *displs,
                          MPI_Datatype sendtype, void *recvbuf,
                          int recvcount, MPI_Datatype recvtype,
                          int root, MPI_Comm comm, MPI_Info info, int *handle){
  return ext_mpi_scatterv_init_general(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype,
                                       root, comm, info, handle);
}

int EXT_MPI_Start(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Start_native(handle));
  } else {
    return -1;
  }
}

int EXT_MPI_Test(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Test_native(handle));
  } else {
    return -1;
  }
}

int EXT_MPI_Progress() { return (EXT_MPI_Progress_native()); }

int EXT_MPI_Wait(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Wait_native(handle));
  } else {
    return -1;
  }
}

int EXT_MPI_Done(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Done_native(handle));
  } else {
    return -1;
  }
}

int EXT_MPI_Init() {
  ext_mpi_read_bench();
  read_env();
  EXT_MPI_Init_native();
  is_initialised = 1;
  return 0;
}

int EXT_MPI_Initialized(int *flag) {
  *flag = is_initialised;
  return 0;
}

int EXT_MPI_Finalize() {
  EXT_MPI_Finalize_native();
  delete_env();
  ext_mpi_delete_bench();
  return 0;
}
