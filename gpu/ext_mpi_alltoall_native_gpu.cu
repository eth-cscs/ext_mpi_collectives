#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <string.h>
#include <mpi.h>
#include "ext_mpi_alltoall_native_gpu.h"

#define NUM_BARRIERS 4

#define OPCODE_RETURN 0
#define OPCODE_MEMCPY 1
#define OPCODE_MPIIRECV 2
#define OPCODE_MPIISEND 3
#define OPCODE_MPIWAITALL 4
#define OPCODE_NODEBARRIER 5
#define OPCODE_SETNUMCORES 6
#define OPCODE_CUDAMEMCPY 7
#define OPCODE_CUDACOPYKERNEL 8
#define OPCODE_CUDADEVICESYNCHRONIZE 9

int shmemid = -1;
char volatile *shmem = NULL;
int shmem_size = 0;
cudaIpcMemHandle_st shmemid_gpu;
char volatile *shmem_gpu = NULL;

char *locmem = NULL;
int locmem_size = 0;

char **comm_code = NULL;
char **comm_code_gpu = NULL;

int handle_max = 10;

void
code_put_char (char **code, char c, int isdryrun)
{
  if (!isdryrun)
    *((char *) (*code)) = c;
  *code += sizeof (char);
}

void
code_put_int (char **code, int i, int isdryrun)
{
  if (!isdryrun)
    *((int *) (*code)) = i;
  *code += sizeof (int);
}

void
code_put_long (char **code, long l, int isdryrun)
{
  if (!isdryrun)
    *((long *) (*code)) = l;
  *code += sizeof (long);
}

void
code_put_pointer (char **code, void *p, int isdryrun)
{
  if (!isdryrun)
    *((void **) (*code)) = p;
  *code += sizeof (void *);
}

__host__ __device__ char
code_get_char (char **code)
{
  char c;
  c = *((char *) (*code));
  *code += sizeof (char);
  return c;
}

__host__ __device__ int
code_get_int (char **code)
{
  int i;
  i = *((int *) (*code));
  *code += sizeof (int);
  return i;
}

__host__ __device__ long
code_get_long (char **code)
{
  long l;
  l = *((long *) (*code));
  *code += sizeof (long);
  return l;
}

__host__ __device__ void *
code_get_pointer (char **code)
{
  void *p;
  p = *((void **) (*code));
  *code += sizeof (void *);
  return p;
}

int
setup_shared_memory (MPI_Comm comm, int my_cores_per_node_row,
		     MPI_Comm comm_column, int my_cores_per_node_column,
		     int size_shared, int *shmemid, char volatile **shmem,
		     char fill, int numfill)
{
  MPI_Comm my_comm_node_h, my_comm_node_v;
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column,
    my_mpi_size_column;
  MPI_Comm_size (comm, &my_mpi_size_row);
  MPI_Comm_rank (comm, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_size (comm_column, &my_mpi_size_column);
      MPI_Comm_rank (comm_column, &my_mpi_rank_column);
    }
  else
    {
      my_mpi_size_column = 1;
      my_mpi_rank_column = 0;
    }
  MPI_Comm_split (comm, my_mpi_rank_row / my_cores_per_node_row,
		  my_mpi_rank_row % my_cores_per_node_row, &my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_split (comm_column,
		      my_mpi_rank_column / my_cores_per_node_column,
		      my_mpi_rank_column % my_cores_per_node_column,
		      &my_comm_node_v);
    }
  if ((*shmem) != NULL)
    {
      MPI_Comm_free (&my_comm_node_h);
      if (comm_column != MPI_COMM_NULL)
	{
	  MPI_Comm_free (&my_comm_node_v);
	}
      return 1;
    }
  if ((my_mpi_rank_row % my_cores_per_node_row == 0)
      && (my_mpi_rank_column % my_cores_per_node_column == 0))
    {
      (*shmemid) = shmget (IPC_PRIVATE, size_shared, IPC_CREAT | 0666);
    }
  MPI_Bcast (shmemid, 1, MPI_INT, 0, my_comm_node_h);
  MPI_Barrier (my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Bcast (shmemid, 1, MPI_INT, 0, my_comm_node_v);
      MPI_Barrier (my_comm_node_v);
    }
  (*shmem) = (char *) shmat (*shmemid, NULL, 0);
  if ((*shmem) == NULL)
    exit (2);
  MPI_Barrier (my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Barrier (my_comm_node_v);
      MPI_Barrier (my_comm_node_h);
    }
  if (!((my_mpi_rank_row % my_cores_per_node_row == 0)
	&& (my_mpi_rank_column % my_cores_per_node_column == 0)))
    {
      (*shmemid) = -1;
    }
  else
    {
      memset ((void *) *shmem, fill, numfill);
    }
  MPI_Barrier (my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Barrier (my_comm_node_v);
      MPI_Barrier (my_comm_node_h);
    }
  MPI_Comm_free (&my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_free (&my_comm_node_v);
    }
  return 0;
}

int
setup_shared_memory_gpu (MPI_Comm comm, int my_cores_per_node_row,
			 MPI_Comm comm_column, int my_cores_per_node_column,
			 int size_shared, cudaIpcMemHandle_st * shmemid_gpu,
			 char volatile **shmem_gpu)
{
  MPI_Comm my_comm_node_h, my_comm_node_v;
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column,
    my_mpi_size_column;
  MPI_Comm_size (comm, &my_mpi_size_row);
  MPI_Comm_rank (comm, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_size (comm_column, &my_mpi_size_column);
      MPI_Comm_rank (comm_column, &my_mpi_rank_column);
    }
  else
    {
      my_mpi_size_column = 1;
      my_mpi_rank_column = 0;
    }
  MPI_Comm_split (comm, my_mpi_rank_row / my_cores_per_node_row,
		  my_mpi_rank_row % my_cores_per_node_row, &my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_split (comm_column,
		      my_mpi_rank_column / my_cores_per_node_column,
		      my_mpi_rank_column % my_cores_per_node_column,
		      &my_comm_node_v);
    }
  if ((*shmem_gpu) != NULL)
    {
      MPI_Comm_free (&my_comm_node_h);
      if (comm_column != MPI_COMM_NULL)
	{
	  MPI_Comm_free (&my_comm_node_v);
	}
      return 1;
    }
  if ((my_mpi_rank_row % my_cores_per_node_row == 0)
      && (my_mpi_rank_column % my_cores_per_node_column == 0))
    {
      if (cudaMalloc ((void **) shmem_gpu, size_shared) != 0)
	exit (16);
      if ((*shmem_gpu) == NULL)
	exit (16);
      if (cudaIpcGetMemHandle (shmemid_gpu, (void *) (*shmem_gpu)) != 0)
	exit (15);
    }
  MPI_Bcast (shmemid_gpu, sizeof (cudaIpcMemHandle_st), MPI_CHAR, 0,
	     my_comm_node_h);
  MPI_Barrier (my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Bcast (shmemid_gpu, sizeof (cudaIpcMemHandle_st), MPI_CHAR, 0,
		 my_comm_node_v);
      MPI_Barrier (my_comm_node_v);
    }
  if ((*shmem_gpu) == NULL)
    {
      if (cudaIpcOpenMemHandle
	  ((void **) shmem_gpu, *shmemid_gpu,
	   cudaIpcMemLazyEnablePeerAccess) != 0)
	exit (13);
    }
  if ((*shmem_gpu) == NULL)
    exit (2);
  MPI_Barrier (my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Barrier (my_comm_node_v);
      MPI_Barrier (my_comm_node_h);
    }
  MPI_Comm_free (&my_comm_node_h);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_free (&my_comm_node_v);
    }
  return 0;
}

int
rebase_address (char **ip, char *shmem_old, int shmem_size_old, char *shmem)
{
  if (*((char **) (*ip)) - shmem_old <= shmem_size_old)
    {
      code_put_pointer (ip, shmem + (*((char **) (*ip)) - shmem_old), 0);
      return (1);
    }
  else
    {
      code_get_pointer (ip);
      return (0);
    }
}

