#ifndef EXT_MPI_NUM_PORTS_FACTORS_H_

#define EXT_MPI_NUM_PORTS_FACTORS_H_

#ifdef __cplusplus
extern "C" {
#endif

void ext_mpi_set_ports_single_node(int num_sockets_per_node, int *num_ports, int *groups);
int ext_mpi_num_ports_factors(int message_size, int collective_type, MPI_Comm comm_row, int my_cores_per_node_row, int ext_mpi_num_sockets_per_node, int ext_mpi_minimum_computation, int *ext_mpi_fixed_factors_ports, int *ext_mpi_fixed_factors_groups, int *num_ports, int *groups);

#ifdef __cplusplus
}
#endif

#endif
