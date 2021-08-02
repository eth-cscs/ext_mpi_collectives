#ifndef EXT_MPI_RANK_PERMUTATION_H_

#define EXT_MPI_RANK_PERMUTATION_H_

#ifdef __cplusplus
extern "C"
{
#endif

void rank_perm_heuristic(int num_nodes, int *node_recvcounts, int *rank_perm);
int generate_rank_permutation_forward(char *buffer_in, char *buffer_out);
int generate_rank_permutation_backward(char *buffer_in, char *buffer_out);

#ifdef __cplusplus
}
#endif

#endif
