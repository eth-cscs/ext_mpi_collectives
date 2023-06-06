#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ext_mpi.h"
#include "constants.h"
#include "ext_mpi_native.h"
#include "read_write.h"
#include "read_bench.h"
#include "cost_simple_recursive.h"
#include "cost_estimation.h"
#include "cost_simulation.h"
#include "count_instructions.h"
#include "ports_groups.h"
#include "cost_copyin_measurement.h"
#include "recursive_factors.h"
#ifdef GPU_ENABLED
#include "gpu_core.h"
#endif

int ext_mpi_num_sockets_per_node = 1;
int ext_mpi_blocking = 0;
int ext_mpi_bit_reproducible = 1;
int ext_mpi_bit_identical = 0;
//parameter for minimum computation set
int ext_mpi_minimum_computation = 0;
int ext_mpi_verbose = 0;

static int num_tasks_per_socket = 0;
static int is_initialised = 0;
static int copyin_method = -1;
static int alternating = 0;
// FIXME: should be 0 if feature is present
static int not_recursive = 0;
static int *fixed_factors_ports = NULL;
static int *fixed_factors_groups = NULL;
static int new_factors_heuristic = 0;

static int read_env() {
  int mpi_comm_rank, mpi_comm_size, var, i, j;
  char *c = NULL;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_comm_size);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_NEW_FACTORS_HEURISTIC")) != NULL);
    if (var) {
      if (sscanf(c, "%d", &var) >= 1){
        new_factors_heuristic = var;
      }
    }
  }
  MPI_Bcast(&new_factors_heuristic, 1, MPI_INT, 0, MPI_COMM_WORLD);
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
      not_recursive = 1;
      printf("# EXT_MPI not recursive\n");
    }
  }
  MPI_Bcast(&not_recursive, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_VERBOSE")) != NULL);
    if (var) {
      ext_mpi_verbose = 1;
      printf("# EXT_MPI verbose\n");
    }
  }
  MPI_Bcast(&ext_mpi_verbose, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_COPYIN_METHOD")) != NULL);
    if (var) {
      copyin_method = c[0]-'0';
      if (ext_mpi_verbose) {
        printf("# EXT_MPI copy in method %d\n", copyin_method);
      }
    }
  }
  MPI_Bcast(&copyin_method, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_ALTERNATING")) != NULL);
    if (var) {
      if (c[0] == '1') {
        alternating = 1;
        if (ext_mpi_verbose) {
          printf("# EXT_MPI not alternating\n");
        }
      }
      if (c[0] == '2') {
        alternating = 2;
        if (ext_mpi_verbose) {
          printf("# EXT_MPI alternating\n");
        }
      }
    }
  }
  MPI_Bcast(&alternating, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_NUM_TASKS_PER_SOCKET")) != NULL);
    if (var) {
      if (sscanf(c, "%d", &var) >= 1){
        num_tasks_per_socket = var;
      }
    }
  }
  MPI_Bcast(&num_tasks_per_socket, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_NUM_PORTS")) != NULL);
  }
  MPI_Bcast(&var, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (var && (fixed_factors_ports != NULL)) {
    free(fixed_factors_ports);
    free(fixed_factors_groups);
    fixed_factors_ports=fixed_factors_groups=0;
  }
  if (var) {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    if (mpi_comm_rank == 0) {
      ext_mpi_scan_ports_groups(c, &fixed_factors_ports, &fixed_factors_groups);
      for (i=0; fixed_factors_ports[i]; i++);
      i++;
    }else{
      fixed_factors_ports = (int *)malloc((2 * j + 1) * sizeof(int));
      if (!fixed_factors_ports)
        goto error;
      fixed_factors_groups = (int *)malloc((2 * j + 1) * sizeof(int));
      if (!fixed_factors_groups)
        goto error;
    }
    MPI_Bcast(&i, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(fixed_factors_ports, i, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(fixed_factors_groups, i, MPI_INT, 0, MPI_COMM_WORLD);
  }
  return 0;
error:
  free(fixed_factors_ports);
  free(fixed_factors_groups);
  return ERROR_MALLOC;
}

static int delete_env() {
  free(fixed_factors_groups);
  free(fixed_factors_ports);
  return 0;
}

static int get_num_tasks_per_socket(MPI_Comm comm) {
  int my_mpi_size = -1, my_mpi_rank = -1, num_cores = -1, *all_num_cores, i, j;
  MPI_Comm comm_node;
  if (num_tasks_per_socket > 0) {
    return num_tasks_per_socket;
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
      if (ext_mpi_verbose&&(my_mpi_rank==0)){
        printf("# EXT_MPI MPI tasks per socket: %d\n", j/ext_mpi_num_sockets_per_node);
      }
      return j/ext_mpi_num_sockets_per_node;
    }
  }
  free(all_num_cores);
  return -1;
error:
  free(all_num_cores);
  return ERROR_MALLOC;
}

static void set_ports_single_node(int ext_mpi_num_sockets_per_node, int num_sockets_per_node, int *num_ports, int *groups) {
  if (ext_mpi_num_sockets_per_node == 1 && num_sockets_per_node == 2) {
    num_ports[0] = -1;
    num_ports[1] = 1;
    num_ports[2] = 0;
    groups[0] = -2;
    groups[1] = -2;
    groups[2] = 0;
  } else if (ext_mpi_num_sockets_per_node == 1 && num_sockets_per_node == 4) {
    num_ports[0] = -1;
    num_ports[1] = -1;
    num_ports[2] = 1;
    num_ports[3] = 1;
    num_ports[4] = 0;
    groups[0] = -2;
    groups[1] = -2;
    groups[2] = -2;
    groups[3] = -2;
    groups[4] = 0;
  }
}

static int is_rank_zero(MPI_Comm comm_row, MPI_Comm comm_column){
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

static int allgatherv_init_general(const void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   const int *recvcounts, const int *displs,
                                   MPI_Datatype recvtype, MPI_Comm comm_row,
                                   int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle) {
  int comm_rank_row, comm_size_row, *num_ports = NULL, *groups = NULL, type_size, scount,
        i, j, alt, rcount, group_size, factors_max_max = -1, *factors_max, **factors, *primes;
  char *str;
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  PMPI_Allreduce(&sendcount, &scount, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &scount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (fixed_factors_ports == NULL && new_factors_heuristic) {
    if (comm_rank_row == 0) {
      factors_max_max = ext_mpi_heuristic_recursive_non_factors(comm_size_row/my_cores_per_node_row, 1, &factors_max, &factors, &primes);
      if (my_cores_per_node_row > 1) {
	for (i = 0; i < factors_max_max; i++) {
	  primes[i] = 0;
	}
      }
      ext_mpi_min_cost_total(scount * type_size, factors_max_max, factors_max, factors, primes, &i);
      for (j = 0; j < factors_max[i]; j++) {
        if (factors[i][j] > 0) {
          num_ports[j] = factors[i][j] - 1;
        } else {
          num_ports[j] = factors[i][j] + 1;
        }
        groups[j] = comm_size_row/my_cores_per_node_row;
      }
      groups[j - 1] = -groups[j - 1];
    }
    MPI_Bcast(&j, 1, MPI_INT, 0, comm_row);
    MPI_Bcast(num_ports, j, MPI_INT, 0, comm_row);
    MPI_Bcast(groups, j, MPI_INT, 0, comm_row);
    groups[j] = num_ports[j] = 0;
    if (comm_rank_row == 0) {
      free(primes);
      for (i = 0; i < factors_max_max; i++) {
        free(factors[i]);
      }
      free(factors);
      free(factors_max);
    }
  } else if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
    if (comm_size_row/my_cores_per_node_row==1){
      group_size = 1;
    }else{
      //set group size to 2^group_size <= num_ports
      group_size = ceil(log(comm_size_row/my_cores_per_node_row)/log(2));
    }
    for(int i=0;i<group_size;i++){
      //set num_ports and groups
      num_ports[i]=1;
      if(i==group_size-1){
        //set to negative to indicate end of a group
        groups[i]=-comm_size_row/my_cores_per_node_row;
      }
      else{
        groups[i]=comm_size_row/my_cores_per_node_row;
      }
    }
    //set final to zero to indicate the end
    groups[group_size]=0;
    num_ports[group_size]=0;
  } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation){
    if (my_cores_per_node_row * my_cores_per_node_column > 1) {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         scount * type_size,
                         my_cores_per_node_row * my_cores_per_node_column,
                         num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    } else {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         scount * type_size, 12, num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    }
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
      groups[i] = fixed_factors_groups[i];
    } while (fixed_factors_ports[i] > 0);
  }
  rcount = 0;
  for (i = 0; i < comm_size_row; i++) {
    rcount += recvcounts[i];
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = rcount < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1;
  }
  if (comm_size_row/my_cores_per_node_row>1){
    group_size=1;
  }else{
    group_size=0;
  }
  for (i=0; num_ports[i]; i++){
    group_size*=abs(num_ports[i])+1;
  }
  if (ext_mpi_verbose) {
    if (is_rank_zero(comm_row, comm_column)){
      if (group_size==comm_size_row/my_cores_per_node_row){
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
      comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
      num_ports, groups,
      my_cores_per_node_row * my_cores_per_node_column, alt, (group_size==comm_size_row/my_cores_per_node_row) && !not_recursive, ext_mpi_blocking, ext_mpi_num_sockets_per_node, 0, NULL);
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

int EXT_MPI_Allgatherv_init_general(const void *sendbuf, int sendcount,
                                    MPI_Datatype sendtype, void *recvbuf,
                                    const int *recvcounts, const int *displs,
                                    MPI_Datatype recvtype, MPI_Comm comm_row,
                                    int my_cores_per_node_row,
                                    MPI_Comm comm_column,
                                    int my_cores_per_node_column, int *handle) {
#ifdef DEBUG
  int comm_size_row, type_size, i;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  int world_rank, comm_rank_row, max_sendcount, max_displs, j, k;
#ifdef GPU_ENABLED
  void *sendbuf_hh = NULL, *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
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
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    sendbuf_hh = (void *)malloc(sendcount * type_size);
    if (!sendbuf_hh)
      goto error;
    recvbuf_hh = (long int *)malloc(j * type_size);
    if (!recvbuf_hh)
      goto error;
    recvbuf_ref_hh = (void *)malloc(j * type_size);
    if (!recvbuf_ref_hh)
      goto error;
    ext_mpi_gpu_malloc(&recvbuf_ref, j * type_size);
    if (!recvbuf_ref)
      goto error;
    ext_mpi_gpu_malloc(&sendbuf_h, sendcount * type_size);
    if (!sendbuf_h)
      goto error;
    for (i = 0; i < (int)((sendcount * type_size) / (int)sizeof(long int)); i++) {
      ((long int *)sendbuf_hh)[i] = world_rank * max_sendcount + i;
    }
//    if ((sendcount * type_size) % (int)sizeof(long int)){
//      ((int *)sendbuf_hh)[sendcount-1] = world_rank * max_sendcount + max_sendcount - 1;
//    }
    for (i = 0; i < (int)((j * type_size) / (int)sizeof(long int)); i++) {
      ((long int *)recvbuf_hh)[i] = -1;
    }
    ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, sendcount * type_size);
    ext_mpi_gpu_memcpy_hd(recvbuf_ref, recvbuf_hh, j * type_size);
    ext_mpi_gpu_memcpy_hd(recvbuf, recvbuf_hh, j * type_size);
  } else {
#endif
    recvbuf_ref = (void *)malloc(j * type_size);
    if (!recvbuf_ref)
      goto error;
    sendbuf_h = (void *)malloc(sendcount * type_size);
    if (!sendbuf_h)
      goto error;
    for (i = 0; i < (int)((sendcount * type_size) / (int)sizeof(long int)); i++) {
      ((long int *)sendbuf_h)[i] = world_rank * max_sendcount + i;
    }
#ifdef GPU_ENABLED
  }
#endif
  MPI_Allgatherv(sendbuf_h, sendcount, sendtype, recvbuf_ref, recvcounts, displs,
                 recvtype, comm_row);
  if (allgatherv_init_general(sendbuf_h, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
    goto error;
  if (EXT_MPI_Start_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Done_native(*handle) < 0)
    goto error;
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, j * type_size);
    ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, j * type_size);
    k = 0;
    for (j = 0; j < comm_size_row; j++) {
      for (i = 0; i < (int)((recvcounts[j] * type_size) / sizeof(long int));
           i++) {
        if (((long int *)
                 recvbuf_hh)[(displs[j] * type_size) / sizeof(long int) + i] !=
            ((long int *)
                 recvbuf_ref_hh)[(displs[j] * type_size) / sizeof(long int) + i]) {
          k = 1;
        }
      }
//      if ((int)((recvcounts[j] * type_size) % sizeof(long int)) {
//      }
    }
  } else {
#endif
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
//      if ((int)((recvcounts[j] * type_size) % sizeof(long int)) {
//      }
    }
#ifdef GPU_ENABLED
  }
