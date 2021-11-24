#ifndef EXT_MPI_PORTS_GROUPS_H_

#define EXT_MPI_PORTS_GROUPS_H_

#ifdef __cplusplus
extern "C"
{
#endif

char* ext_mpi_print_ports_groups(int *ports, int *groups);
int ext_mpi_scan_ports_groups(char *str, int **ports, int **groups);

#ifdef __cplusplus
}
#endif

#endif
