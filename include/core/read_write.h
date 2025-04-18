#ifndef EXT_MPI_READ_WRITE_H_

#define EXT_MPI_READ_WRITE_H_

#define MAX_BUFFER_SIZE 100000000
#define CACHE_LINE_SIZE 64
//#define CACHE_LINE_SIZE 1024
#define OFFSET_FAST sizeof(long int)

#ifdef __cplusplus
extern "C" {
#endif

enum ecollective_type {
  collective_type_allgatherv,
  collective_type_reduce_scatter,
  collective_type_allreduce,
  collective_type_allreduce_group,
  collective_type_allreduce_short,
  collective_type_allgather,
  collective_type_reduce_scatter_block
};
enum edata_type {
  data_type_char,
  data_type_int,
  data_type_long_int,
  data_type_float,
  data_type_double
};
enum eassembler_type {
  enode_barrier,
  esocket_barrier,
  esocket_barrier_small,
  eset_node_barrier,
  ewait_node_barrier,
  ememcpy,
  ememcp_,
  esmemcpy,
  esmemcp_,
  ereduce,
  ereduc_,
  einvreduce,
  esreduce,
  esreduc_,
  esinvreduce,
  eirecv,
  eirec_,
  eisend,
  eisen_,
  ewaitall,
  ewaitany,
  eattached,
  esendbufp,
  erecvbufp,
  elocmemp,
  eshmem_tempp,
  eshmemo,
  ecpbuffer_offseto,
  ecp,
  ereturn,
  enop,
  estage,
  estart,
  egemv,
  ememory_fence,
  ememory_fence_store,
  ememory_fence_load
};

struct parameters_block {
  enum ecollective_type collective_type;
  int node;
  int num_nodes;
  int socket_number;
  int socket_rank;
  int socket_row_size;
  int socket_column_size;
  int socket_size_barrier;
  int socket_size_barrier_small;
  int num_sockets_per_node;
  int *counts;
  int counts_max;
  int *num_ports;
  int num_ports_max;
  int *groups;
  int groups_max;
  int *message_sizes;
  int message_sizes_max;
  int *rank_perm;
  int rank_perm_max;
  int *iocounts;
  int iocounts_max;
  int copyin_method;
  int *copyin_factors;
  int copyin_factors_max;
  int *mem_partners_send;
  int mem_partners_send_max;
  int *mem_partners_recv;
  int mem_partners_recv_max;
  enum edata_type data_type;
  int verbose;
  int in_place;
  int ascii_in, ascii_out;
  int bit_identical;
  int not_recursive;
  int locmem_max;
  int shmem_max;
  int *shmem_buffer_offset;
  int shmem_buffer_offset_max;
  int root;
  int on_gpu;
};

struct data_algorithm_line {
  int frac;
  int sendto_max;
  int *sendto_node;
  int *sendto_line;
  int recvfrom_max;
  int *recvfrom_node;
  int *recvfrom_line;
  int reducefrom_max;
  int *reducefrom;
  int copyreducefrom_max;
  int *copyreducefrom;
};

struct data_algorithm_block {
  int num_lines;
  struct data_algorithm_line *lines;
};

struct data_algorithm {
  int num_blocks;
  struct data_algorithm_block *blocks;
};

struct line_irecv_isend {
  enum eassembler_type type;
  enum eassembler_type buffer_type;
  int buffer_number;
  int is_offset;
  int offset_number;
  int offset;
  int size, partner, tag;
};

struct line_memcpy_reduce {
  enum eassembler_type type;
  enum eassembler_type buffer_type1;
  int buffer_number1;
  int is_offset1;
  int offset_number1;
  int offset1;
  enum eassembler_type buffer_type2;
  int buffer_number2;
  int is_offset2;
  int offset_number2;
  int offset2;
  int size;
};

int ext_mpi_read_parameters(char *buffer_in, struct parameters_block **parameters);
int ext_mpi_write_parameters(struct parameters_block *parameters, char *buffer_out);
int ext_mpi_delete_parameters(struct parameters_block *parameters);
int ext_mpi_read_algorithm(char *buffer_in, struct data_algorithm *data, int ascii_in);
int ext_mpi_write_algorithm(struct data_algorithm data, char *buffer_out, int ascii_out);
void ext_mpi_delete_algorithm(struct data_algorithm data);
void ext_mpi_delete_stage_line(struct data_algorithm_line data);
int ext_mpi_read_line(char *buffer_in, char *line, int ascii);
int ext_mpi_write_line(char *buffer_out, char *line, int ascii);
int ext_mpi_write_eof(char *buffer_out, int ascii);

int ext_mpi_switch_to_ascii(char *buffer);

int ext_mpi_write_assembler_line(char *buffer_out, int ascii, char *types, ...);
int ext_mpi_read_assembler_line(char *buffer_in, int ascii, char *types, ...);
int ext_mpi_read_irecv_isend(char *line, struct line_irecv_isend *data);
int ext_mpi_read_memcpy_reduce(char *line, struct line_memcpy_reduce *data);
int ext_mpi_write_irecv_isend(char *buffer_out, struct line_irecv_isend *data, int ascii);
int ext_mpi_write_memcpy_reduce(char *buffer_out, struct line_memcpy_reduce *data, int ascii);

#ifdef __cplusplus
}
#endif

#endif
