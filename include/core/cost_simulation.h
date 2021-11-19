#ifndef EXT_MPI_COST_SIMULATION_H_

#define EXT_MPI_COST_SIMULATION_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define COST_LIST_LENGTH_MAX 100

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

extern struct cost_list *cost_list_start;
extern int cost_list_length;
extern int cost_list_counter;

int ext_mpi_cost_simulation(int count, int type_size, int comm_size_row,
                            int my_cores_per_node_row, int comm_size_column,
                            int my_cores_per_node_column, int comm_size_rowb,
                            int comm_rank_row, int simulate);

int ext_mpi_allreduce_simulate(int count, int type_size,
                               int comm_size_row, int my_cores_per_node_row,
                               int comm_size_column,
                               int my_cores_per_node_column);

#ifdef __cplusplus
}
#endif

#endif
