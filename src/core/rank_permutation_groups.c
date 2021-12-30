#include "rank_permutation_groups.h"
#include "rank_permutation.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ext_mpi_rank_perm_heuristic_groups(int num_nodes, int groups_size, int *node_recvcounts, int *rank_perm){
  int lnode_recvcounts[num_nodes], lrank_perm[num_nodes], j, i;
  for (i=0; i<groups_size; i++){
    for (j=0; j<num_nodes/groups_size; j++){
      lnode_recvcounts[j] = node_recvcounts[i+j*groups_size];
    }
    ext_mpi_rank_perm_heuristic(num_nodes/groups_size, lnode_recvcounts, lrank_perm);
    for (j=0; j<num_nodes/groups_size; j++){
      node_recvcounts[i+j*groups_size] = lnode_recvcounts[j];
      rank_perm[i+j*groups_size] = lrank_perm[j]+i*(num_nodes/groups_size);
      //rank_perm[i*(num_nodes/groups_size)+j] = lrank_perm[j]+i*(num_nodes/groups_size);
    }
  }
}

int ext_mpi_generate_rank_permutation_forward_groups(char *buffer_in, char *buffer_out){
}

int ext_mpi_generate_rank_permutation_backward_groups(char *buffer_in, char *buffer_out){
}