int
rebase_addresses (char *shmem_old, int shmem_size_old, char *shmem)
{
  char instruction, *ip;
  int handle;
  for (handle = 0; handle < handle_max; handle++)
    {
      ip = comm_code[handle];
      if (ip != NULL)
	{
	  do
	    {
	      instruction = code_get_char (&ip);
	      switch (instruction)
		{
		case OPCODE_RETURN:
		  break;
		case OPCODE_MEMCPY:
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  code_get_int (&ip);
		  break;
		case OPCODE_MPIIRECV:
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  code_get_int (&ip);
		  code_get_int (&ip);
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  break;
		case OPCODE_MPIISEND:
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  code_get_int (&ip);
		  code_get_int (&ip);
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  break;
		case OPCODE_MPIWAITALL:
		  code_get_int (&ip);
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  break;
		case OPCODE_NODEBARRIER:
		  break;
		case OPCODE_SETNUMCORES:
		  code_get_int (&ip);
		  break;
		case OPCODE_CUDAMEMCPY:
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  rebase_address (&ip, shmem_old, shmem_size_old, shmem);
		  code_get_int (&ip);
		  code_get_char (&ip);
		  break;
		case OPCODE_CUDACOPYKERNEL:
		  code_get_int (&ip);
		  code_get_int (&ip);
		  code_get_pointer (&ip);
		  break;
		case OPCODE_CUDADEVICESYNCHRONIZE:
		  break;
		default:
		  printf ("illegal MPI_OPCODE\n");
		  exit (1);
		}
	    }
	  while (instruction != OPCODE_RETURN);
	}
    }
  return (0);
}

void
setup_rank_translation (MPI_Comm comm, int my_cores_per_node_row,
			MPI_Comm comm_column, int my_cores_per_node_column,
			int *global_ranks)
{
  MPI_Comm my_comm_node;
  int my_mpi_size_row, grank, my_mpi_size_column, my_mpi_rank_column,
    *lglobal_ranks;
  MPI_Comm_size (comm, &my_mpi_size_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_size (comm_column, &my_mpi_size_column);
      MPI_Comm_rank (comm_column, &my_mpi_rank_column);
      MPI_Comm_split (comm_column,
		      my_mpi_rank_column / my_cores_per_node_column,
		      my_mpi_rank_column % my_cores_per_node_column,
		      &my_comm_node);
      MPI_Comm_rank (MPI_COMM_WORLD, &grank);
      lglobal_ranks =
	(int *) malloc (sizeof (int) * my_cores_per_node_column);
      MPI_Gather (&grank, 1, MPI_INT, lglobal_ranks, 1, MPI_INT, 0,
		  my_comm_node);
      MPI_Bcast (lglobal_ranks, my_cores_per_node_column, MPI_INT, 0,
		 my_comm_node);
      MPI_Barrier (my_comm_node);
      MPI_Comm_free (&my_comm_node);
      MPI_Gather (lglobal_ranks, my_cores_per_node_column, MPI_INT,
		  global_ranks, my_cores_per_node_column, MPI_INT, 0, comm);
      free (lglobal_ranks);
    }
  else
    {
      MPI_Comm_rank (MPI_COMM_WORLD, &grank);
      MPI_Gather (&grank, 1, MPI_INT, global_ranks, 1, MPI_INT, 0, comm);
    }
  MPI_Bcast (global_ranks, my_mpi_size_row * my_cores_per_node_column,
	     MPI_INT, 0, comm);
}

void
compute_offsets (int my_num_nodes, int num_ports, int gbstep, int port,
		 int *offset, int *size)
{
  int my_indices[my_num_nodes], i;
  for (i = 0; i < my_num_nodes; i++)
    {
      my_indices[i] = (i / gbstep) % (num_ports + 1);
    }
  (*offset) = (*size) = 0;
  for (i = 0; i < my_num_nodes; i++)
    {
      if (my_indices[i] < port + 1)
	(*offset)++;
      if (my_indices[i] == port + 1)
	(*size)++;
    }
}

int
get_handle ()
{
  char **handles_old, **handles_old_gpu;
  int handle, i;
  if (comm_code == NULL)
    {
      comm_code = (char **) malloc (sizeof (char *) * handle_max);
      comm_code_gpu = (char **) malloc (sizeof (char *) * handle_max);
      for (i = 0; i < handle_max; i++)
	{
	  comm_code[i] = NULL;
	  comm_code_gpu[i] = NULL;
	}
    }
  handle = 0;
  while ((comm_code[handle] != NULL) && handle < handle_max - 1)
    {
      handle++;
    }
  if (handle >= handle_max - 1)
    {
      if (comm_code[handle] != NULL)
	{
	  handles_old = comm_code;
	  handles_old_gpu = comm_code_gpu;
	  handle_max *= 2;
	  comm_code = (char **) malloc (sizeof (char *) * handle_max);
	  comm_code_gpu = (char **) malloc (sizeof (char *) * handle_max);
	  for (i = 0; i < handle_max; i++)
	    {
	      comm_code[i] = NULL;
	      comm_code_gpu[i] = NULL;
	    }
	  for (i = 0; i < handle_max / 2; i++)
	    {
	      comm_code[i] = handles_old[i];
	      comm_code_gpu[i] = handles_old_gpu[i];
	    }
	  free (handles_old);
	  free (handles_old_gpu);
	  handle++;
	}
    }
  return (handle);
}

