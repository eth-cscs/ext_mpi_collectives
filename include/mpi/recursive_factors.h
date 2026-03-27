#ifndef EXT_MPI_RECURSIVE_FACTORS_H_

#define EXT_MPI_RECURSIVE_FACTORS_H_

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_heuristic_recursive_factors(int num_nodes, int **factors_max, int ***factors);
double ext_mpi_min_cost_total(int msize, int num, int *factors_max, int **factors, int *primes, int *ind_min);
int ext_mpi_heuristic_recursive_non_factors(int num_nodes, int allgather, int **factors_max, int ***factors, int **primes);
int ext_mpi_heuristic_cancel_factors(int factors_max_max, int *factors_max, int **factors, int *primes);

#ifdef __cplusplus
}
#endif

#endif