#endif
  if (k) {
    printf("logical error in EXT_MPI_Allgatherv %d\n", world_rank);
    exit(1);
  }
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    ext_mpi_gpu_free(sendbuf_h);
    ext_mpi_gpu_free(recvbuf_ref);
    free(recvbuf_ref_hh);
    free(recvbuf_hh);
    free(sendbuf_hh);
  } else {
#endif
    free(sendbuf_h);
    free(recvbuf_ref);
#ifdef GPU_ENABLED
  }
#endif
#endif
  return allgatherv_init_general(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
#ifdef DEBUG
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
#endif
}

static int gatherv_init_general(const void *sendbuf, int sendcount,
                                MPI_Datatype sendtype, void *recvbuf,
                                const int *recvcounts, const int *displs,
                                MPI_Datatype recvtype, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle) {
  int comm_size_row, *num_ports = NULL, *groups = NULL, type_size, scount,
                     i, alt, rcount, group_size;
  char *str;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  PMPI_Allreduce(&sendcount, &scount, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &scount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
    if (comm_size_row/my_cores_per_node_row==1){
      group_size = 1;
    }else{
      group_size = ceil(log(comm_size_row/my_cores_per_node_row)/log(2));
    }
    for(int i=0;i<group_size;i++){
      num_ports[i]=1;
      if(i==group_size-1){
        groups[i]=-comm_size_row/my_cores_per_node_row;
      }
      else{
        groups[i]=comm_size_row/my_cores_per_node_row;
      }
    }
    groups[group_size]=0;
    num_ports[group_size]=0;
  } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation) {
    if (my_cores_per_node_row * my_cores_per_node_column > 1) {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         scount * type_size,
                         my_cores_per_node_row * my_cores_per_node_column,
                         num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    } else {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         scount * type_size, 12, num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    }
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
    } while (fixed_factors_ports[i] > 0);
  }
  for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
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
    alt = rcount < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1;
  }
  if (comm_size_row/my_cores_per_node_row>1){
    group_size=1;
  }else{
    group_size=0;
  }
  for (i=0; num_ports[i]; i++){
    group_size*=abs(num_ports[i])+1;
  }
  if (ext_mpi_verbose) {
    if (is_rank_zero(comm_row, comm_column)){
      if (group_size==comm_size_row/my_cores_per_node_row){
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
      root, comm_row, my_cores_per_node_row, comm_column,
      my_cores_per_node_column, num_ports, groups,
      my_cores_per_node_row * my_cores_per_node_column, alt, (group_size==comm_size_row/my_cores_per_node_row) && !not_recursive, ext_mpi_blocking, ext_mpi_num_sockets_per_node, 0, NULL);
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

int EXT_MPI_Gatherv_init_general(const void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 const int *recvcounts, const int *displs,
                                 MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *handle) {
#ifdef DEBUG
  int comm_size_row, type_size, world_rank, comm_rank_row, max_sendcount, max_displs, i, j, k;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
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
  sendbuf_h = (void *)malloc(sendcount * type_size);
  if (!sendbuf_h)
    goto error;
  for (i = 0; i < (int)((sendcount * type_size) / (int)sizeof(long int)); i++) {
    ((long int *)sendbuf_h)[i] = world_rank * max_sendcount + i;
  }
  MPI_Gatherv(sendbuf_h, sendcount, sendtype, recvbuf_ref, recvcounts, displs,
              recvtype, root, comm_row);
  if (gatherv_init_general(sendbuf_h, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
    goto error;
  if (EXT_MPI_Start_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Done_native(*handle) < 0)
    goto error;
  if (root == comm_rank_row) {
    k = 0;
    for (j = 0; j < comm_size_row; j++) {
      for (i = 0; i < (int)((recvcounts[j] * type_size) / (int)sizeof(long int));
           i++) {
        if (((long int *)
                 recvbuf)[(displs[j] * type_size) / (int)sizeof(long int) + i] !=
            ((long int *)
                 recvbuf_ref)[(displs[j] * type_size) / (int)sizeof(long int) + i]) {
          k = 1;
        }
      }
    }
    if (k) {
      printf("logical error in EXT_MPI_Allgatherv %d\n", world_rank);
      exit(1);
    }
  }
  free(sendbuf_h);
  free(recvbuf_ref);
#endif
  return gatherv_init_general(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
#ifdef DEBUG
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
#endif
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

static void revert_num_ports(int *num_ports, int *groups) {
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

static int reduce_scatter_init_general(
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle) {
  int comm_size_row, *num_ports = NULL, *groups = NULL, type_size, rcount, acount,
      i, alt, group_size, copyin_method_ = -1, *copyin_factors = NULL, num_sockets_per_node;
  char *str;
  MPI_Comm_size(comm_row, &comm_size_row);
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
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &rcount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
    if (comm_size_row/my_cores_per_node_row==1){
      group_size = 1;
    }else{
      group_size = ceil(log(comm_size_row/my_cores_per_node_row)/log(2));
    }
    for(int i=0;i<group_size;i++){
      num_ports[i]=-1;
      if(i==group_size-1){
        groups[i]=-comm_size_row/my_cores_per_node_row;
      }
      else{
        groups[i]=comm_size_row/my_cores_per_node_row;
      }
    }
    groups[group_size]=0;
    num_ports[group_size]=0;
  } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation){
    if (my_cores_per_node_row * my_cores_per_node_column > 1) {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         rcount * type_size,
                         my_cores_per_node_row * my_cores_per_node_column,
                         num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    } else {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         rcount * type_size, 12, num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    }
    revert_num_ports(num_ports, groups);
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
      groups[i] = fixed_factors_groups[i];
    } while (fixed_factors_ports[i]);
  }
  if (copyin_method < 0) {
    rcount = 0;
    for (i = 0; i < comm_size_row; i++) {
      rcount += recvcounts[i];
    }
#ifdef GPU_ENABLED
    if ((rcount * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && !ext_mpi_gpu_is_device_pointer(recvbuf) && ext_mpi_blocking) {
#else
    if ((rcount * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && ext_mpi_blocking) {
#endif
      copyin_method = 3;
    } else if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      copyin_method = (rcount * type_size >
                       ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      copyin_method = (rcount * type_size >
                       ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = rcount < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1;
  }
  if (comm_size_row/my_cores_per_node_row>1){
    group_size=1;
  }else{
    group_size=0;
  }
  for (i=0; num_ports[i]; i++){
    group_size*=abs(num_ports[i])+1;
  }
  if (ext_mpi_verbose) {
    if (is_rank_zero(comm_row, comm_column)){
      if (group_size==comm_size_row/my_cores_per_node_row){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      i = recvcounts[0];
      printf("# EXT_MPI reduce_scatter parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, i * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  acount = 0;
  for (i = 0; i < comm_size_row; i++) {
    acount += recvcounts[i];
  }
  num_sockets_per_node = ext_mpi_num_sockets_per_node;
  for (i = 0; i < comm_size_row + 1; i++) {
    copyin_factors[i] = 0;
  }
  if (comm_size_row > my_cores_per_node_row) {
    EXT_MPI_Allreduce_measurement(
        sendbuf, recvbuf, acount, datatype, op, comm_row, &my_cores_per_node_row,
        comm_column, my_cores_per_node_column,
        my_cores_per_node_row * my_cores_per_node_column, &copyin_method_, copyin_factors, &num_sockets_per_node);
    set_ports_single_node(ext_mpi_num_sockets_per_node, num_sockets_per_node, num_ports, groups);
  }
  *handle = EXT_MPI_Reduce_scatter_init_native(
      sendbuf, recvbuf, recvcounts, datatype, op, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, my_cores_per_node_row * my_cores_per_node_column,
      copyin_method_, copyin_factors, alt, (group_size==comm_size_row/my_cores_per_node_row) && !not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL, NULL);
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

int EXT_MPI_Reduce_scatter_init_general(
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle) {
#ifdef DEBUG
  int comm_size_row, type_size, i, j;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  int world_rank, comm_rank_row, tsize;
#ifdef GPU_ENABLED
  void *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL, *sendbuf_hh = NULL;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    tsize = 0;
    for (i = 0; i < comm_size_row; i++) {
      tsize += recvcounts[i];
    }
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      recvbuf_ref_hh = malloc(recvcounts[comm_rank_row] * type_size);
      if (!recvbuf_ref_hh)
        goto error;
      sendbuf_hh = malloc(tsize * type_size);
      if (!sendbuf_hh)
        goto error;
      for (i = 0; i < (tsize * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_hh)[i] = world_rank * tsize + i;
      }
      if ((tsize * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_hh)[tsize-1] = world_rank * tsize + tsize - 1;
      }
      ext_mpi_gpu_malloc(&recvbuf_ref, recvcounts[comm_rank_row] * type_size);
      ext_mpi_gpu_malloc(&sendbuf_h, tsize * type_size);
      ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, tsize * type_size);
    } else {
#endif
      recvbuf_ref = malloc(recvcounts[comm_rank_row] * type_size);
      if (!recvbuf_ref)
        goto error;
      sendbuf_h = malloc(tsize * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (tsize * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rank * tsize + i;
      }
      if ((tsize * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_h)[tsize-1] = world_rank * tsize + tsize - 1;
      }
#ifdef GPU_ENABLED
    }
#endif
    if (type_size == sizeof(long int)){
      MPI_Reduce_scatter(sendbuf_h, recvbuf_ref, recvcounts, MPI_LONG, op, comm_row);
      if (reduce_scatter_init_general(sendbuf_h, recvbuf, recvcounts, MPI_LONG, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
        goto error;
    }else{
      MPI_Reduce_scatter(sendbuf_h, recvbuf_ref, recvcounts, MPI_INT, op, comm_row);
      if (reduce_scatter_init_general(sendbuf_h, recvbuf, recvcounts, MPI_INT, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
        goto error;
    }
    if (EXT_MPI_Start_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      recvbuf_hh = malloc(recvcounts[comm_rank_row] * type_size);
      if (!recvbuf_hh)
        goto error;
      ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, recvcounts[comm_rank_row] * type_size);
      ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, recvcounts[comm_rank_row] * type_size);
      j = 0;
      for (i = 0; i < (recvcounts[comm_rank_row] * type_size) / (int)sizeof(long int); i++) {
        if (((long int *)recvbuf_hh)[i] != ((long int *)recvbuf_ref_hh)[i]) {
          j = 1;
        }
      }
      if ((recvcounts[comm_rank_row] * type_size) % (int)sizeof(long int)){
        if (((int *)recvbuf_hh)[recvcounts[comm_rank_row]-1] != ((int *)recvbuf_ref_hh)[recvcounts[comm_rank_row]-1]) {
          j = 1;
        }
      }
      free(sendbuf_hh);
      free(recvbuf_hh);
      free(recvbuf_ref_hh);
      ext_mpi_gpu_free(sendbuf_h);
      ext_mpi_gpu_free(recvbuf_ref);
    } else {
#endif
      j = 0;
      for (i = 0; i < (recvcounts[comm_rank_row] * type_size) / (int)sizeof(long int); i++) {
        if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
          j = 1;
        }
      }
      if ((recvcounts[comm_rank_row] * type_size) % (int)sizeof(long int)){
        if (((int *)recvbuf)[recvcounts[comm_rank_row]-1] != ((int *)recvbuf_ref)[recvcounts[comm_rank_row]-1]) {
          j = 1;
        }
      }
      free(sendbuf_h);
      free(recvbuf_ref);
#ifdef GPU_ENABLED
    }
#endif
    if (j) {
      printf("logical error in EXT_MPI_Reduce_scatter %d\n", world_rank);
      exit(1);
    }
  }
#endif
  return reduce_scatter_init_general(sendbuf, recvbuf, recvcounts, datatype, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
#ifdef DEBUG
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
#endif
}

static int scatterv_init_general(const void *sendbuf, const int *sendcounts, const int *displs,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype,
                                 int root, MPI_Comm comm_row,
                                 int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *handle) {
  int comm_size_row, *num_ports = NULL, *groups = NULL, type_size, rcount,
                     i, j, k, alt, group_size;
  char *str;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  rcount = recvcount;
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &rcount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
    if (comm_size_row/my_cores_per_node_row==1){
      group_size = 1;
    }else{
      group_size = ceil(log(comm_size_row/my_cores_per_node_row)/log(2));
    }
    for(int i=0;i<group_size;i++){
      num_ports[i]=1;
      if(i==group_size-1){
        groups[i]=-comm_size_row/my_cores_per_node_row;
      }
      else{
        groups[i]=comm_size_row/my_cores_per_node_row;
      }
    }
    groups[group_size]=0;
    num_ports[group_size]=0;
  } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation) {
    if (my_cores_per_node_row * my_cores_per_node_column > 1) {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         rcount * type_size,
                         my_cores_per_node_row * my_cores_per_node_column,
                         num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    } else {
      if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node_row,
                         rcount * type_size, 12, num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
        goto error;
    }
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
    } while (fixed_factors_ports[i] > 0);
  }
  for (i = 0; num_ports[i]; i++) {
  }
  for (j = 0; j < i / 2; j++) {
    k = num_ports[j];
    num_ports[j] = num_ports[i - 1 - j];
    num_ports[i - 1 - j] = k;
  }
  if (copyin_method < 0) {
    rcount = 0;
    for (i = 0; i < comm_size_row; i++) {
      rcount += sendcounts[i];
    }
    if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      copyin_method = (rcount * type_size >
                       ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      copyin_method = (rcount * type_size >
                       ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = rcount < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1;
  }
  if (comm_size_row/my_cores_per_node_row>1){
    group_size=1;
  }else{
    group_size=0;
  }
  for (i=0; num_ports[i]; i++){
    group_size*=abs(num_ports[i])+1;
  }
  if (ext_mpi_verbose) {
    if (is_rank_zero(comm_row, comm_column)){
      if (group_size==comm_size_row/my_cores_per_node_row){
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
      root, comm_row, my_cores_per_node_row, comm_column,
      my_cores_per_node_column, num_ports, groups,
      my_cores_per_node_row * my_cores_per_node_column, copyin_method, alt, (group_size==comm_size_row/my_cores_per_node_row) && !not_recursive, ext_mpi_blocking, ext_mpi_num_sockets_per_node, 0, NULL);
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

int EXT_MPI_Scatterv_init_general(const void *sendbuf, const int *sendcounts, const int *displs,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  int root, MPI_Comm comm_row,
                                  int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *handle) {
#ifdef DEBUG
  int comm_size_row, type_size, world_rank, comm_rank_row, i, j, k;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  recvbuf_ref = (long int *)malloc(recvcount * type_size);
  if (!recvbuf_ref)
    goto error;
  sendbuf_h = malloc(
      (displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) * type_size);
  if (!sendbuf_h)
    goto error;
  k = 0;
  for (i = 0; i < comm_size_row; i++) {
    for (j = 0; j < (sendcounts[i] * type_size) / (int)sizeof(long int); j++) {
      ((long int *)((char *)sendbuf_h + displs[i] * type_size))[j] =
          world_rank *
              (((displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) *
                type_size) /
               (int)sizeof(long int)) +
          k++;
    }
  }
  MPI_Scatterv(sendbuf_h, sendcounts, displs, MPI_LONG, recvbuf_ref, recvcount,
               MPI_LONG, root, comm_row);
  if (scatterv_init_general(sendbuf_h, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
    goto error;
  if (EXT_MPI_Start_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait_native(*handle) < 0)
    goto error;
  if (EXT_MPI_Done_native(*handle) < 0)
    goto error;
  j = 0;
  for (i = 0; i < (recvcount * type_size) / (int)sizeof(long int); i++) {
    if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
      j = 1;
    }
  }
  if (j) {
    printf("logical error in EXT_MPI_Scatterv %d\n", world_rank);
    exit(1);
  }
  free(sendbuf_h);
  free(recvbuf_ref);
#endif
  return scatterv_init_general(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
#ifdef DEBUG
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
#endif
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

static int allreduce_init_general(const void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *handle) {
  int comm_size_row, comm_rank_row, i, j, alt, comm_size_column, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method_, *copyin_factors = NULL, num_sockets_per_node, factors_max_max = 0, *factors_max, **factors, *primes;
  double d1;
  struct cost_list *p1, *p2;
  struct {
    double value;
    int rank;
  } composition;
  char *str;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  copyin_factors = (int*) malloc(sizeof(int) * (comm_size_row + 1));
  if (!copyin_factors)
    goto error;
  MPI_Type_size(datatype, &type_size);
  if (sendbuf == MPI_IN_PLACE) {
    sendbuf = recvbuf;
  }
  message_size = type_size * count;
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &message_size, 1, MPI_INT, MPI_SUM,
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
  if (fixed_factors_ports == NULL && comm_size_row == my_cores_per_node_row * ext_mpi_num_sockets_per_node && ext_mpi_num_sockets_per_node == 2) {
    set_ports_single_node(1, 2, num_ports, groups);
  } else if (fixed_factors_ports == NULL && comm_size_row == my_cores_per_node_row * ext_mpi_num_sockets_per_node && ext_mpi_num_sockets_per_node == 4) {
    set_ports_single_node(1, 4, num_ports, groups);
  } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation) {
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
    if (comm_size_row / my_cores_per_node_row > 1 && !new_factors_heuristic) {
      if (my_cores_per_node_row == 1) {
        if (ext_mpi_cost_simulation(count, type_size, comm_size_row * 12, 12,
                                    comm_size_column, my_cores_per_node_column,
                                    comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simulation(count, type_size, comm_size_row, my_cores_per_node_row,
                                           comm_size_column, my_cores_per_node_column,
                                           comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      }
      p1 = ext_mpi_cost_list_start;
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
      ext_mpi_cost_list_start = NULL;
      ext_mpi_cost_list_length = 0;
      ext_mpi_cost_list_counter = 0;
      composition.value = d1;
      composition.rank = comm_rank_row;
      PMPI_Allreduce(MPI_IN_PLACE, &composition, 1, MPI_DOUBLE_INT, MPI_MINLOC,
                     comm_row);
      d1 = composition.value;
      for (i = 0; num_ports[i]; i++)
        ;
      MPI_Bcast(&i, 1, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(num_ports, i, MPI_INT, composition.rank, comm_row);
      MPI_Bcast(groups, i, MPI_INT, composition.rank, comm_row);
      groups[i] = num_ports[i] = 0;
    } else if (comm_size_row / my_cores_per_node_row > 1) {
      if (comm_rank_row == 0) {
        factors_max_max = ext_mpi_heuristic_recursive_non_factors(comm_size_row/my_cores_per_node_row, 0, &factors_max, &factors, &primes);
	if (my_cores_per_node_row > 1) {
	  for (i = 0; i < factors_max_max; i++) {
	    primes[i] = 0;
	  }
	}
        ext_mpi_min_cost_total(message_size, factors_max_max, factors_max, factors, primes, &i);
        for (j = 0; j < factors_max[i]; j++) {
          if (factors[i][j] > 0) {
            num_ports[j] = factors[i][j] - 1;
          } else {
            num_ports[j] = factors[i][j] + 1;
          }
          groups[j] = comm_size_row/my_cores_per_node_row;
        }
        groups[j - 1] = -groups[j - 1];
      }
      MPI_Bcast(&j, 1, MPI_INT, 0, comm_row);
      MPI_Bcast(num_ports, j, MPI_INT, 0, comm_row);
      MPI_Bcast(groups, j, MPI_INT, 0, comm_row);
      groups[j] = num_ports[j] = 0;
      if (comm_rank_row == 0) {
        free(primes);
        for (i = 0; i < factors_max_max; i++) {
          free(factors[i]);
        }
        free(factors);
        free(factors_max);
      }
    } else {
      groups[0] = num_ports[0] = -1;
      groups[1] = num_ports[1] = 0;
    }
    // FIXME comm_column
  } else if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
    //allreduce case for minimum computation
    if (comm_size_row/my_cores_per_node_row==1){
      //if only one node
      group_size = 1;
    }else{
      //set group_size to 2^group_size <= num_nodes
      group_size = ceil(log(comm_size_row/my_cores_per_node_row)/log(2));
    }
    for(int i=0;i<2*group_size;i++){
      //iterate over the two groups
      if(i<group_size){
        //set num_ports to -1 for the first group
        num_ports[i]=-1;
        if(i==group_size-1){
          //set the final value of a group to a negative value to indicate the end of the group
          groups[i]=-comm_size_row/my_cores_per_node_row;
        }
        else{
          groups[i]=comm_size_row/my_cores_per_node_row;
        }
      }
      else{
        //set num_ports to 1 for the second group
        num_ports[i]=1;
        if(i==2*group_size-1){
          groups[i]=-comm_size_row/my_cores_per_node_row;
        }
        else{
          groups[i]=comm_size_row/my_cores_per_node_row;
        }
      }
    }
    //set the final value of the array to 0 to indicate the end of it
    groups[2*group_size]=0;
    num_ports[2*group_size]=0;
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
      groups[i] = fixed_factors_groups[i];
    } while (fixed_factors_ports[i]);
  }
  if (copyin_method < 0) {
#ifdef GPU_ENABLED
    if ((count * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && !ext_mpi_gpu_is_device_pointer(recvbuf) && ext_mpi_blocking) {
#else
    if ((count * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && ext_mpi_blocking) {
#endif
      copyin_method = 3;
    } else if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      copyin_method = (count * type_size >
                       ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      copyin_method =
          (count * type_size > ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = count * type_size < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1;
  }
  if (comm_size_row/my_cores_per_node_row>1){
    group_size=1;
  }else{
    group_size=0;
  }
  for (i=0; num_ports[i]; i++){
    if (num_ports[i]>0){
      group_size*=abs(num_ports[i])+1;
    }
  }
  if (ext_mpi_verbose) {
    if (is_rank_zero(comm_row, comm_column)) {
      if (group_size==comm_size_row/my_cores_per_node_row){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI allreduce parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, message_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  num_sockets_per_node = ext_mpi_num_sockets_per_node;
  EXT_MPI_Allreduce_measurement(
      sendbuf, recvbuf, count, datatype, op, comm_row, &my_cores_per_node_row,
      comm_column, my_cores_per_node_column,
      my_cores_per_node_row * my_cores_per_node_column, &copyin_method_, copyin_factors, &num_sockets_per_node);
  set_ports_single_node(ext_mpi_num_sockets_per_node, num_sockets_per_node, num_ports, groups);
  *handle = EXT_MPI_Allreduce_init_native(
      sendbuf, recvbuf, count, datatype, op, comm_row, my_cores_per_node_row,
      comm_column, my_cores_per_node_column, num_ports, groups,
      my_cores_per_node_row * my_cores_per_node_column, copyin_method_, copyin_factors, alt,
      ext_mpi_bit_identical, !ext_mpi_bit_reproducible, (group_size==comm_size_row/my_cores_per_node_row) && !not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL, NULL);
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

int EXT_MPI_Allreduce_init_general(const void *sendbuf, void *recvbuf, int count,
                                   MPI_Datatype datatype, MPI_Op op,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle) {
#ifdef DEBUG
  int comm_size_row, comm_rank_row;
  int type_size, i, j;
#ifdef GPU_ENABLED
  void *sendbuf_hh = NULL, *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL;
#endif
  int world_rankd;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    sendbuf_hh = (long int *)malloc(count * type_size);
    if (!sendbuf_hh)
      goto error;
    recvbuf_hh = (long int *)malloc(count * type_size);
    if (!recvbuf_hh)
      goto error;
    recvbuf_ref_hh = (long int *)malloc(count * type_size);
    if (!recvbuf_ref_hh)
      goto error;
    ext_mpi_gpu_malloc(&recvbuf_ref, count * type_size);
    if (!recvbuf_ref)
      goto error;
    if (sendbuf != MPI_IN_PLACE){
      ext_mpi_gpu_malloc(&sendbuf_h, count * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_hh)[i] = world_rankd * count + i;
        ((long int *)recvbuf_hh)[i] = -1;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_hh)[count-1] = world_rankd * count + count - 1;
      }
      ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, count * type_size);
      ext_mpi_gpu_memcpy_hd(recvbuf, recvbuf_hh, count * type_size);
    }else{
      sendbuf_h = MPI_IN_PLACE;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)recvbuf_hh)[i] = world_rankd * count + i;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)recvbuf_hh)[count-1] = world_rankd * count + count - 1;
      }
      ext_mpi_gpu_memcpy_hd(recvbuf, recvbuf_hh, count * type_size);
      ext_mpi_gpu_memcpy_hd(recvbuf_ref, recvbuf_hh, count * type_size);
    }
    if (type_size == sizeof(long int)){
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_LONG, op, comm_row);
      if (allreduce_init_general(sendbuf_h, recvbuf, count, MPI_LONG, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle)<0)
        goto error;
    }else{
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_INT, op, comm_row);
      if (allreduce_init_general(sendbuf_h, recvbuf, count, MPI_INT, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle)<0)
        goto error;
    }
    if (EXT_MPI_Start_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
    ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, count * type_size);
    ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, count * type_size);
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      if (((long int *)recvbuf_hh)[i] != ((long int *)recvbuf_ref_hh)[i]) {
        j = 1;
      }
    }
    if ((count * type_size) % (int)sizeof(long int)){
      if (((int *)recvbuf_hh)[count-1] != ((int *)recvbuf_ref_hh)[count-1]){
        j = 1;
      }
    }
    if (sendbuf != MPI_IN_PLACE){
      ext_mpi_gpu_free(sendbuf_h);
    }
    free(recvbuf_ref_hh);
    free(recvbuf_hh);
    free(sendbuf_hh);
  } else {
#endif
    recvbuf_ref = (long int *)malloc(count * type_size);
    if (!recvbuf_ref)
      goto error;
    if (sendbuf != MPI_IN_PLACE){
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rankd * count + i;
        ((long int *)recvbuf)[i] = -1;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_h)[count-1] = world_rankd * count + count - 1;
      }
    }else{
      sendbuf_h = MPI_IN_PLACE;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)recvbuf)[i] = ((long int *)recvbuf_ref)[i] = world_rankd * count + i;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)recvbuf)[count-1] = ((int *)recvbuf_ref)[count-1] = world_rankd * count + count - 1;
      }
    }
    if (type_size == sizeof(long int)){
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_LONG, op, comm_row);
      if (allreduce_init_general(sendbuf_h, recvbuf, count, MPI_LONG, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle)<0)
        goto error;
    }else{
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_INT, op, comm_row);
      if (allreduce_init_general(sendbuf_h, recvbuf, count, MPI_INT, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle)<0)
        goto error;
    }
    if (EXT_MPI_Start_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
        j = 1;
      }
    }
    if ((count * type_size) % (int)sizeof(long int)){
      if (((int *)recvbuf)[count-1] != ((int *)recvbuf_ref)[count-1]){
        j = 1;
      }
    }
    if (sendbuf != MPI_IN_PLACE){
      free(sendbuf_h);
    }
    free(recvbuf_ref);
#ifdef GPU_ENABLED
  }
#endif
  if (j) {
    printf("logical error in EXT_MPI_Allreduce %d\n", world_rankd);
    exit(1);
  }
#endif
  if (sendbuf == MPI_IN_PLACE) {
    return allreduce_init_general(recvbuf, recvbuf, count, datatype, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
  }else{
    return allreduce_init_general(sendbuf, recvbuf, count, datatype, op, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
  }
#ifdef DEBUG
error:
#ifdef GPU_ENABLED
  free(recvbuf_ref_hh);
  free(recvbuf_hh);
  free(sendbuf_hh);
  if (sendbuf != MPI_IN_PLACE){
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      ext_mpi_gpu_free(sendbuf_h);
    }
    sendbuf_h = NULL;
  }
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    ext_mpi_gpu_free(recvbuf_ref);
    recvbuf_ref = NULL;
  }
#endif
  if (sendbuf != MPI_IN_PLACE){
    free(sendbuf_h);
  }
  free(recvbuf_ref);
  return ERROR_MALLOC;
#endif
}

static int reduce_init_general(const void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *handle) {
  int comm_size_row, comm_rank_row, i, alt, comm_size_column, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method_, *copyin_factors, num_sockets_per_node;
  double d1;
  char *str;
  struct cost_list *p1, *p2;
  struct {
    double value;
    int rank;
  } composition;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  copyin_factors = (int*) malloc(sizeof(int) * (comm_size_row + 1));
  MPI_Type_size(datatype, &type_size);
  if (sendbuf == MPI_IN_PLACE) {
    sendbuf = recvbuf;
  }
  message_size = type_size * count;
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &message_size, 1, MPI_INT, MPI_SUM,
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
  if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
    if (comm_size_row/my_cores_per_node_row==1){
      group_size = 1;
    }else{
      group_size = ceil(log(comm_size_row/my_cores_per_node_row)/log(2));
    }
    for(int i=0;i<group_size;i++){
      num_ports[i]=-1;
      if(i==group_size-1){
        groups[i]=-comm_size_row/my_cores_per_node_row;
      }
      else{
        groups[i]=comm_size_row/my_cores_per_node_row;
      }
    }
    groups[group_size]=0;
    num_ports[group_size]=0;
  } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation){
    if (comm_size_row / my_cores_per_node_row > 1) {
      if (my_cores_per_node_row == 1) {
        if (ext_mpi_cost_simulation(count, type_size, comm_size_row * 12, 12,
                                    comm_size_column, my_cores_per_node_column,
                                    comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simulation(count, type_size, comm_size_row, my_cores_per_node_row,
                                    comm_size_column, my_cores_per_node_column,
                                    comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      }
      p1 = ext_mpi_cost_list_start;
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
      ext_mpi_cost_list_start = NULL;
      ext_mpi_cost_list_length = 0;
      ext_mpi_cost_list_counter = 0;
      composition.value = d1;
      composition.rank = comm_rank_row;
      PMPI_Allreduce(MPI_IN_PLACE, &composition, 1, MPI_DOUBLE_INT, MPI_MINLOC,
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
  if (copyin_method < 0) {
#ifdef GPU_ENABLED
    if ((count * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && !ext_mpi_gpu_is_device_pointer(recvbuf) && ext_mpi_blocking) {
#else
    if ((count * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && ext_mpi_blocking) {
#endif
      copyin_method = 3;
    } else if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      copyin_method = (count * type_size >
                       ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      copyin_method =
          (count * type_size > ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = count * type_size < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1;
  }
  if (comm_size_row/my_cores_per_node_row>1){
    group_size=1;
  }else{
    group_size=0;
  }
  for (i=0; num_ports[i]; i++){
    group_size*=abs(num_ports[i])+1;
  }
  if (ext_mpi_verbose) {
    if (is_rank_zero(comm_row, comm_column)){
      if (group_size==comm_size_row/my_cores_per_node_row){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI reduce parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, count * message_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  num_sockets_per_node = ext_mpi_num_sockets_per_node;
  EXT_MPI_Allreduce_measurement(
      sendbuf, recvbuf, count, datatype, op, comm_row, &my_cores_per_node_row,
      comm_column, my_cores_per_node_column,
      my_cores_per_node_row * my_cores_per_node_column, &copyin_method_, copyin_factors, &num_sockets_per_node);
  *handle = EXT_MPI_Reduce_init_native(
      sendbuf, recvbuf, count, datatype, op, root, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, my_cores_per_node_row * my_cores_per_node_column, copyin_method_, copyin_factors, alt,
      0, !ext_mpi_bit_reproducible, (group_size==comm_size_row/my_cores_per_node_row) && !not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL, NULL);
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

int EXT_MPI_Reduce_init_general(const void *sendbuf, void *recvbuf, int count,
                                MPI_Datatype datatype, MPI_Op op, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle) {
#ifdef DEBUG
  int comm_size_row, comm_rank_row, type_size, i, j;
#ifdef GPU_ENABLED
  void *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL, *sendbuf_hh = NULL;
#endif
  int world_rankd;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(sendbuf)) {
      ext_mpi_gpu_malloc(&sendbuf_h, count * type_size);
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      ext_mpi_gpu_malloc(&recvbuf_ref, count * type_size);
      if (!recvbuf_ref)
        goto error;
      recvbuf_hh = (long int *)malloc(count * type_size);
      if (!recvbuf_hh)
        goto error;
      recvbuf_ref_hh = (long int *)malloc(count * type_size);
      if (!recvbuf_ref_hh)
        goto error;
      sendbuf_hh = (long int *)malloc(count * type_size);
      if (!recvbuf_ref_hh)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_hh)[i] = world_rankd * count + i;
      }
      ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, count * type_size);
      MPI_Reduce(sendbuf, recvbuf_ref, count, MPI_LONG, MPI_SUM, root,
                 comm_row);
      EXT_MPI_Start_native(*handle);
      EXT_MPI_Wait_native(*handle);
      ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, count * type_size);
      ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, count * type_size);
      j = 0;
      if (root == comm_rank_row) {
        for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
          if (((long int *)recvbuf_hh)[i] != ((long int *)recvbuf_ref_hh)[i]) {
            j = 1;
          }
        }
      }
      free(recvbuf_ref_hh);
      free(recvbuf_hh);
      free(sendbuf_hh);
      ext_mpi_gpu_free(recvbuf_ref);
      ext_mpi_gpu_free(sendbuf_h);
    } else {
#endif
      recvbuf_ref = (long int *)malloc(count * type_size);
      if (!recvbuf_ref)
        goto error;
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rankd * count + i;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_h)[count-1] = world_rankd * count + count - 1;
      }
      if (type_size == sizeof(long int)){
        MPI_Reduce(sendbuf_h, recvbuf_ref, count, MPI_LONG, op, root, comm_row);
        if (reduce_init_general(sendbuf_h, recvbuf, count, MPI_LONG, op, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
          goto error;
      }else{
        MPI_Reduce(sendbuf_h, recvbuf_ref, count, MPI_INT, op, root, comm_row);
        if (reduce_init_general(sendbuf_h, recvbuf, count, MPI_INT, op, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
          goto error;
      }
      if (EXT_MPI_Start_native(*handle) < 0)
        goto error;
      if (EXT_MPI_Wait_native(*handle) < 0)
        goto error;
      if (EXT_MPI_Done_native(*handle) < 0)
        goto error;
      j = 0;
      if (root == comm_rank_row) {
        for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
          if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
            j = 1;
          }
        }
        if ((count * type_size) % (int)sizeof(long int)){
          if (((int *)recvbuf)[count-1] != ((int *)recvbuf_ref)[count-1]) {
            j = 1;
          }
        }
      }
      free(sendbuf_h);
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
  if (sendbuf == MPI_IN_PLACE) {
    return reduce_init_general(recvbuf, recvbuf, count, datatype, op, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
  }else{
    return reduce_init_general(sendbuf, recvbuf, count, datatype, op, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
  }
#ifdef DEBUG
error:
#ifdef GPU_ENABLED
  free(recvbuf_ref_hh);
  free(recvbuf_hh);
  free(sendbuf_hh);
  if (ext_mpi_gpu_is_device_pointer(sendbuf)){
    ext_mpi_gpu_free(recvbuf_ref);
    recvbuf_ref = NULL;
    ext_mpi_gpu_free(sendbuf_h);
    sendbuf_h = NULL;
  }
#endif
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
#endif
}

static int bcast_init_general(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *handle) {
  int comm_size_row, comm_rank_row, i, alt, comm_size_column, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method_, *copyin_factors, num_sockets_per_node;
  double d1;
  char *str;
  struct cost_list *p1, *p2;
  struct {
    double value;
    int rank;
  } composition;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  copyin_factors = (int*) malloc(sizeof(int) * (comm_size_row + 1));
  MPI_Type_size(datatype, &type_size);
  message_size = type_size * count;
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &message_size, 1, MPI_INT, MPI_SUM,
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
  if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
    if (comm_size_row/my_cores_per_node_row==1){
      group_size = 1;
    }else{
      group_size = ceil(log(comm_size_row/my_cores_per_node_row)/log(2));
    }
    for(int i=0;i<group_size;i++){
      num_ports[i]=1;
      if(i==group_size-1){
        groups[i]=-comm_size_row/my_cores_per_node_row;
      }
      else{
        groups[i]=comm_size_row/my_cores_per_node_row;
      }
    }
    groups[group_size]=0;
    num_ports[group_size]=0;
  } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation){
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
        if (ext_mpi_cost_simulation(count, type_size, comm_size_row * 12, 12,
                                    comm_size_column, my_cores_per_node_column,
                                    comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simulation(count, type_size, comm_size_row, my_cores_per_node_row,
                                    comm_size_column, my_cores_per_node_column,
                                    comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      }
      p1 = ext_mpi_cost_list_start;
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
      ext_mpi_cost_list_start = NULL;
      ext_mpi_cost_list_length = 0;
      ext_mpi_cost_list_counter = 0;
      composition.value = d1;
      composition.rank = comm_rank_row;
      PMPI_Allreduce(MPI_IN_PLACE, &composition, 1, MPI_DOUBLE_INT, MPI_MINLOC,
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
  if (copyin_method < 0) {
    if (my_cores_per_node_row > ext_mpi_node_size_threshold_max) {
      copyin_method = (count * type_size >
                       ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
    } else {
      copyin_method =
          (count * type_size > ext_mpi_node_size_threshold[my_cores_per_node_row - 1]);
    }
  }
  alt = 0;
  if (alternating >= 1) {
    alt = alternating - 1;
  } else {
    alt = count * type_size < 10000000 && my_cores_per_node_row * my_cores_per_node_column > 1;
  }
  if (comm_size_row/my_cores_per_node_row>1){
    group_size=1;
  }else{
    group_size=0;
  }
  for (i=0; num_ports[i]; i++){
    group_size*=abs(num_ports[i])+1;
  }
  if (ext_mpi_verbose) {
    if (is_rank_zero(comm_row, comm_column)){
      if (group_size==comm_size_row/my_cores_per_node_row){
        printf("# recursive\n");
      }
      str = ext_mpi_print_ports_groups(num_ports, groups);
      printf("# EXT_MPI bcast parameters %d %d %d %d ports %s\n",
             comm_size_row / my_cores_per_node_row, message_size, 1,
             my_cores_per_node_row * my_cores_per_node_column, str);
      free(str);
    }
  }
  num_sockets_per_node = ext_mpi_num_sockets_per_node;
  EXT_MPI_Allreduce_measurement(
      buffer, buffer, count, datatype, MPI_OP_NULL, comm_row, &my_cores_per_node_row,
      comm_column, my_cores_per_node_column,
      my_cores_per_node_row * my_cores_per_node_column, &copyin_method_, copyin_factors, &num_sockets_per_node);
  *handle = EXT_MPI_Bcast_init_native(
      buffer, count, datatype, root, comm_row, my_cores_per_node_row,
      comm_column, my_cores_per_node_column, num_ports, groups,
      my_cores_per_node_row * my_cores_per_node_column, copyin_method_, copyin_factors, alt, (group_size==comm_size_row/my_cores_per_node_row) && !not_recursive, ext_mpi_blocking, num_sockets_per_node, 0, NULL);
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

int EXT_MPI_Bcast_init_general(void *buffer, int count, MPI_Datatype datatype,
                               int root, MPI_Comm comm_row,
                               int my_cores_per_node_row, MPI_Comm comm_column,
                               int my_cores_per_node_column, int *handle) {
#ifdef DEBUG
  int comm_size_row, comm_rank_row, type_size, i, j;
#ifdef GPU_ENABLED
  void *buffer_hh = NULL, *buffer_ref_hh = NULL;
#endif
  int world_rankd;
  void *buffer_ref = NULL, *buffer_org = NULL;
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(buffer)) {
    ext_mpi_gpu_malloc(&buffer_ref, count * type_size);
    if (!buffer_ref)
      goto error;
    buffer_hh = (long int *)malloc(count * type_size);
    if (!buffer_hh)
      goto error;
    buffer_ref_hh = (long int *)malloc(count * type_size);
    if (!buffer_ref_hh)
      goto error;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      ((long int *)buffer_hh)[i] = world_rankd * count + i;
    }
    ext_mpi_gpu_memcpy_hd(buffer_ref, buffer_hh, count * type_size);
    MPI_Bcast(buffer_hh, count, MPI_LONG, root, comm_row);
    if (bcast_init_general(buffer_ref, count, datatype, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
    if (EXT_MPI_Start_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
    ext_mpi_gpu_memcpy_dh(buffer_ref_hh, buffer_ref, count * type_size);
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      if (((long int *)buffer_ref_hh)[i] != ((long int *)buffer_hh)[i]) {
        j = 1;
      }
    }
    free(buffer_ref_hh);
    free(buffer_hh);
    ext_mpi_gpu_free(buffer_ref);
  } else {
#endif
    buffer_ref = (long int *)malloc(count * type_size);
    if (!buffer_ref)
      goto error;
    buffer_org = (long int *)malloc(count * type_size);
    if (!buffer_org)
      goto error;
    memcpy(buffer_org, buffer, count * type_size);
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      ((long int *)buffer_ref)[i] = ((long int *)buffer)[i] =
          world_rankd * count + i + 1000;
    }
    MPI_Bcast(buffer_ref, count, datatype, root, comm_row);
    if (bcast_init_general(buffer, count, datatype, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle) < 0)
      goto error;
    if (EXT_MPI_Start_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait_native(*handle) < 0)
      goto error;
    if (EXT_MPI_Done_native(*handle) < 0)
      goto error;
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
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
  return bcast_init_general(buffer, count, datatype, root, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
#ifdef DEBUG
error:
#ifdef GPU_ENABLED
  free(buffer_ref_hh);
  free(buffer_hh);
  if (ext_mpi_gpu_is_device_pointer(buffer)) {
    ext_mpi_gpu_free(buffer_ref);
    buffer_ref = NULL;
  }
#endif
  free(buffer_org);
  free(buffer_ref);
  return ERROR_MALLOC;
#endif
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
                            MPI_Datatype recvtype, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allgatherv_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
        comm, num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return -1;
  }
}

int EXT_MPI_Allgather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allgather_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm,
        num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return -1;
  }
}

int EXT_MPI_Reduce_scatter_init(const void *sendbuf, void *recvbuf, const int *recvcounts,
                                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                                int *handle) {
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_scatter_init_general(
        sendbuf, recvbuf, recvcounts, datatype, op, comm, num_core_per_node,
        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return -1;
  }
}

int EXT_MPI_Reduce_scatter_block_init(void *sendbuf, void *recvbuf,
                                      int recvcount, MPI_Datatype datatype,
                                      MPI_Op op, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_scatter_block_init_general(
        sendbuf, recvbuf, recvcount, datatype, op, comm, num_core_per_node,
        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Allreduce_init(const void *sendbuf, void *recvbuf, int count,
                           MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                           int *handle) {
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allreduce_init_general(sendbuf, recvbuf, count, datatype,
                                           op, comm, num_core_per_node,
                                           MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return -1;
  }
}

int EXT_MPI_Bcast_init(void *sendbuf, int count, MPI_Datatype datatype,
                       int root, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Bcast_init_general(sendbuf, count, datatype, root, comm,
                                       num_core_per_node, MPI_COMM_NULL, 1,
                                       handle));
  } else {
    *handle = -1;
    return -1;
  }
}

int EXT_MPI_Reduce_init(const void *sendbuf, void *recvbuf, int count,
                        MPI_Datatype datatype, MPI_Op op, int root,
                        MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_init_general(sendbuf, recvbuf, count, datatype, op,
                                        root, comm, num_core_per_node,
                                        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return -1;
  }
}

int EXT_MPI_Gatherv_init(const void *sendbuf, int sendcount,
                         MPI_Datatype sendtype, void *recvbuf,
                         const int *recvcounts, const int *displs,
                         MPI_Datatype recvtype, int root,
                         MPI_Comm comm, int *handle){
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Gatherv_init_general(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
                                         root, comm, num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return -1;
  }
}

int EXT_MPI_Scatterv_init(const void *sendbuf, const int *sendcounts, const int *displs,
                          MPI_Datatype sendtype, void *recvbuf,
                          int recvcount, MPI_Datatype recvtype,
                          int root, MPI_Comm comm, int *handle){
  int num_core_per_node = get_num_tasks_per_socket(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Scatterv_init_general(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype,
                                          root, comm, num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return -1;
  }
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

static int init_blocking_comm_allreduce(MPI_Comm comm, int i_comm) {
  int comm_size_row, comm_rank_row, i, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method_, *copyin_factors = NULL, num_sockets_per_node, j, k, factors_max_max = -1, *factors_max, **factors, *primes;
  double d1;
  struct cost_list *p1, *p2;
//  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072, 1048576, 2097152};
  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072, 1048576};
  MPI_Datatype datatype = MPI_LONG;
  MPI_Op op = MPI_SUM;
  int my_cores_per_node = get_num_tasks_per_socket(comm);
  char *sendbuf = malloc(1024 * 1024 * 1024);
  char *recvbuf = malloc(1024 * 1024 * 1024);
  struct {
    double value;
    int rank;
  } composition;
  char *str;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
  copyin_factors = (int*) malloc(sizeof(int) * (comm_size_row + 1));
  if (!copyin_factors)
    goto error;
  MPI_Type_size(datatype, &type_size);
  num_ports = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((2 * comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (j = 0; j < sizeof(counts) / sizeof(counts[0]); j++) {
    message_size = type_size * counts[j];
    for (i = 0; i < comm_size_row; i++) {
      num_ports[i] = groups[i] = 0;
    }
    if (fixed_factors_ports == NULL && comm_size_row == my_cores_per_node * ext_mpi_num_sockets_per_node && ext_mpi_num_sockets_per_node == 2) {
      set_ports_single_node(1, 2, num_ports, groups);
    } else if (fixed_factors_ports == NULL && comm_size_row == my_cores_per_node * ext_mpi_num_sockets_per_node && ext_mpi_num_sockets_per_node == 4) {
      set_ports_single_node(1, 4, num_ports, groups);
    } else if (fixed_factors_ports == NULL && comm_size_row / my_cores_per_node > 1 && new_factors_heuristic) {
      if (comm_rank_row == 0) {
        factors_max_max = ext_mpi_heuristic_recursive_non_factors(comm_size_row/my_cores_per_node, 0, &factors_max, &factors, &primes);
        if (my_cores_per_node > 1) {
          for (i = 0; i < factors_max_max; i++) {
            primes[i] = 0;
          }
        }
        ext_mpi_min_cost_total(message_size, factors_max_max, factors_max, factors, primes, &i);
        for (k = 0; k < factors_max[i]; k++) {
          if (factors[i][k] > 0) {
            num_ports[k] = factors[i][k] - 1;
          } else {
            num_ports[k] = factors[i][k] + 1;
          }
          groups[k] = comm_size_row/my_cores_per_node;
        }
        groups[k - 1] = -groups[k - 1];
      }
      MPI_Bcast(&k, 1, MPI_INT, 0, comm);
      MPI_Bcast(num_ports, k, MPI_INT, 0, comm);
      MPI_Bcast(groups, k, MPI_INT, 0, comm);
      groups[k] = num_ports[k] = 0;
      if (comm_rank_row == 0) {
        free(primes);
        for (i = 0; i < factors_max_max; i++) {
          free(factors[i]);
        }
        free(factors);
        free(factors_max);
      }
    } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation) {
      if (comm_size_row / my_cores_per_node > 1) {
        if (my_cores_per_node == 1) {
          if (ext_mpi_cost_simulation(counts[j], type_size, comm_size_row * 12, 12,
                                      1, 1,
                                      comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
            goto error;
        } else {
          if (ext_mpi_cost_simulation(counts[j], type_size, comm_size_row, my_cores_per_node,
                                             1, 1,
                                             comm_size_row, comm_rank_row, 0, ext_mpi_bit_identical, ext_mpi_num_sockets_per_node) < 0)
            goto error;
        }
        p1 = ext_mpi_cost_list_start;
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
        ext_mpi_cost_list_start = NULL;
        ext_mpi_cost_list_length = 0;
        ext_mpi_cost_list_counter = 0;
        composition.value = d1;
        composition.rank = comm_rank_row;
        PMPI_Allreduce(MPI_IN_PLACE, &composition, 1, MPI_DOUBLE_INT, MPI_MINLOC, comm);
        d1 = composition.value;
        for (i = 0; num_ports[i]; i++)
          ;
        MPI_Bcast(&i, 1, MPI_INT, composition.rank, comm);
        MPI_Bcast(num_ports, i, MPI_INT, composition.rank, comm);
        MPI_Bcast(groups, i, MPI_INT, composition.rank, comm);
        groups[i] = num_ports[i] = 0;
      } else {
        groups[0] = num_ports[0] = -1;
        groups[1] = num_ports[1] = 0;
      }
      // FIXME comm_column
    } else if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
      //allreduce case for minimum computation
      if (comm_size_row / my_cores_per_node == 1){
        //if only one node
        group_size = 1;
      }else{
        //set group_size to 2^group_size <= num_nodes
        group_size = ceil(log(comm_size_row/my_cores_per_node)/log(2));
      }
      for(int i=0;i<2*group_size;i++){
        //iterate over the two groups
        if(i<group_size){
          //set num_ports to -1 for the first group
          num_ports[i]=-1;
          if(i==group_size-1){
            //set the final value of a group to a negative value to indicate the end of the group
            groups[i]=-comm_size_row/my_cores_per_node;
          }
          else{
            groups[i]=comm_size_row/my_cores_per_node;
          }
        }
        else{
          //set num_ports to 1 for the second group
          num_ports[i]=1;
          if(i==2*group_size-1){
            groups[i]=-comm_size_row/my_cores_per_node;
          }
          else{
            groups[i]=comm_size_row/my_cores_per_node;
          }
        }
      }
      //set the final value of the array to 0 to indicate the end of it
      groups[2*group_size]=0;
      num_ports[2*group_size]=0;
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors_ports[i];
        groups[i] = fixed_factors_groups[i];
      } while (fixed_factors_ports[i]);
    }
    if (copyin_method < 0) {
#ifdef GPU_ENABLED
      if ((counts[j] * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && !ext_mpi_gpu_is_device_pointer(recvbuf) && ext_mpi_blocking) {
#else
      if ((counts[j] * type_size <= CACHE_LINE_SIZE - OFFSET_FAST) && ext_mpi_blocking) {
#endif
        copyin_method = 3;
      } else if (my_cores_per_node > ext_mpi_node_size_threshold_max) {
        copyin_method = (counts[j] * type_size >
                         ext_mpi_node_size_threshold[ext_mpi_node_size_threshold_max - 1]);
      } else {
        copyin_method =
            (counts[j] * type_size > ext_mpi_node_size_threshold[my_cores_per_node - 1]);
      }
    }
    if (comm_size_row / my_cores_per_node > 1){
      group_size=1;
    }else{
      group_size=0;
    }
    for (i=0; num_ports[i]; i++){
      if (num_ports[i]>0){
        group_size*=abs(num_ports[i])+1;
      }
    }
    if (ext_mpi_verbose) {
      if (is_rank_zero(comm, MPI_COMM_NULL)) {
        if (group_size==comm_size_row/my_cores_per_node){
          printf("# recursive\n");
        }
        str = ext_mpi_print_ports_groups(num_ports, groups);
        printf("# EXT_MPI prep allreduce parameters %d %d %d %d ports %s\n",
               comm_size_row / my_cores_per_node, message_size, 1,
               my_cores_per_node, str);
        free(str);
      }
    }
    num_sockets_per_node = ext_mpi_num_sockets_per_node;
    if (num_sockets_per_node == 1) num_sockets_per_node = -1;
    EXT_MPI_Allreduce_measurement(
        sendbuf, recvbuf, counts[j], datatype, op, comm, &my_cores_per_node,
        MPI_COMM_NULL, 1,
        my_cores_per_node, &copyin_method_, copyin_factors, &num_sockets_per_node);
    set_ports_single_node(ext_mpi_num_sockets_per_node, num_sockets_per_node, num_ports, groups);
    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, copyin_method_, copyin_factors, 0, 1, (group_size==comm_size_row/my_cores_per_node) && !not_recursive, ext_mpi_blocking, num_sockets_per_node, collective_type_allreduce, i_comm) < 0)
      goto error;
    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, copyin_method_, copyin_factors, 0, 0, 0, ext_mpi_blocking, num_sockets_per_node, collective_type_allreduce, i_comm) < 0)
      goto error;
  }
  free(groups);
  free(num_ports);
  free(copyin_factors);
  free(recvbuf);
  free(sendbuf);
  return 0;
error:
  free(groups);
  free(num_ports);
  free(copyin_factors);
  free(recvbuf);
  free(sendbuf);
  return ERROR_MALLOC;
}

static int init_blocking_comm_reduce_scatter_block(MPI_Comm comm, int i_comm) {
  int comm_size_row, *num_ports = NULL, *groups = NULL, type_size,
      i, group_size, copyin_method_ = -1, *copyin_factors = NULL, num_sockets_per_node, j;
  char *str;
//  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072, 1048576, 2097152};
  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072, 1048576};
  MPI_Datatype datatype = MPI_LONG;
  MPI_Op op = MPI_SUM;
  int my_cores_per_node = get_num_tasks_per_socket(comm);
  char *sendbuf = malloc(1024 * 1024 * 1024);
  char *recvbuf = malloc(1024 * 1024 * 1024);
  MPI_Comm_size(comm, &comm_size_row);
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
  for (j = 0; j < sizeof(counts)/sizeof(int); j++) {
    if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
      if (comm_size_row/my_cores_per_node==1){
        group_size = 1;
      }else{
        group_size = ceil(log(comm_size_row/my_cores_per_node)/log(2));
      }
      for(int i=0;i<group_size;i++){
        num_ports[i]=-1;
        if(i==group_size-1){
          groups[i]=-comm_size_row/my_cores_per_node;
        }
        else{
          groups[i]=comm_size_row/my_cores_per_node;
        }
      }
      groups[group_size]=0;
      num_ports[group_size]=0;
    } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation){
      if (my_cores_per_node > 1) {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node,
                           counts[j] * type_size, my_cores_per_node,
                           num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node,
                           counts[j] * type_size, 12, num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      }
      revert_num_ports(num_ports, groups);
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors_ports[i];
        groups[i] = fixed_factors_groups[i];
      } while (fixed_factors_ports[i]);
    }
    if (comm_size_row/my_cores_per_node>1){
      group_size=1;
    }else{
      group_size=0;
    }
    for (i=0; num_ports[i]; i++){
      group_size*=abs(num_ports[i])+1;
    }
    if (ext_mpi_verbose) {
      if (is_rank_zero(comm, MPI_COMM_NULL)){
        if (group_size==comm_size_row/my_cores_per_node){
          printf("# recursive\n");
        }
        str = ext_mpi_print_ports_groups(num_ports, groups);
        printf("# EXT_MPI prep reduce_scatter parameters %d %d %d %d ports %s\n",
               comm_size_row / my_cores_per_node, counts[j] * type_size, 1,
               my_cores_per_node, str);
        free(str);
      }
    }
    num_sockets_per_node = ext_mpi_num_sockets_per_node;
    for (i = 0; i < comm_size_row + 1; i++) {
      copyin_factors[i] = 0;
    }
    if (num_sockets_per_node == 1) num_sockets_per_node = -1;
    EXT_MPI_Allreduce_measurement(
        sendbuf, recvbuf, counts[j], datatype, op, comm, &my_cores_per_node,
        MPI_COMM_NULL, 1,
        my_cores_per_node, &copyin_method_, copyin_factors, &num_sockets_per_node);
    set_ports_single_node(ext_mpi_num_sockets_per_node, num_sockets_per_node, num_ports, groups);
    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, copyin_method_, copyin_factors, 0, 1, (group_size==comm_size_row/my_cores_per_node) && !not_recursive, ext_mpi_blocking, num_sockets_per_node, collective_type_reduce_scatter_block, i_comm) < 0)
      goto error;
  }
  free(groups);
  free(num_ports);
  free(copyin_factors);
  free(recvbuf);
  free(sendbuf);
  return 0;
error:
  free(groups);
  free(num_ports);
  free(copyin_factors);
  free(recvbuf);
  free(sendbuf);
  return ERROR_MALLOC;
}

static int init_blocking_comm_allgather(MPI_Comm comm, int i_comm) {
  int comm_rank_row, comm_size_row, *num_ports = NULL, *groups = NULL, type_size,
      i, group_size, num_sockets_per_node, j, k, factors_max_max = -1, *factors_max, **factors, *primes;
  char *str;
//  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072, 1048576, 2097152};
  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072, 1048576};
  MPI_Datatype datatype = MPI_LONG;
  int my_cores_per_node = get_num_tasks_per_socket(comm);
  MPI_Comm_rank(comm, &comm_rank_row);
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Type_size(datatype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (j = 0; j < sizeof(counts)/sizeof(int); j++) {
    if (fixed_factors_ports == NULL && comm_size_row / my_cores_per_node > 1 && new_factors_heuristic) {
      if (comm_rank_row == 0) {
        factors_max_max = ext_mpi_heuristic_recursive_non_factors(comm_size_row/my_cores_per_node, 1, &factors_max, &factors, &primes);
        if (my_cores_per_node > 1) {
          for (i = 0; i < factors_max_max; i++) {
            primes[i] = 0;
          }
        }
        ext_mpi_min_cost_total(counts[j] * type_size, factors_max_max, factors_max, factors, primes, &i);
        for (k = 0; k < factors_max[i]; k++) {
          if (factors[i][k] > 0) {
            num_ports[k] = factors[i][k] - 1;
          } else {
            num_ports[k] = factors[i][k] + 1;
          }
          groups[k] = comm_size_row/my_cores_per_node;
        }
        groups[k - 1] = -groups[k - 1];
      }
      MPI_Bcast(&k, 1, MPI_INT, 0, comm);
      MPI_Bcast(num_ports, k, MPI_INT, 0, comm);
      MPI_Bcast(groups, k, MPI_INT, 0, comm);
      groups[k] = num_ports[k] = 0;
      if (comm_rank_row == 0) {
        free(primes);
        for (i = 0; i < factors_max_max; i++) {
          free(factors[i]);
        }
        free(factors);
        free(factors_max);
      }
    } else if (fixed_factors_ports == NULL && ext_mpi_minimum_computation){
      if (comm_size_row/my_cores_per_node==1){
        group_size = 1;
      }else{
        group_size = ceil(log(comm_size_row/my_cores_per_node)/log(2));
      }
      for(int i=0;i<group_size;i++){
        num_ports[i]=-1;
        if(i==group_size-1){
          groups[i]=-comm_size_row/my_cores_per_node;
        }
        else{
          groups[i]=comm_size_row/my_cores_per_node;
        }
      }
      groups[group_size]=0;
      num_ports[group_size]=0;
    } else if (fixed_factors_ports == NULL && !ext_mpi_minimum_computation){
      if (my_cores_per_node > 1) {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node,
                           counts[j] * type_size, my_cores_per_node,
                           num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      } else {
        if (ext_mpi_cost_simple_recursive(comm_size_row / my_cores_per_node,
                           counts[j] * type_size, 12, num_ports, groups, ext_mpi_num_sockets_per_node) < 0)
          goto error;
      }
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors_ports[i];
        groups[i] = fixed_factors_groups[i];
      } while (fixed_factors_ports[i]);
    }
    if (comm_size_row/my_cores_per_node>1){
      group_size=1;
    }else{
      group_size=0;
    }
    for (i=0; num_ports[i]; i++){
      group_size*=abs(num_ports[i])+1;
    }
    if (ext_mpi_verbose) {
      if (is_rank_zero(comm, MPI_COMM_NULL)){
        if (group_size==comm_size_row/my_cores_per_node){
          printf("# recursive\n");
        }
        str = ext_mpi_print_ports_groups(num_ports, groups);
        printf("# EXT_MPI prep allgather parameters %d %d %d %d ports %s\n",
               comm_size_row / my_cores_per_node, counts[j] * type_size, 1,
               my_cores_per_node, str);
        free(str);
      }
    }
    num_sockets_per_node = ext_mpi_num_sockets_per_node;
    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, -1, NULL, 0, 1, (group_size==comm_size_row/my_cores_per_node) && !not_recursive, ext_mpi_blocking, num_sockets_per_node, collective_type_allgather, i_comm) < 0)
      goto error;
  }
  free(groups);
  free(num_ports);
  return 0;
error:
  free(groups);
  free(num_ports);
  return ERROR_MALLOC;
}

int EXT_MPI_Init_blocking_comm(MPI_Comm comm, int i_comm) {
  init_blocking_comm_allreduce(comm, i_comm);
  init_blocking_comm_reduce_scatter_block(comm, i_comm);
  init_blocking_comm_allgather(comm, i_comm);
  return 0;
}

int EXT_MPI_Finalize_blocking_comm(int i_comm) {
  EXT_MPI_Release_blocking_native(i_comm);
  return 0;
}

int EXT_MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm){
  return EXT_MPI_Allreduce_native(sendbuf, recvbuf, count, reduction_op, i_comm);
}

int EXT_MPI_Reduce_scatter_block(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm){
  return EXT_MPI_Reduce_scatter_block_native(sendbuf, recvbuf, recvcount, reduction_op, i_comm);
}

int EXT_MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, int i_comm){
  return EXT_MPI_Allgather_native(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, i_comm);
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