int
local_alltoall_init (void *sendbuf, int sendcount, MPI_Datatype sendtype,
		     void *recvbuf, int recvcount, MPI_Datatype recvtype,
		     MPI_Comm comm_row, int my_cores_per_node_row,
		     MPI_Comm comm_column, int my_cores_per_node_column,
		     int num_ports, int num_active_ports, int chunks_throttle)
{
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
    my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node,
    my_mpi_size_global, my_mpi_rank_global;
  int dsize, gbstep, handle, isdryrun, num_comm, num_comm_max;
  char volatile *my_shared_sendbuf, *my_shared_recvbuf, *my_shared_middbuf,
    *ptemp;
  char *ip, *shmem_old, *locmem_old;
  int *global_ranks, i, j, port, shmem_size_old, locmem_size_old;
  void *sendbuf_host, *recvbuf_host;
  int num_comm_throttle, i_throttle;
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column)
    {
      num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
    }
  if (num_active_ports < 1)
    {
      num_active_ports = 1;
    }
  MPI_Type_size (sendtype, &type_size);
  dsize = type_size * sendcount;
  handle = get_handle ();
  MPI_Comm_size (comm_row, &my_mpi_size_row);
  MPI_Comm_rank (comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_size (comm_column, &my_mpi_size_column);
      MPI_Comm_rank (comm_column, &my_mpi_rank_column);
    }
  else
    {
      my_mpi_size_column = 1;
      my_mpi_rank_column = 0;
    }
  shmem_old = (char *) shmem;
  shmem_size_old = shmem_size;
  if (shmem_size <=
      dsize * my_cores_per_node_row * my_mpi_size_row *
      my_cores_per_node_column * 3 + NUM_BARRIERS)
    {
      shmem_size =
	dsize * my_cores_per_node_row * my_mpi_size_row *
	my_cores_per_node_column * 3 + NUM_BARRIERS;
      if (shmem != NULL)
	{
	  shmdt ((void *) shmem);
	  if (shmemid != -1)
	    {
	      shmctl (shmemid, IPC_RMID, NULL);
	    }
	  shmem = NULL;
	  shmemid = -1;
	}
    }
  if (!setup_shared_memory
      (comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
       shmem_size, &shmemid, &shmem, 0, NUM_BARRIERS))
    {
      if (shmem_old != NULL)
	{
	  rebase_addresses (shmem_old, shmem_size_old, (char *) shmem);
	}
    }
  global_ranks =
    (int *) malloc (sizeof (int) * my_mpi_size_row *
		    my_cores_per_node_column);
  setup_rank_translation (comm_row, my_cores_per_node_row, comm_column,
			  my_cores_per_node_column, global_ranks);
  my_shared_sendbuf = shmem + NUM_BARRIERS;
  my_shared_recvbuf =
    shmem + NUM_BARRIERS +
    dsize * my_cores_per_node_row * my_mpi_size_row *
    my_cores_per_node_column;
  my_shared_middbuf =
    shmem + NUM_BARRIERS +
    dsize * my_cores_per_node_row * my_mpi_size_row *
    my_cores_per_node_column * 2;
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_column * my_cores_per_node_row + my_lrank_row;
  my_mpi_size_global = my_mpi_size_row * my_cores_per_node_column;
  my_mpi_rank_global =
    my_mpi_rank_row * my_cores_per_node_column +
    my_mpi_rank_column % my_cores_per_node_column;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--)
    {
      if (isdryrun)
	{
	  ip = NULL;
	}
      else
	{
	  if (num_comm_max * (sizeof (MPI_Request) + sizeof (MPI_Status)) +
	      dsize * my_mpi_size_row * 2 > locmem_size)
	    {
	      locmem_old = locmem;
	      locmem_size_old = locmem_size;
	      locmem_size =
		num_comm_max * (sizeof (MPI_Request) + sizeof (MPI_Status)) +
		dsize * my_mpi_size_row * 2;
	      locmem = (char *) malloc (sizeof (char) * locmem_size);
	      if (locmem_old != NULL)
		{
		  rebase_addresses (locmem_old, locmem_size_old,
				    (char *) locmem);
		  free (locmem_old);
		}
	    }
	  sendbuf_host =
	    (void *) (locmem +
		      num_comm_max * (sizeof (MPI_Request) +
				      sizeof (MPI_Status)));
	  recvbuf_host =
	    (void *) (locmem +
		      num_comm_max * (sizeof (MPI_Request) +
				      sizeof (MPI_Status)) +
		      dsize * my_mpi_size_row);
	  ip = comm_code[handle] =
	    (char *) malloc (sizeof (char *) * ((size_t) (ip)));
	}
      num_comm_max = 0;
      code_put_char (&ip, OPCODE_SETNUMCORES, isdryrun);
      code_put_int (&ip, my_cores_per_node_row * my_cores_per_node_column,
		    isdryrun);
      code_put_char (&ip, OPCODE_CUDAMEMCPY, isdryrun);
      code_put_pointer (&ip, (void *) (sendbuf_host), isdryrun);
      code_put_pointer (&ip, (void *) (sendbuf), isdryrun);
      code_put_int (&ip, dsize * my_mpi_size_row, isdryrun);
      code_put_char (&ip, 2, isdryrun);
      if (my_mpi_size_row <= my_cores_per_node_row)
	{
	  for (i = 0; i < my_cores_per_node_row; i++)
	    {
	      if (i != my_mpi_rank_row)
		{
		  code_put_char (&ip, OPCODE_MEMCPY, isdryrun);
		  code_put_pointer (&ip, (void *) (my_shared_sendbuf +
						   ((i +
						     my_lrank_row *
						     my_cores_per_node_row) *
						    my_cores_per_node_column +
						    my_lrank_column) * dsize),
				    isdryrun);
		  code_put_pointer (&ip,
				    (void *) (((char *) sendbuf_host) +
					      i * dsize), isdryrun);
		  code_put_int (&ip, dsize, isdryrun);
		}
	    }
	  code_put_char (&ip, OPCODE_MEMCPY, isdryrun);
	  code_put_pointer (&ip,
			    (void *) (((char *) recvbuf_host) +
				      my_mpi_rank_row * dsize), isdryrun);
	  code_put_pointer (&ip,
			    (void *) (((char *) sendbuf_host) +
				      my_mpi_rank_row * dsize), isdryrun);
	  code_put_int (&ip, dsize, isdryrun);
	  code_put_char (&ip, OPCODE_NODEBARRIER, isdryrun);
	  for (i = 0; i < my_cores_per_node_row; i++)
	    {
	      if (i != my_lrank_row)
		{
		  code_put_char (&ip, OPCODE_MEMCPY, isdryrun);
		  code_put_pointer (&ip, (void *) (((char *) recvbuf_host) +
						   (my_node *
						    my_cores_per_node_row +
						    i) * dsize), isdryrun);
		  code_put_pointer (&ip,
				    (void *) (my_shared_sendbuf +
					      ((i * my_cores_per_node_row +
						my_lrank_row) *
					       my_cores_per_node_column +
					       my_lrank_column) * dsize),
				    isdryrun);
		  code_put_int (&ip, dsize, isdryrun);
		}
	    }
	}
      else
	{
	  int locations[my_mpi_size_row / my_cores_per_node_row],
	    locations2[my_mpi_size_row / my_cores_per_node_row],
	    counts[num_ports + 1], add, isize;
	  for (gbstep = 1; gbstep < my_mpi_size_row / my_cores_per_node_row;
	       gbstep *= (num_ports + 1))
	    {
	      for (i_throttle = 0; i_throttle < chunks_throttle; i_throttle++)
		{
		  num_comm = 0;
		  num_comm_throttle = 0;
		  if (my_lrank_node < num_active_ports)
		    {
		      for (port = my_lrank_node; port < num_ports;
			   port += num_active_ports)
			{
			  num_comm_throttle++;
			  compute_offsets (my_mpi_size_row /
					   my_cores_per_node_row, num_ports,
					   gbstep, port, &add, &isize);
			  if ((num_comm_throttle - 1) % chunks_throttle ==
			      i_throttle)
			    {
			      if (isize > 0)
				{
				  code_put_char (&ip, OPCODE_MPIIRECV,
						 isdryrun);
				  code_put_pointer (&ip,
						    (void
						     *) (((char *)
							  my_shared_recvbuf) +
							 add * dsize *
							 my_cores_per_node_row
							 *
							 my_cores_per_node_row
							 *
							 my_cores_per_node_column),
						    isdryrun);
				  code_put_int (&ip,
						isize * dsize *
						my_cores_per_node_row *
						my_cores_per_node_row *
						my_cores_per_node_column,
						isdryrun);
				  code_put_int (&ip,
						global_ranks[(my_mpi_rank_global + my_mpi_size_global - (port + 1) * gbstep * my_cores_per_node_row * my_cores_per_node_column) % my_mpi_size_global], isdryrun);
				  code_put_pointer (&ip,
						    (void *) (locmem +
							      num_comm *
							      sizeof
							      (MPI_Request)),
						    isdryrun);
				  num_comm++;
				}
			    }
			}
		    }
		  if (i_throttle == 0)
		    {
		      for (i = 0; i < num_ports + 1; i++)
			{
			  counts[i] = 0;
			}
		      if (gbstep == 1)
			{
			  for (j = 0;
			       j < my_mpi_size_row / my_cores_per_node_row;
			       j++)
			    {
			      compute_offsets (my_mpi_size_row /
					       my_cores_per_node_row,
					       num_ports, gbstep,
					       j % (num_ports + 1) - 1, &add,
					       &isize);
			      locations[j] =
				add + counts[j % (num_ports + 1)]++;
			    }
			  for (j = 0;
			       j < my_mpi_size_row / my_cores_per_node_row;
			       j++)
			    {
			      int jjj =
				locations[(j +
					   my_mpi_size_row /
					   my_cores_per_node_row -
					   my_node) % (my_mpi_size_row /
						       my_cores_per_node_row)];
			      for (i = 0; i < my_cores_per_node_row; i++)
				{
				  if (i + j * my_cores_per_node_row !=
				      my_mpi_rank_row)
				    {
				      code_put_char (&ip, OPCODE_MEMCPY,
						     isdryrun);
				      code_put_pointer (&ip,
							(void
							 *) (my_shared_sendbuf
							     +
							     ((i +
							       (jjj *
								my_cores_per_node_row
								+
								my_lrank_row)
							       *
							       my_cores_per_node_row)
							      *
							      my_cores_per_node_column
							      +
							      my_lrank_column)
							     * dsize),
							isdryrun);
				      code_put_pointer (&ip,
							(void
							 *) (((char *)
							      sendbuf_host) +
							     (i +
							      j *
							      my_cores_per_node_row)
							     * dsize),
							isdryrun);
				      code_put_int (&ip, dsize, isdryrun);
				    }
				}
			    }
			}
		      else
			{
			  for (j = 0;
			       j < my_mpi_size_row / my_cores_per_node_row;
			       j++)
			    {
			      compute_offsets (my_mpi_size_row /
					       my_cores_per_node_row,
					       num_ports, gbstep,
					       (j / gbstep) % (num_ports +
							       1) - 1, &add,
					       &isize);
			      int jjj =
				add +
				counts[(j / gbstep) % (num_ports + 1)]++;
			      locations2[j] = jjj;
			    }
			  for (j = my_lrank_node + 1;
			       j <
			       my_mpi_size_row / (my_cores_per_node_row *
						  my_cores_per_node_column);
			       j +=
			       my_cores_per_node_row *
			       my_cores_per_node_column)
			    {
			      code_put_char (&ip, OPCODE_MEMCPY, isdryrun);
			      code_put_pointer (&ip,
						(void *) (my_shared_sendbuf +
							  locations2[j] *
							  my_cores_per_node_row
							  *
							  my_cores_per_node_row
							  *
							  my_cores_per_node_column
							  * dsize), isdryrun);
			      code_put_pointer (&ip,
						(void *) (my_shared_middbuf +
							  locations[j] *
							  my_cores_per_node_row
							  *
							  my_cores_per_node_row
							  *
							  my_cores_per_node_column
							  * dsize), isdryrun);
			      code_put_int (&ip,
					    dsize * my_cores_per_node_row *
					    my_cores_per_node_row *
					    my_cores_per_node_column,
					    isdryrun);
			    }
			  for (j = 0;
			       j < my_mpi_size_row / my_cores_per_node_row;
			       j++)
			    {
			      locations[j] = locations2[j];
			    }
			}

		      code_put_char (&ip, OPCODE_NODEBARRIER, isdryrun);
		    }

		  num_comm_throttle = 0;
		  if (my_lrank_node < num_active_ports)
		    {
		      for (port = my_lrank_node; port < num_ports;
			   port +=
			   my_cores_per_node_row * my_cores_per_node_column)
			{
			  num_comm_throttle++;
			  compute_offsets (my_mpi_size_row /
					   my_cores_per_node_row, num_ports,
					   gbstep, port, &add, &isize);
			  if ((num_comm_throttle - 1) % chunks_throttle ==
			      i_throttle)
			    {
			      if (isize > 0)
				{
				  code_put_char (&ip, OPCODE_MPIISEND,
						 isdryrun);
				  code_put_pointer (&ip,
						    (void
						     *) (((char *)
							  my_shared_sendbuf) +
							 add * dsize *
							 my_cores_per_node_row
							 *
							 my_cores_per_node_row
							 *
							 my_cores_per_node_column),
						    isdryrun);
				  code_put_int (&ip,
						isize * dsize *
						my_cores_per_node_row *
						my_cores_per_node_row *
						my_cores_per_node_column,
						isdryrun);
				  code_put_int (&ip,
						global_ranks[(my_mpi_rank_global + (port + 1) * gbstep * my_cores_per_node_row * my_cores_per_node_column) % my_mpi_size_global], isdryrun);
				  code_put_pointer (&ip,
						    (void *) (locmem +
							      num_comm *
							      sizeof
							      (MPI_Request)),
						    isdryrun);
				  num_comm++;
				}
			    }
			}
		    }

		  if (i_throttle == 0)
		    {
		      if (gbstep == 1)
			{
			  code_put_char (&ip, OPCODE_MEMCPY, isdryrun);
			  code_put_pointer (&ip,
					    (void *) (((char *) recvbuf_host)
						      +
						      my_mpi_rank_row *
						      dsize), isdryrun);
			  code_put_pointer (&ip,
					    (void *) (((char *) sendbuf_host)
						      +
						      my_mpi_rank_row *
						      dsize), isdryrun);
			  code_put_int (&ip, dsize, isdryrun);
			  for (i = 0; i < my_cores_per_node_row; i++)
			    {
			      if (i != my_lrank_row)
				{
				  code_put_char (&ip, OPCODE_MEMCPY,
						 isdryrun);
				  code_put_pointer (&ip,
						    (void
						     *) (((char *)
							  recvbuf_host) +
							 (my_node *
							  my_cores_per_node_row
							  + i) * dsize),
						    isdryrun);
				  code_put_pointer (&ip,
						    (void
						     *) (((char *)
							  my_shared_sendbuf) +
							 ((i *
							   my_cores_per_node_row
							   +
							   my_lrank_row) *
							  my_cores_per_node_column
							  +
							  my_lrank_column) *
							 dsize), isdryrun);
				  code_put_int (&ip, dsize, isdryrun);
				}
			    }
			}
		    }
		  if (num_comm > 0)
		    {
		      code_put_char (&ip, OPCODE_MPIWAITALL, isdryrun);
		      code_put_int (&ip, num_comm, isdryrun);
		      code_put_pointer (&ip, (void *) locmem, isdryrun);
		      code_put_pointer (&ip,
					(void *) (locmem +
						  num_comm *
						  sizeof (MPI_Request)),
					isdryrun);
		    }
		  if (num_comm > num_comm_max)
		    {
		      num_comm_max = num_comm;
		    }
		  num_comm = 0;
		}
	      code_put_char (&ip, OPCODE_NODEBARRIER, isdryrun);
	      compute_offsets (my_mpi_size_row / my_cores_per_node_row,
			       num_ports, gbstep, -1, &add, &isize);
	      for (j = my_lrank_node + 1; j < isize;
		   j += my_cores_per_node_row * my_cores_per_node_column)
		{
		  code_put_char (&ip, OPCODE_MEMCPY, isdryrun);
		  code_put_pointer (&ip, (void *) (my_shared_recvbuf +
						   j * my_cores_per_node_row *
						   my_cores_per_node_row *
						   my_cores_per_node_column *
						   dsize), isdryrun);
		  code_put_pointer (&ip,
				    (void *) (my_shared_sendbuf +
					      j * my_cores_per_node_row *
					      my_cores_per_node_row *
					      my_cores_per_node_column *
					      dsize), isdryrun);
		  code_put_int (&ip,
				dsize * my_cores_per_node_row *
				my_cores_per_node_row *
				my_cores_per_node_column, isdryrun);
		}

	      if (gbstep * (num_ports + 1) >=
		  my_mpi_size_row / my_cores_per_node_row)
		{
		  for (j = 0; j < my_mpi_size_row / my_cores_per_node_row;
		       j++)
		    {
		      int jjj =
			(2 * my_mpi_size_row / my_cores_per_node_row -
			 locations[j] +
			 my_node) % (my_mpi_size_row / my_cores_per_node_row);
		      if (locations[j] != my_node)
			{
			  for (i = 0; i < my_cores_per_node_row; i++)
			    {
			      code_put_char (&ip, OPCODE_MEMCPY, isdryrun);
			      code_put_pointer (&ip,
						(void
						 *) (((char *) recvbuf_host) +
						     (i +
						      j *
						      my_cores_per_node_row) *
						     dsize), isdryrun);
			      code_put_pointer (&ip,
						(void
						 *) (((char *)
						      my_shared_recvbuf) +
						     (((i +
							jjj *
							my_cores_per_node_row)
						       *
						       my_cores_per_node_row +
						       my_lrank_row) *
						      my_cores_per_node_column
						      +
						      my_lrank_column) *
						     dsize), isdryrun);
			      code_put_int (&ip, dsize, isdryrun);
			    }
			}
		    }
		}

	      ptemp = my_shared_recvbuf;
	      my_shared_recvbuf = my_shared_middbuf;
	      my_shared_middbuf = ptemp;
	    }
	}
      code_put_char (&ip, OPCODE_CUDAMEMCPY, isdryrun);
      code_put_pointer (&ip, (void *) (recvbuf), isdryrun);
      code_put_pointer (&ip, (void *) (recvbuf_host), isdryrun);
      code_put_int (&ip, dsize * my_mpi_size_row, isdryrun);
      code_put_char (&ip, 1, isdryrun);
      code_put_char (&ip, OPCODE_RETURN, isdryrun);
    }
  free (global_ranks);
  return (handle);
}

