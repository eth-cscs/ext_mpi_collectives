#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "recursive_factors.h"
#include "ports_groups.h"
#include "num_ports_factors.h"

int ext_mpi_get_param(int ext_mpi_param, MPI_Comm comm, MPI_Info info, char *info_str, int de) {
  char value[101];
  int flag, var;
  if (ext_mpi_param >= 0) {
    return ext_mpi_param;
  }
  if (info != MPI_INFO_NULL) {
    MPI_Info_get(info, info_str, 100, value, &flag);
    if (flag) {
      sscanf(value, "%d", &var);
      return var;
    }
  }
  return de;
}

int ext_mpi_copyin_info(MPI_Comm comm, MPI_Info info, int *copyin_method, int **copyin_factors) {
  char value[1001];
  int flag;
  *copyin_method = -1;
  if (info != MPI_INFO_NULL) {
    MPI_Info_get(info, "ext_mpi_copyin", 1000, value, &flag);
    if (flag) {
      ext_mpi_scan_copyin(value, copyin_method, copyin_factors);
    }
  }
  return *copyin_method;
}

void ext_mpi_set_ports_single_node(int num_sockets_per_node, int *num_ports, int *groups) {
  if (groups[0] == -1 && groups[1] == 0) {
    if (num_sockets_per_node == 1) {
      num_ports[0] = -1;
      num_ports[1] = 0;
      groups[0] = -1;
      groups[1] = 0;
    } else if (num_sockets_per_node == 2) {
      num_ports[0] = -1;
      num_ports[1] = 1;
      num_ports[2] = 0;
      groups[0] = -2;
      groups[1] = -2;
      groups[2] = 0;
    } else if (num_sockets_per_node == 4) {
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
}

static int get_group_size(int *num_ports, int *groups){
  int group_size, i;
  if (num_ports[1]) {
    group_size = 1;
  } else {
    group_size = 0;
  }
  for (i=0; num_ports[i]; i++) {
    if (num_ports[i] > 0) {
      group_size *= abs(num_ports[i]) + 1;
    }
  }
  return group_size;
}

int ext_mpi_num_ports_factors_env(MPI_Comm comm_row, int *fixed_factors_ports, int *fixed_factors_groups, int *num_ports, int *groups) {
  int i;
  if (fixed_factors_ports) {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors_ports[i];
      groups[i] = fixed_factors_groups[i];
    } while (fixed_factors_ports[i]);
    return get_group_size(num_ports, groups);
  } else {
    return -1;
  }
}

int ext_mpi_num_ports_factors_info(MPI_Comm comm_row, MPI_Info info, int **num_ports, int **groups) {
  char value[1001];
  int flag;
  if (info != MPI_INFO_NULL) {
    MPI_Info_get(info, "ext_mpi_num_ports", 1000, value, &flag);
    if (flag) {
      ext_mpi_scan_ports_groups(value, num_ports, groups);
      return 1;
    }
  }
  return -1;
}

int ext_mpi_num_ports_factors(int message_size, int collective_type, MPI_Comm comm_row, int my_cores_per_node_row, int num_sockets_per_node, int minimum_computation, int **num_ports, int **groups) {
  int comm_rank_row, comm_size_row, factors_max_max = -1, group_size, **factors, *factors_max, *primes, i, j;
  PMPI_Comm_rank(comm_row, &comm_rank_row);
  PMPI_Comm_size(comm_row, &comm_size_row);
  (*num_ports) = (int*)malloc((comm_size_row + 1)*sizeof(int));
  (*groups) = (int*)malloc((comm_size_row + 1)*sizeof(int));
  if (comm_size_row == my_cores_per_node_row * num_sockets_per_node) {
    (*num_ports)[0] = (*groups[0]) = -1;
    (*num_ports)[1] = (*groups[1]) = 0;
    ext_mpi_set_ports_single_node(num_sockets_per_node, *num_ports, *groups);
  } else if (!minimum_computation) {
    if (comm_rank_row == 0) {
      factors_max_max = ext_mpi_heuristic_recursive_non_factors(comm_size_row/my_cores_per_node_row, collective_type, &factors_max, &factors, &primes);
      factors_max_max = ext_mpi_heuristic_cancel_factors(factors_max_max, factors_max, factors, primes);
      if (my_cores_per_node_row > 1) {
        for (i = 0; i < factors_max_max; i++) {
	  primes[i] = 1;
	}
      }
      ext_mpi_min_cost_total(message_size, factors_max_max, factors_max, factors, primes, &i);
      for (j = 0; j < factors_max[i]; j++) {
        if (factors[i][j] > 0) {
          (*num_ports)[j] = factors[i][j] - 1;
        } else {
          (*num_ports)[j] = factors[i][j] + 1;
        }
        (*groups)[j] = comm_size_row/my_cores_per_node_row;
      }
      (*groups)[j - 1] = -(*groups)[j - 1];
    }
    PMPI_Bcast(&j, 1, MPI_INT, 0, comm_row);
    PMPI_Bcast(*num_ports, j, MPI_INT, 0, comm_row);
    PMPI_Bcast(*groups, j, MPI_INT, 0, comm_row);
    (*groups)[j] = (*num_ports)[j] = 0;
    if (comm_rank_row == 0) {
      free(primes);
      for (i = 0; i < factors_max_max; i++) {
        free(factors[i]);
      }
      free(factors);
      free(factors_max);
    }
    // FIXME comm_column
  } else if (minimum_computation){
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
        (*num_ports)[i]=-1;
        if(i==group_size-1){
          //set the final value of a group to a negative value to indicate the end of the group
          (*groups)[i]=-comm_size_row/my_cores_per_node_row;
        }
        else{
          (*groups)[i]=comm_size_row/my_cores_per_node_row;
        }
      }
      else{
        //set num_ports to 1 for the second group
        (*num_ports)[i]=1;
        if(i==2*group_size-1){
          (*groups)[i]=-comm_size_row/my_cores_per_node_row;
        }
        else{
          (*groups)[i]=comm_size_row/my_cores_per_node_row;
        }
      }
    }
    //set the final value of the array to 0 to indicate the end of it
    (*num_ports)[2*group_size] = (*groups)[2*group_size] = 0;
  }
  return get_group_size(*num_ports, *groups);
}
