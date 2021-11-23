#ifndef EXT_MPI_RANK_PERMUTATION_H_

#define EXT_MPI_RANK_PERMUTATION_H_

#ifdef __cplusplus
extern "C"
{
#endif

void ext_mpi_rank_perm_heuristic(int num_nodes, int *node_recvcounts, int *rank_perm);
int ext_mpi_generate_rank_permutation_forward(char *buffer_in, char *buffer_out);
int ext_mpi_generate_rank_permutation_backward(char *buffer_in, char *buffer_out);

#ifdef __cplusplus
}
#endif

#endif