int
local_alltoallv_init (void *sendbuf, int *sendcounts, int *sdispls,
		      MPI_Datatype sendtype, void *recvbuf, int *recvcounts,
		      int *rdispls, MPI_Datatype recvtype, MPI_Comm comm_row,
		      int my_cores_per_node_row, MPI_Comm comm_column,
		      int my_cores_per_node_column, int num_active_ports,
		      int chunks_throttle)
{
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
    my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node,
    my_mpi_size_global, my_mpi_rank_global;
  int handle, isdryrun, num_comm, num_comm_max;
  char volatile *my_shared_sendbuf, *my_shared_recvbuf;
  int *global_ranks, i, j, k, l, m, port, new_counts_displs, add, isize,
    my_size_shared_sendbuf, my_size_shared_recvbuf;
  char *ip, *shmem_old_gpu, *locmem_old;
  int lshmemid, shmem_size_old, locmem_size_old;
  int volatile *lshmem_sendcounts, *lshmem_recvcounts, *lshmem = NULL;
  int sendrecv_count_max, num_sendrecv, num_sendrecv_all;
  char *bytecode_buffer_host, *bytecode_buffer_device, *ipl;
  int num_comm_throttle, i_throttle;
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column)
    {
      num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
    }
  if (num_active_ports < 1)
    {
      num_active_ports = 1;
    }
  MPI_Type_size (sendtype, &type_size);
  handle = get_handle ();
  MPI_Comm_size (comm_row, &my_mpi_size_row);
  MPI_Comm_rank (comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Comm_size (comm_column, &my_mpi_size_column);
      MPI_Comm_rank (comm_column, &my_mpi_rank_column);
    }
  else
    {
      my_mpi_size_column = 1;
      my_mpi_rank_column = 0;
    }
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_column * my_cores_per_node_row + my_lrank_row;
  my_mpi_size_global = my_mpi_size_row * my_cores_per_node_column;
  my_mpi_rank_global =
    my_mpi_rank_row * my_cores_per_node_column +
    my_mpi_rank_column % my_cores_per_node_column;
  new_counts_displs = (sdispls == NULL);
  if (new_counts_displs)
    {
      sdispls = (int *) malloc (my_mpi_size_row * sizeof (int));
      recvcounts = (int *) malloc (my_mpi_size_row * sizeof (int));
      rdispls = (int *) malloc (my_mpi_size_row * sizeof (int));
      MPI_Alltoall (sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT, comm_row);
      sdispls[0] = 0;
      rdispls[0] = 0;
      for (i = 0; i < my_mpi_size_row - 1; i++)
	{
	  sdispls[i + 1] = sdispls[i] + sendcounts[i];
	  rdispls[i + 1] = rdispls[i] + recvcounts[i];
	}
    }
  setup_shared_memory (comm_row, my_cores_per_node_row, comm_column,
		       my_cores_per_node_column,
		       my_mpi_size_row * my_cores_per_node_row *
		       my_cores_per_node_column * 2 * sizeof (int), &lshmemid,
		       (volatile char **) (&lshmem), 0, 0);
  lshmem_sendcounts = lshmem;
  lshmem_recvcounts =
    lshmem +
    my_mpi_size_row * my_cores_per_node_row * my_cores_per_node_column;
  for (j = 0; j < my_mpi_size_row / my_cores_per_node_row; j++)
    {
      for (i = 0; i < my_cores_per_node_row; i++)
	{
	  lshmem_sendcounts[(i +
			     ((my_mpi_size_row / my_cores_per_node_row + j -
			       my_node) % (my_mpi_size_row /
					   my_cores_per_node_row)) *
			     my_cores_per_node_row) * my_cores_per_node_row *
			    my_cores_per_node_column +
			    my_lrank_row * my_cores_per_node_column +
			    my_lrank_column] =
	    sendcounts[i + j * my_cores_per_node_row];
	  lshmem_recvcounts[(my_lrank_row +
			     ((my_mpi_size_row / my_cores_per_node_row - j +
			       my_node) % (my_mpi_size_row /
					   my_cores_per_node_row)) *
			     my_cores_per_node_row) * my_cores_per_node_row *
			    my_cores_per_node_column +
			    i * my_cores_per_node_column + my_lrank_column] =
	    recvcounts[i + j * my_cores_per_node_row];
	}
    }
  MPI_Barrier (comm_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Barrier (comm_column);
      MPI_Barrier (comm_row);
    }
  my_size_shared_sendbuf = 0;
  my_size_shared_recvbuf = 0;
  for (i = 0;
       i < my_mpi_size_row * my_cores_per_node_row * my_cores_per_node_column;
       i++)
    {
      my_size_shared_sendbuf += lshmem_sendcounts[i];
      my_size_shared_recvbuf += lshmem_recvcounts[i];
    }
  my_size_shared_sendbuf *= type_size;
  my_size_shared_recvbuf *= type_size;
  shmem_size_old = shmem_size;
  shmem_old_gpu = (char *) shmem_gpu;
  if (shmem_size <= my_size_shared_sendbuf + my_size_shared_recvbuf)
    {
      shmem_size = my_size_shared_sendbuf + my_size_shared_recvbuf;
      if (shmem_gpu != NULL)
	{
	  cudaIpcCloseMemHandle ((void *) &shmemid_gpu);
	  if (shmemid != -1)
	    {
	      cudaFree ((void *) shmem_gpu);
	    }
	  shmem_gpu = NULL;
	}
    }
  setup_shared_memory (comm_row, my_cores_per_node_row, comm_column,
		       my_cores_per_node_column, NUM_BARRIERS, &shmemid,
		       &shmem, 0, NUM_BARRIERS);
  if (!setup_shared_memory_gpu
      (comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
       shmem_size, &shmemid_gpu, &shmem_gpu))
    {
      if (shmem_old_gpu != NULL)
	{
	  rebase_addresses (shmem_old_gpu, shmem_size_old,
			    (char *) shmem_gpu);
	}
    }
  global_ranks =
    (int *) malloc (sizeof (int) * my_mpi_size_row *
		    my_cores_per_node_column);
  setup_rank_translation (comm_row, my_cores_per_node_row, comm_column,
			  my_cores_per_node_column, global_ranks);
  my_shared_sendbuf = shmem_gpu;
  my_shared_recvbuf = shmem_gpu + my_size_shared_sendbuf;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--)
    {
      if (isdryrun)
	{
	  ip = NULL;
	}
      else
	{
	  if (num_comm_max * (sizeof (MPI_Request) + sizeof (MPI_Status)) >
	      locmem_size)
	    {
	      locmem_old = locmem;
	      locmem_size_old = locmem_size;
	      locmem_size =
		num_comm_max * (sizeof (MPI_Request) + sizeof (MPI_Status));
	      locmem = (char *) malloc (sizeof (char) * locmem_size);
	      if (locmem_old != NULL)
		{
		  rebase_addresses (locmem_old, locmem_size_old,
				    (char *) locmem);
		  free (locmem_old);
		}
	    }
	  ip = comm_code[handle] =
	    (char *) malloc (sizeof (char *) * ((size_t) (ip)));
	}
      code_put_char (&ip, OPCODE_SETNUMCORES, isdryrun);
      code_put_int (&ip, my_cores_per_node_row * my_cores_per_node_column,
		    isdryrun);
      num_comm_max = 0;
      for (i_throttle = 0; i_throttle < chunks_throttle; i_throttle++)
	{
	  num_comm = 0;
	  num_comm_throttle = 0;
	  if (my_lrank_node < num_active_ports)
	    {
	      for (port = my_lrank_node;
		   port < my_mpi_size_row / my_cores_per_node_row - 1;
		   port += num_active_ports)
		{
		  num_comm_throttle++;
		  add = 0;
		  m = 0;
		  for (i = 0;
		       i <
		       my_cores_per_node_row * my_cores_per_node_row *
		       my_cores_per_node_column * (port + 1); i++)
		    {
		      add += lshmem_recvcounts[m++];
		    }
		  isize = 0;
		  for (i = 0;
		       i <
		       my_cores_per_node_row * my_cores_per_node_row *
		       my_cores_per_node_column; i++)
		    {
		      isize +=
			lshmem_recvcounts[my_cores_per_node_row *
					  my_cores_per_node_row *
					  my_cores_per_node_column * (port +
								      1) + i];
		    }
		  if ((num_comm_throttle - 1) % chunks_throttle == i_throttle)
		    {
		      if (isize > 0)
			{
			  code_put_char (&ip, OPCODE_MPIIRECV, isdryrun);
			  code_put_pointer (&ip,
					    (void
					     *) (((char *) my_shared_recvbuf)
						 + add * type_size),
					    isdryrun);
			  code_put_int (&ip, isize * type_size, isdryrun);
			  code_put_int (&ip,
					global_ranks[(my_mpi_rank_global +
						      my_mpi_size_global -
						      (port +
						       1) *
						      my_cores_per_node_row *
						      my_cores_per_node_column)
						     % my_mpi_size_global],
					isdryrun);
			  code_put_pointer (&ip,
					    (void *) (locmem +
						      num_comm *
						      sizeof (MPI_Request)),
					    isdryrun);
			  num_comm++;
			}
		    }
		}
	    }

	  if (i_throttle == 0)
	    {
	      sendrecv_count_max = 0;
	      num_sendrecv = 0;
	      for (k = 0; k < my_mpi_size_row / my_cores_per_node_row; k++)
		{
		  for (i = 0; i < my_cores_per_node_row; i++)
		    {
		      j =
			(my_mpi_size_row / my_cores_per_node_row + k +
			 my_node) % (my_mpi_size_row / my_cores_per_node_row);
		      if (sendcounts[i + j * my_cores_per_node_row] *
			  type_size > sendrecv_count_max)
			{
			  sendrecv_count_max =
			    sendcounts[i +
				       j * my_cores_per_node_row] * type_size;
			}
		      num_sendrecv++;
		    }
		}
	      if (isdryrun)
		{
		  num_sendrecv_all = num_sendrecv;
		  bytecode_buffer_host = bytecode_buffer_device = NULL;
		}
	      else
		{
		  bytecode_buffer_host =
		    (char *) malloc (6 * sizeof (int) +
				     num_sendrecv_all * (sizeof (void *) * 2 +
							 sizeof (long)));
		  if (cudaMalloc
		      (&bytecode_buffer_device,
		       6 * sizeof (int) +
		       num_sendrecv_all * (sizeof (void *) * 2 +
					   sizeof (long))) != 0)
		    exit (11);
		}
	      comm_code_gpu[handle] = bytecode_buffer_device;
	      ipl = bytecode_buffer_host;
	      code_put_int (&ipl, num_sendrecv, isdryrun);
	      code_put_int (&ipl, sendrecv_count_max, isdryrun);
	      add = 0;
	      m = 0;
	      for (i = 0;
		   i <
		   my_lrank_row * my_cores_per_node_column + my_lrank_column;
		   i++)
		{
		  add += lshmem_sendcounts[m++];
		}
	      for (k = 0; k < my_mpi_size_row / my_cores_per_node_row; k++)
		{
		  for (i = 0; i < my_cores_per_node_row; i++)
		    {
		      j =
			(my_mpi_size_row / my_cores_per_node_row + k +
			 my_node) % (my_mpi_size_row / my_cores_per_node_row);
		      code_put_pointer (&ipl,
					(void *) (my_shared_sendbuf +
						  add * type_size), isdryrun);
		      code_put_pointer (&ipl,
					(void *) (((char *) sendbuf) +
						  sdispls[i +
							  j *
							  my_cores_per_node_row]
						  * type_size), isdryrun);
		      code_put_long (&ipl,
				     sendcounts[i +
						j * my_cores_per_node_row] *
				     type_size, isdryrun);
		      for (j = 0;
			   j <
			   my_cores_per_node_row * my_cores_per_node_column;
			   j++)
			{
			  add += lshmem_sendcounts[m++];
			}
		    }
		}
	      code_put_char (&ip, OPCODE_CUDACOPYKERNEL, isdryrun);
	      code_put_int (&ip, num_sendrecv, isdryrun);
	      code_put_int (&ip, sendrecv_count_max, isdryrun);
	      code_put_pointer (&ip, bytecode_buffer_device, isdryrun);

	      code_put_char (&ip, OPCODE_CUDADEVICESYNCHRONIZE, isdryrun);
	      code_put_char (&ip, OPCODE_NODEBARRIER, isdryrun);
	    }
	  num_comm_throttle = 0;
	  if (my_lrank_node < num_active_ports)
	    {
	      for (port = my_lrank_node;
		   port < my_mpi_size_row / my_cores_per_node_row - 1;
		   port += num_active_ports)
		{
		  num_comm_throttle++;
		  add = 0;
		  m = 0;
		  for (i = 0;
		       i <
		       my_cores_per_node_row * my_cores_per_node_row *
		       my_cores_per_node_column * (port + 1); i++)
		    {
		      add += lshmem_sendcounts[m++];
		    }
		  isize = 0;
		  for (i = 0;
		       i <
		       my_cores_per_node_row * my_cores_per_node_row *
		       my_cores_per_node_column; i++)
		    {
		      isize += lshmem_sendcounts[m++];
		    }
		  if ((num_comm_throttle - 1) % chunks_throttle == i_throttle)
		    {
		      if (isize > 0)
			{
			  code_put_char (&ip, OPCODE_MPIISEND, isdryrun);
			  code_put_pointer (&ip,
					    (void
					     *) (((char *) my_shared_sendbuf)
						 + add * type_size),
					    isdryrun);
			  code_put_int (&ip, isize * type_size, isdryrun);
			  code_put_int (&ip,
					global_ranks[(my_mpi_rank_global +
						      (port +
						       1) *
						      my_cores_per_node_row *
						      my_cores_per_node_column)
						     % my_mpi_size_global],
					isdryrun);
			  code_put_pointer (&ip,
					    (void *) (locmem +
						      num_comm *
						      sizeof (MPI_Request)),
					    isdryrun);
			  num_comm++;
			}
		    }
		}
	    }

	  if (i_throttle == chunks_throttle - 1)
	    {
	      sendrecv_count_max = 0;
	      num_sendrecv = 0;
	      for (i = 0; i < my_cores_per_node_row; i++)
		{
		  j =
		    (my_mpi_size_row / my_cores_per_node_row - 0 +
		     my_node) % (my_mpi_size_row / my_cores_per_node_row);
		  if (recvcounts[i + j * my_cores_per_node_row] * type_size >
		      sendrecv_count_max)
		    {
		      sendrecv_count_max =
			recvcounts[i + j * my_cores_per_node_row] * type_size;
		    }
		  num_sendrecv++;
		}
	      if (isdryrun)
		{
		  num_sendrecv_all += num_sendrecv;
		}
	      else
		{
		  bytecode_buffer_device += ipl - bytecode_buffer_host;
		}
	      code_put_int (&ipl, num_sendrecv, isdryrun);
	      code_put_int (&ipl, sendrecv_count_max, isdryrun);
	      for (i = 0; i < my_cores_per_node_row; i++)
		{
		  add = 0;
		  m = 0;
		  for (j = 0;
		       j < i * my_cores_per_node_column + my_lrank_column;
		       j++)
		    {
		      add += lshmem_recvcounts[m++];
		    }
		  for (k = 0; k < my_lrank_row; k++)
		    {
		      for (j = 0;
			   j <
			   my_cores_per_node_row * my_cores_per_node_column;
			   j++)
			{
			  add += lshmem_recvcounts[m++];
			}
		    }
		  for (j = 0;
		       j <
		       my_cores_per_node_row * my_cores_per_node_row *
		       my_cores_per_node_column * (i / my_cores_per_node_row);
		       j++)
		    {
		      add += lshmem_recvcounts[m++];
		    }
		  j =
		    (my_mpi_size_row / my_cores_per_node_row - 0 +
		     my_node) % (my_mpi_size_row / my_cores_per_node_row);
		  code_put_pointer (&ipl, (void *) (((char *) recvbuf) +
						    rdispls[i +
							    j *
							    my_cores_per_node_row]
						    * type_size), isdryrun);
		  code_put_pointer (&ipl,
				    (void *) (my_shared_sendbuf +
					      add * type_size), isdryrun);
		  code_put_long (&ipl,
				 recvcounts[i +
					    j * my_cores_per_node_row] *
				 type_size, isdryrun);
		}
	      code_put_char (&ip, OPCODE_CUDACOPYKERNEL, isdryrun);
	      code_put_int (&ip, num_sendrecv, isdryrun);
	      code_put_int (&ip, sendrecv_count_max, isdryrun);
	      code_put_pointer (&ip, bytecode_buffer_device, isdryrun);
	    }
	  if (num_comm > 0)
	    {
	      code_put_char (&ip, OPCODE_MPIWAITALL, isdryrun);
	      code_put_int (&ip, num_comm, isdryrun);
	      code_put_pointer (&ip, (void *) locmem, isdryrun);
	      code_put_pointer (&ip,
				(void *) (locmem +
					  num_comm * sizeof (MPI_Request)),
				isdryrun);
	    }
	  if (num_comm > num_comm_max)
	    {
	      num_comm_max = num_comm;
	    }
	  num_comm = 0;
	}
      code_put_char (&ip, OPCODE_CUDADEVICESYNCHRONIZE, isdryrun);
      code_put_char (&ip, OPCODE_NODEBARRIER, isdryrun);

      sendrecv_count_max = 0;
      num_sendrecv = 0;
      for (k = 1; k < my_mpi_size_row / my_cores_per_node_row; k++)
	{
	  for (l = 0; l < my_cores_per_node_row; l++)
	    {
	      j =
		(my_mpi_size_row / my_cores_per_node_row - k +
		 my_node) % (my_mpi_size_row / my_cores_per_node_row);
	      if (recvcounts[l + j * my_cores_per_node_row] * type_size >
		  sendrecv_count_max)
		{
		  sendrecv_count_max =
		    recvcounts[l + j * my_cores_per_node_row] * type_size;
		}
	      num_sendrecv++;
	    }
	}
      if (isdryrun)
	{
	  num_sendrecv_all += num_sendrecv;
	}
      else
	{
	  bytecode_buffer_device =
	    comm_code_gpu[handle] + (ipl - bytecode_buffer_host);
	}
      if (num_sendrecv > 0)
	{
	  code_put_int (&ipl, num_sendrecv, isdryrun);
	  code_put_int (&ipl, sendrecv_count_max, isdryrun);
	  for (k = 1; k < my_mpi_size_row / my_cores_per_node_row; k++)
	    {
	      for (l = 0; l < my_cores_per_node_row; l++)
		{
		  add = 0;
		  m = 0;
		  for (i = 0;
		       i < l * my_cores_per_node_column + my_lrank_column;
		       i++)
		    {
		      add += lshmem_recvcounts[m++];
		    }
		  for (j = 0; j < my_lrank_row; j++)
		    {
		      for (i = 0;
			   i <
			   my_cores_per_node_row * my_cores_per_node_column;
			   i++)
			{
			  add += lshmem_recvcounts[m++];
			}
		    }
		  for (i = 0;
		       i <
		       my_cores_per_node_row * my_cores_per_node_row *
		       my_cores_per_node_column * k; i++)
		    {
		      add += lshmem_recvcounts[m++];
		    }
		  j =
		    (my_mpi_size_row / my_cores_per_node_row - k +
		     my_node) % (my_mpi_size_row / my_cores_per_node_row);
		  code_put_pointer (&ipl, (void *) (((char *) recvbuf) +
						    rdispls[l +
							    j *
							    my_cores_per_node_row]
						    * type_size), isdryrun);
		  code_put_pointer (&ipl,
				    (void *) (((char *) my_shared_recvbuf) +
					      add * type_size), isdryrun);
		  code_put_long (&ipl,
				 recvcounts[l +
					    j * my_cores_per_node_row] *
				 type_size, isdryrun);
		}
	    }
	  code_put_char (&ip, OPCODE_CUDACOPYKERNEL, isdryrun);
	  code_put_int (&ip, num_sendrecv, isdryrun);
	  code_put_int (&ip, sendrecv_count_max, isdryrun);
	  code_put_pointer (&ip, bytecode_buffer_device, isdryrun);
	}
      if (!isdryrun)
	{
	  if (cudaMemcpy
	      (comm_code_gpu[handle], bytecode_buffer_host,
	       6 * sizeof (int) + num_sendrecv_all * (sizeof (void *) * 2 +
						      sizeof (long)),
	       cudaMemcpyHostToDevice) != 0)
	    exit (10);
	}
      free (bytecode_buffer_host);

      code_put_char (&ip, OPCODE_RETURN, isdryrun);
    }

  if (new_counts_displs)
    {
      free (rdispls);
      free (recvcounts);
      free (sdispls);
    }
  shmdt ((void *) lshmem);
  if (lshmemid != -1)
    {
      shmctl (lshmemid, IPC_RMID, NULL);
    }
  free (global_ranks);
  return (handle);
}

