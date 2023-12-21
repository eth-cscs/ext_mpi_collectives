#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ext_mpi.h"
#include "ext_mpi_blocking.h"
#include "constants.h"
#include "ext_mpi_native.h"
#include "ext_mpi_native_blocking.h"
#include "read_write.h"
#include "read_bench.h"
#include "cost_simple_recursive.h"
#include "cost_estimation.h"
#include "cost_simulation.h"
#include "count_instructions.h"
#include "ports_groups.h"
#include "cost_copyin_measurement.h"
#include "recursive_factors.h"
#include "num_ports_factors.h"
#ifdef GPU_ENABLED
#include "gpu_core.h"
#endif

extern int ext_mpi_blocking;
extern int ext_mpi_verbose;
extern int ext_mpi_num_sockets_per_node;
extern int ext_mpi_bit_reproducible;
extern int ext_mpi_bit_identical;
extern int ext_mpi_minimum_computation;
extern int *ext_mpi_fixed_factors_ports;
extern int *ext_mpi_fixed_factors_groups;
extern int ext_mpi_not_recursive;

static int init_blocking_comm_allreduce(MPI_Comm comm, int i_comm) {
  int comm_size_row, comm_rank_row, i, message_size, type_size, *num_ports = NULL, *groups = NULL, group_size, copyin_method_, *copyin_factors = NULL, num_sockets_per_node, j, k, factors_max_max = -1, *factors_max, **factors, *primes;
// int mpi_size;
//  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072, 1048576, 2097152};
  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072};
/*  MPI_Comm_size(comm, &mpi_size);
  for (j = 0; j < sizeof(counts) / sizeof(counts[0]); j++) {
    if (counts[j] > 1) {
      counts[j] = (counts[j] / mpi_size) * mpi_size;
      if (!counts[j]) {
	counts[j] = mpi_size;
      }
    }
  }*/
  MPI_Datatype datatype = MPI_LONG;
  MPI_Op op = MPI_SUM;
  int my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, abs(ext_mpi_num_sockets_per_node));
  char *sendbuf = (char *)malloc(1024 * 1024 * 1024);
  char *recvbuf = (char *)malloc(1024 * 1024 * 1024);
  char *str;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
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
    if (ext_mpi_fixed_factors_ports == NULL && comm_size_row == my_cores_per_node * ext_mpi_num_sockets_per_node) {
      ext_mpi_set_ports_single_node(ext_mpi_num_sockets_per_node, num_ports, groups);
    } else if (ext_mpi_fixed_factors_ports == NULL && !ext_mpi_minimum_computation) {
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
    } else if (ext_mpi_fixed_factors_ports == NULL && ext_mpi_minimum_computation){
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
        num_ports[i] = ext_mpi_fixed_factors_ports[i];
        groups[i] = ext_mpi_fixed_factors_groups[i];
      } while (ext_mpi_fixed_factors_ports[i]);
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
      if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)) {
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
        my_cores_per_node, &copyin_method_, &copyin_factors, &num_sockets_per_node);
    if (ext_mpi_num_sockets_per_node == 1) {
      ext_mpi_set_ports_single_node(num_sockets_per_node, num_ports, groups);
    }
    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, copyin_method_, copyin_factors, 0, 1, (group_size==comm_size_row/my_cores_per_node) && !ext_mpi_not_recursive, ext_mpi_blocking, num_sockets_per_node, collective_type_allreduce, i_comm) < 0)
      goto error;
/*    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, copyin_method_, copyin_factors, 0, 0, 0, ext_mpi_blocking, num_sockets_per_node, collective_type_allreduce, i_comm) < 0)
      goto error;*/
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
  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072};
  MPI_Datatype datatype = MPI_LONG;
  MPI_Op op = MPI_SUM;
  int my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, abs(ext_mpi_num_sockets_per_node));
  char *sendbuf = (char *)malloc(1024 * 1024 * 1024);
  char *recvbuf = (char *)malloc(1024 * 1024 * 1024);
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Type_size(datatype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!num_ports)
    goto error;
  groups = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (!groups)
    goto error;
  for (j = 0; j < sizeof(counts)/sizeof(int); j++) {
    if (ext_mpi_fixed_factors_ports == NULL && ext_mpi_minimum_computation){
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
    } else if (ext_mpi_fixed_factors_ports == NULL && !ext_mpi_minimum_computation){
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
      ext_mpi_revert_num_ports(num_ports, groups);
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = ext_mpi_fixed_factors_ports[i];
        groups[i] = ext_mpi_fixed_factors_groups[i];
      } while (ext_mpi_fixed_factors_ports[i]);
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
      if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
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
    if (num_sockets_per_node == 1) num_sockets_per_node = -1;
    EXT_MPI_Allreduce_measurement(
        sendbuf, recvbuf, counts[j], datatype, op, comm, &my_cores_per_node,
        MPI_COMM_NULL, 1,
        my_cores_per_node, &copyin_method_, &copyin_factors, &num_sockets_per_node);
    if (ext_mpi_num_sockets_per_node == 1) {
      ext_mpi_set_ports_single_node(num_sockets_per_node, num_ports, groups);
    }
    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, copyin_method_, copyin_factors, 0, 1, (group_size==comm_size_row/my_cores_per_node) && !ext_mpi_not_recursive, ext_mpi_blocking, num_sockets_per_node, collective_type_reduce_scatter_block, i_comm) < 0)
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
  int counts[] = {1, 4, 16, 64, 256, 2048, 16384, 131072};
  MPI_Datatype datatype = MPI_LONG;
  int my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, abs(ext_mpi_num_sockets_per_node));
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
    if (ext_mpi_fixed_factors_ports == NULL && comm_size_row / my_cores_per_node > 1 && !ext_mpi_minimum_computation) {
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
    } else if (ext_mpi_fixed_factors_ports == NULL && ext_mpi_minimum_computation){
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
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = ext_mpi_fixed_factors_ports[i];
        groups[i] = ext_mpi_fixed_factors_groups[i];
      } while (ext_mpi_fixed_factors_ports[i]);
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
      if (ext_mpi_is_rank_zero(comm, MPI_COMM_NULL)){
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
    if (EXT_MPI_Add_blocking_native(counts[j], MPI_LONG, MPI_SUM, comm, my_cores_per_node, num_ports, groups, -1, NULL, 0, 1, (group_size==comm_size_row/my_cores_per_node) && !ext_mpi_not_recursive, ext_mpi_blocking, num_sockets_per_node, collective_type_allgather, i_comm) < 0)
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
