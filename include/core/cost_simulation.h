#ifndef EXT_MPI_COST_SIMULATION_H_

#define EXT_MPI_COST_SIMULATION_H_

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_cost_simulation(int count, int type_size, int comm_size_row,
                            int my_cores_per_node_row, int comm_size_column,
                            int my_cores_per_node_column, int comm_size_rowb,
                            int comm_rank_row, int simulate);

#ifdef __cplusplus
}
#endif

#endif