int barrier_count = 0;

void
node_barrier (int num_cores)
{
  __sync_fetch_and_add (shmem + barrier_count, 1);
  while (shmem[barrier_count] != num_cores)
    {;
    }
  shmem[(barrier_count + NUM_BARRIERS - 1) % NUM_BARRIERS] = 0;
  barrier_count = (barrier_count + 1) % NUM_BARRIERS;
}

int
local_alltoall_nonblocking (int handle)
{
  char instruction, *ip2, *ip;
  int handle2, isdryrun, numwaits;
  handle2 = get_handle ();
  for (isdryrun = 1; isdryrun >= 0; isdryrun--)
    {
      numwaits = 0;
      if (isdryrun)
	{
	  ip = comm_code[handle];
	  ip2 = NULL;
	}
      else
	{
	  comm_code[handle2] =
	    (char *) malloc (sizeof (char *) * ((size_t) (ip2)));
	  ip2 = ip = comm_code[handle];
	}
      do
	{
	  instruction = code_get_char (&ip);
	  switch (instruction)
	    {
	    case OPCODE_RETURN:
	      code_put_char (&ip2, OPCODE_RETURN, isdryrun);
	      break;
	    case OPCODE_MEMCPY:
	      code_put_char (&ip2, OPCODE_MEMCPY, isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      break;
	    case OPCODE_MPIIRECV:
	      code_put_char (&ip2, OPCODE_MPIIRECV, isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      break;
	    case OPCODE_MPIISEND:
	      code_put_char (&ip2, OPCODE_MPIISEND, isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      break;
	    case OPCODE_MPIWAITALL:
	      numwaits++;
	      if (numwaits > 1)
		{
		  printf ("multiple MPI_Waitall are not supported\n");
		  exit (1);
		}
	      if (!isdryrun)
		{
		  code_put_char (&ip2, OPCODE_RETURN, isdryrun);
		  ip2 = comm_code[handle2];
		}
	      code_put_char (&ip2, OPCODE_MPIWAITALL, isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      break;
	    case OPCODE_NODEBARRIER:
	      code_put_char (&ip2, OPCODE_NODEBARRIER, isdryrun);
	      break;
	    case OPCODE_SETNUMCORES:
	      code_put_char (&ip2, OPCODE_SETNUMCORES, isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      break;
	    case OPCODE_CUDAMEMCPY:
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_char (&ip2, code_get_char (&ip), isdryrun);
	      break;
	    case OPCODE_CUDACOPYKERNEL:
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_int (&ip2, code_get_int (&ip), isdryrun);
	      code_put_pointer (&ip2, code_get_pointer (&ip), isdryrun);
	      break;
	    case OPCODE_CUDADEVICESYNCHRONIZE:
	      code_put_char (&ip2, OPCODE_CUDADEVICESYNCHRONIZE, isdryrun);
	      break;
	    default:
	      printf ("illegal MPI_OPCODE\n");
	      exit (1);
	    }
	}
      while (instruction != OPCODE_RETURN);
      if (!isdryrun && (ip == ip2))
	{
	  ip2 = comm_code[handle2];
	  code_put_char (&ip2, OPCODE_RETURN, isdryrun);
	}
    }
  return (handle2);
}

__global__ void
cudacopykernel (char *data)
{
  int size, num, max_size, index, offset, i =
    blockIdx.x * blockDim.x + threadIdx.x;
  char *ldata, *p1, *p2;
  ldata = data;
  num = code_get_int (&ldata);
  max_size = code_get_int (&ldata);

  if (i < num * max_size)
    {
      index = i / max_size;
      offset = i % max_size;
      ldata += index * (sizeof (char *) * 2 + sizeof (long));
      p1 = (char *) code_get_pointer (&ldata);
      p2 = (char *) code_get_pointer (&ldata);
      size = code_get_long (&ldata);
      if (offset < size)
	{
	  p1[offset] = p2[offset];
	}
    }
}

int
EXT_MPI_Alltoall_init_native_gpu (void *sendbuf, int sendcount,
				  MPI_Datatype sendtype, void *recvbuf,
				  int recvcount, MPI_Datatype recvtype,
				  MPI_Comm comm_row,
				  int my_cores_per_node_row,
				  MPI_Comm comm_column,
				  int my_cores_per_node_column, int num_ports,
				  int num_active_ports, int chunks_throttle)
{
  return (local_alltoall_init
	  (sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype,
	   comm_row, my_cores_per_node_row, comm_column,
	   my_cores_per_node_column, num_ports, num_active_ports,
	   chunks_throttle));
}

int
EXT_MPI_Alltoallv_init_native_gpu (void *sendbuf, int *sendcounts,
				   int *sdispls, MPI_Datatype sendtype,
				   void *recvbuf, int *recvcounts,
				   int *rdispls, MPI_Datatype recvtype,
				   MPI_Comm comm_row,
				   int my_cores_per_node_row,
				   MPI_Comm comm_column,
				   int my_cores_per_node_column,
				   int num_active_ports, int chunks_throttle)
{
  return (local_alltoallv_init
	  (sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts,
	   rdispls, recvtype, comm_row, my_cores_per_node_row, comm_column,
	   my_cores_per_node_column, num_active_ports, chunks_throttle));
}

void
EXT_MPI_Ialltoall_init_native_gpu (void *sendbuf, int sendcount,
				   MPI_Datatype sendtype, void *recvbuf,
				   int recvcount, MPI_Datatype recvtype,
				   MPI_Comm comm_row,
				   int my_cores_per_node_row,
				   MPI_Comm comm_column,
				   int my_cores_per_node_column,
				   int num_active_ports, int *handle_begin,
				   int *handle_wait)
{
  int my_mpi_size_row, num_ports, chunks_throttle = 1;
  MPI_Comm_size (comm_row, &my_mpi_size_row);
  num_ports = my_mpi_size_row / my_cores_per_node_row - 1;
  *handle_begin =
    local_alltoall_init (sendbuf, sendcount, sendtype, recvbuf, recvcount,
			 recvtype, comm_row, my_cores_per_node_row,
			 comm_column, my_cores_per_node_column, num_ports,
			 num_active_ports, chunks_throttle);
  *handle_wait = local_alltoall_nonblocking (*handle_begin);
}

void
EXT_MPI_Ialltoallv_init_native_gpu (void *sendbuf, int *sendcounts,
				    int *sdispls, MPI_Datatype sendtype,
				    void *recvbuf, int *recvcounts,
				    int *rdispls, MPI_Datatype recvtype,
				    MPI_Comm comm_row,
				    int my_cores_per_node_row,
				    MPI_Comm comm_column,
				    int my_cores_per_node_column,
				    int num_active_ports, int *handle_begin,
				    int *handle_wait)
{
  int chunks_throttle = 1;
  *handle_begin =
    local_alltoallv_init (sendbuf, sendcounts, sdispls, sendtype, recvbuf,
			  recvcounts, rdispls, recvtype, comm_row,
			  my_cores_per_node_row, comm_column,
			  my_cores_per_node_column, num_active_ports,
			  chunks_throttle);
  *handle_wait = local_alltoall_nonblocking (*handle_begin);
}

int
EXT_MPI_Alltoall_exec_native_gpu (int handle)
{
  char instruction, *ip = comm_code[handle];
  void *p1, *p2;
  int num_cores, i1, i2;
  do
    {
      instruction = code_get_char (&ip);
      switch (instruction)
	{
	case OPCODE_MEMCPY:
	  p1 = code_get_pointer (&ip);
	  p2 = code_get_pointer (&ip);
	  memcpy (p1, p2, code_get_int (&ip));
	  break;
	case OPCODE_MPIIRECV:
	  p1 = code_get_pointer (&ip);
	  i1 = code_get_int (&ip);
	  i2 = code_get_int (&ip);
	  MPI_Irecv (p1, i1, MPI_CHAR, i2, 0, MPI_COMM_WORLD,
		     (MPI_Request *) code_get_pointer (&ip));
	  break;
	case OPCODE_MPIISEND:
	  p1 = code_get_pointer (&ip);
	  i1 = code_get_int (&ip);
	  i2 = code_get_int (&ip);
	  MPI_Isend (p1, i1, MPI_CHAR, i2, 0, MPI_COMM_WORLD,
		     (MPI_Request *) code_get_pointer (&ip));
	  break;
	case OPCODE_MPIWAITALL:
	  i1 = code_get_int (&ip);
	  p1 = code_get_pointer (&ip);
	  MPI_Waitall (i1, (MPI_Request *) p1,
		       (MPI_Status *) code_get_pointer (&ip));
	  break;
	case OPCODE_NODEBARRIER:
	  node_barrier (num_cores);
	  break;
	case OPCODE_SETNUMCORES:
	  num_cores = code_get_int (&ip);
	  break;
	case OPCODE_CUDAMEMCPY:
	  p1 = code_get_pointer (&ip);
	  p2 = code_get_pointer (&ip);
	  i1 = code_get_int (&ip);
	  switch (code_get_char (&ip))
	    {
	    case 0:
	      if (cudaMemcpy (p1, p2, i1, cudaMemcpyHostToHost) != 0)
		exit (23);
	      break;
	    case 1:
	      if (cudaMemcpy (p1, p2, i1, cudaMemcpyHostToDevice) != 0)
		exit (23);
	      break;
	    case 2:
	      if (cudaMemcpy (p1, p2, i1, cudaMemcpyDeviceToHost) != 0)
		exit (23);
	      break;
	    case 3:
	      if (cudaMemcpy (p1, p2, i1, cudaMemcpyDeviceToDevice) != 0)
		exit (23);
	      break;
	    }
	  break;
	case OPCODE_CUDACOPYKERNEL:
	  i1 = code_get_int (&ip);
	  i2 = code_get_int (&ip);
	  cudacopykernel <<< (i1 * i2) / 128 + 1,
	    128 >>> ((char *) code_get_pointer (&ip));
	  break;
	case OPCODE_CUDADEVICESYNCHRONIZE:
	  cudaDeviceSynchronize ();
	  break;
	}
    }
  while (instruction != OPCODE_RETURN);
  return (0);
}

int
EXT_MPI_Alltoall_done_native_gpu (int handle)
{
  int i;
  free (comm_code[handle]);
  comm_code[handle] = NULL;
  if (comm_code_gpu[handle] != NULL)
    {
      cudaFree (comm_code_gpu[handle]);
      comm_code_gpu[handle] = NULL;
    }
  for (i = 0; i < handle_max; i++)
    {
      if (comm_code[i] != NULL)
	{
	  return (0);
	}
    }
  shmdt ((void *) shmem);
  if (shmemid != -1)
    {
      shmctl (shmemid, IPC_RMID, NULL);
    }
  shmem = NULL;
  shmem_size = 0;
  shmemid = -1;
  free (locmem);
  locmem = NULL;
  locmem_size = 0;
  free (comm_code);
  comm_code = NULL;
  return (0);
}
