#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "ext_mpi_alltoall.h"
#include "ext_mpi_alltoall_native.h"

#define BREAK_SIZE 25000
#define MERGE_SIZE 250

int
get_num_cores_per_node (MPI_Comm comm)
{
  int my_mpi_rank, num_cores, num_cores_min, num_cores_max;
  MPI_Comm comm_node;
  MPI_Info info;
  MPI_Comm_rank (comm, &my_mpi_rank);
  MPI_Info_create (&info);
  MPI_Comm_split_type (comm, MPI_COMM_TYPE_SHARED, my_mpi_rank, info,
		       &comm_node);
  MPI_Info_free (&info);
  MPI_Comm_size (comm_node, &num_cores);
  MPI_Comm_free (&comm_node);
  MPI_Allreduce (&num_cores, &num_cores_min, 1, MPI_INT, MPI_MIN, comm);
  MPI_Allreduce (&num_cores, &num_cores_max, 1, MPI_INT, MPI_MAX, comm);
  if (num_cores_min == num_cores_max)
    {
      return (num_cores);
    }
  else
    {
      return (-1);
    }
}

int
break_tiles (int message_size, int *tmsize, int *my_cores_per_node_row,
	     int *my_cores_per_node_column)
{
  int i;
  (*tmsize) *=
    (*my_cores_per_node_row) * (*my_cores_per_node_row) *
    (*my_cores_per_node_column);
  if ((*my_cores_per_node_column) == 1)
    {
      i = 2;
      while ((*tmsize > message_size) && (i <= *my_cores_per_node_row))
	{
	  if ((*my_cores_per_node_row) % i == 0)
	    {
	      (*my_cores_per_node_row) /= i;
	      (*tmsize) /= (i * i);
	    }
	  else
	    {
	      i++;
	    }
	}
    }
  return (*tmsize <= message_size);
}

int
EXT_MPI_Alltoall_init_general (void *sendbuf, int sendcount,
			       MPI_Datatype sendtype, void *recvbuf,
			       int recvcount, MPI_Datatype recvtype,
			       MPI_Comm comm_row, int my_cores_per_node_row,
			       MPI_Comm comm_column,
			       int my_cores_per_node_column, int *handle)
{
  int my_mpi_size_row, num_ports, type_size, tmsize, chunks_throttle, i;
  MPI_Comm_size (comm_row, &my_mpi_size_row);
  MPI_Type_size (sendtype, &type_size);
  tmsize = type_size * sendcount;
  if (tmsize < 1)
    {
      tmsize = 1;
    }
  tmsize *=
    my_cores_per_node_row * my_cores_per_node_row * my_cores_per_node_column;
  num_ports = my_mpi_size_row / my_cores_per_node_row - 1;
  if ((tmsize < MERGE_SIZE)
      && (num_ports >= my_cores_per_node_row * my_cores_per_node_column))
    {
      while ((tmsize < MERGE_SIZE)
	     && (num_ports >=
		 my_cores_per_node_row * my_cores_per_node_column))
	{
	  tmsize *= 2;
	  num_ports = (num_ports - 1) / 4 + 1;
	}
      if (num_ports < my_cores_per_node_row * my_cores_per_node_column)
	{
	  num_ports = my_cores_per_node_row * my_cores_per_node_column;
	}
      chunks_throttle =
	(num_ports / (my_cores_per_node_row * my_cores_per_node_column)) / 3 +
	1;
      *handle =
	EXT_MPI_Alltoall_init_native (sendbuf, sendcount, sendtype, recvbuf,
				      recvcount, recvtype, comm_row,
				      my_cores_per_node_row, comm_column,
				      my_cores_per_node_column, num_ports,
				      my_cores_per_node_row *
				      my_cores_per_node_column,
				      chunks_throttle);
      return (0);
    }
  tmsize = type_size * sendcount;
  if (break_tiles
      (BREAK_SIZE, &tmsize, &my_cores_per_node_row,
       &my_cores_per_node_column))
    {
      num_ports = my_mpi_size_row / my_cores_per_node_row - 1;
      chunks_throttle =
	(num_ports / (my_cores_per_node_row * my_cores_per_node_column)) / 3 +
	1;
      *handle =
	EXT_MPI_Alltoall_init_native (sendbuf, sendcount, sendtype, recvbuf,
				      recvcount, recvtype, comm_row,
				      my_cores_per_node_row, comm_column,
				      my_cores_per_node_column, num_ports,
				      my_cores_per_node_row *
				      my_cores_per_node_column,
				      chunks_throttle);
    }
  else
    {
      *handle = -1;
    }
  return (0);
}

int
EXT_MPI_Alltoallv_init_general (void *sendbuf, int *sendcounts, int *sdispls,
				MPI_Datatype sendtype, void *recvbuf,
				int *recvcounts, int *rdispls,
				MPI_Datatype recvtype, MPI_Comm comm_row,
				int my_cores_per_node_row,
				MPI_Comm comm_column,
				int my_cores_per_node_column, int *handle)
{
  int my_mpi_size_row, chunks_throttle, element_max_size, type_size, tmsize,
    i;
  MPI_Comm_size (comm_row, &my_mpi_size_row);
  element_max_size = 0;
  for (i = 0; i < my_mpi_size_row; i++)
    {
      if (sendcounts[i] > element_max_size)
	{
	  element_max_size = sendcounts[i];
	}
    }
  MPI_Type_size (sendtype, &type_size);
  tmsize = type_size * element_max_size;
  MPI_Allreduce (MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Allreduce (MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_column);
    }
  if (break_tiles
      (BREAK_SIZE, &tmsize, &my_cores_per_node_row,
       &my_cores_per_node_column))
    {
      chunks_throttle =
	((my_mpi_size_row / my_cores_per_node_row - 1) /
	 (my_cores_per_node_row * my_cores_per_node_column)) / 3 + 1;
      *handle =
	EXT_MPI_Alltoallv_init_native (sendbuf, sendcounts, sdispls, sendtype,
				       recvbuf, recvcounts, rdispls, recvtype,
				       comm_row, my_cores_per_node_row,
				       comm_column, my_cores_per_node_column,
				       my_cores_per_node_row *
				       my_cores_per_node_column,
				       chunks_throttle);
    }
  else
    {
      *handle = -1;
    }
  return (0);
}

int
EXT_MPI_Ialltoall_init_general (void *sendbuf, int sendcount,
				MPI_Datatype sendtype, void *recvbuf,
				int recvcount, MPI_Datatype recvtype,
				MPI_Comm comm_row, int my_cores_per_node_row,
				MPI_Comm comm_column,
				int my_cores_per_node_column,
				int *handle_begin, int *handle_wait)
{
  int type_size, tmsize;
  MPI_Type_size (sendtype, &type_size);
  tmsize = type_size * sendcount;
  if (break_tiles
      (BREAK_SIZE, &tmsize, &my_cores_per_node_row,
       &my_cores_per_node_column))
    {
      EXT_MPI_Ialltoall_init_native (sendbuf, sendcount, sendtype, recvbuf,
				     recvcount, recvtype, comm_row,
				     my_cores_per_node_row, comm_column,
				     my_cores_per_node_column,
				     my_cores_per_node_row *
				     my_cores_per_node_column, handle_begin,
				     handle_wait);
    }
  else
    {
      *handle_begin = *handle_wait = -1;
    }
  return (0);
}

int
EXT_MPI_Ialltoallv_init_general (void *sendbuf, int *sendcounts, int *sdispls,
				 MPI_Datatype sendtype, void *recvbuf,
				 int *recvcounts, int *rdispls,
				 MPI_Datatype recvtype, MPI_Comm comm_row,
				 int my_cores_per_node_row,
				 MPI_Comm comm_column,
				 int my_cores_per_node_column,
				 int *handle_begin, int *handle_wait)
{
  int my_mpi_size_row, chunks_throttle, element_max_size, type_size, tmsize,
    i;
  MPI_Comm_size (comm_row, &my_mpi_size_row);
  element_max_size = 0;
  for (i = 0; i < my_mpi_size_row; i++)
    {
      if (sendcounts[i] > element_max_size)
	{
	  element_max_size = sendcounts[i];
	}
    }
  MPI_Type_size (sendtype, &type_size);
  tmsize = type_size * element_max_size;
  MPI_Allreduce (MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL)
    {
      MPI_Allreduce (MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_column);
    }
  if (break_tiles
      (BREAK_SIZE, &tmsize, &my_cores_per_node_row,
       &my_cores_per_node_column))
    {
      chunks_throttle =
	(my_mpi_size_row / my_cores_per_node_row /
	 (my_cores_per_node_row * my_cores_per_node_column)) / 3 + 1;
      EXT_MPI_Ialltoallv_init_native (sendbuf, sendcounts, sdispls, sendtype,
				      recvbuf, recvcounts, rdispls, recvtype,
				      comm_row, my_cores_per_node_row,
				      comm_column, my_cores_per_node_column,
				      my_cores_per_node_row *
				      my_cores_per_node_column, handle_begin,
				      handle_wait);
    }
  else
    {
      *handle_begin = *handle_wait = -1;
    }
  return (0);
}

int
EXT_MPI_Alltoall_init (void *sendbuf, int sendcount, MPI_Datatype sendtype,
		       void *recvbuf, int recvcount, MPI_Datatype recvtype,
		       MPI_Comm comm, int *handle)
{
  int num_core_per_node = get_num_cores_per_node (comm);
  if (num_core_per_node > 0)
    {
      return (EXT_MPI_Alltoall_init_general
	      (sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype,
	       comm, num_core_per_node, MPI_COMM_NULL, 1, handle));
    }
  else
    {
      *handle = -1;
      return (0);
    }
}

int
EXT_MPI_Alltoallv_init (void *sendbuf, int *sendcounts, int *sdispls,
			MPI_Datatype sendtype, void *recvbuf, int *recvcounts,
			int *rdispls, MPI_Datatype recvtype, MPI_Comm comm,
			int *handle)
{
  int num_core_per_node = get_num_cores_per_node (comm);
  if (num_core_per_node > 0)
    {
      return (EXT_MPI_Alltoallv_init_general
	      (sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts,
	       rdispls, recvtype, comm, num_core_per_node, MPI_COMM_NULL, 1,
	       handle));
    }
  else
    {
      *handle = -1;
      return (0);
    }
}

int
EXT_MPI_Ialltoall_init (void *sendbuf, int sendcount, MPI_Datatype sendtype,
			void *recvbuf, int recvcount, MPI_Datatype recvtype,
			MPI_Comm comm, int *handle_begin, int *handle_wait)
{
  int num_core_per_node = get_num_cores_per_node (comm);
  if (num_core_per_node > 0)
    {
      return (EXT_MPI_Ialltoall_init_general
	      (sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype,
	       comm, num_core_per_node, MPI_COMM_NULL, 1, handle_begin,
	       handle_wait));
    }
  else
    {
      *handle_begin = *handle_wait = -1;
      return (0);
    }
}

int
EXT_MPI_Ialltoallv_init (void *sendbuf, int *sendcounts, int *sdispls,
			 MPI_Datatype sendtype, void *recvbuf,
			 int *recvcounts, int *rdispls, MPI_Datatype recvtype,
			 MPI_Comm comm, int *handle_begin, int *handle_wait)
{
  int num_core_per_node = get_num_cores_per_node (comm);
  if (num_core_per_node > 0)
    {
      return (EXT_MPI_Ialltoallv_init_general
	      (sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts,
	       rdispls, recvtype, comm, num_core_per_node, MPI_COMM_NULL, 1,
	       handle_begin, handle_wait));
    }
  else
    {
      *handle_begin = *handle_wait = -1;
      return (0);
    }
}

int
EXT_MPI_Alltoall_exec (const void *sendbuf, int sendcount,
		       MPI_Datatype sendtype, void *recvbuf, int recvcount,
		       MPI_Datatype recvtype, MPI_Comm comm, int handle)
{
  if (handle < 0)
    {
      return (MPI_Alltoall
	      (sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype,
	       comm));
    }
  else
    {
      return (EXT_MPI_Alltoall_exec_native (handle));
    }
}

int
EXT_MPI_Alltoallv_exec (const void *sendbuf, const int *sendcounts,
			const int *sdispls, MPI_Datatype sendtype,
			void *recvbuf, const int *recvcounts,
			const int *rdispls, MPI_Datatype recvtype,
			MPI_Comm comm, int handle)
{
  if (handle < 0)
    {
      return (MPI_Alltoallv
	      (sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts,
	       rdispls, recvtype, comm));
    }
  else
    {
      return (EXT_MPI_Alltoall_exec_native (handle));
    }
}

int
EXT_MPI_Ialltoall_begin (const void *sendbuf, int sendcount,
			 MPI_Datatype sendtype, void *recvbuf, int recvcount,
			 MPI_Datatype recvtype, MPI_Comm comm,
			 MPI_Request * request, int handle_begin)
{
  if (handle_begin < 0)
    {
      return (MPI_Ialltoall
	      (sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype,
	       comm, request));
    }
  else
    {
      return (EXT_MPI_Alltoall_exec_native (handle_begin));
    }
}

int
EXT_MPI_Ialltoall_wait (MPI_Request * request, int handle_wait)
{
  MPI_Status status;
  if (handle_wait < 0)
    {
      return (MPI_Wait (request, &status));
    }
  else
    {
      return (EXT_MPI_Alltoall_exec_native (handle_wait));
    }
}

int
EXT_MPI_Ialltoallv_begin (const void *sendbuf, const int *sendcounts,
			  const int *sdispls, MPI_Datatype sendtype,
			  void *recvbuf, const int *recvcounts,
			  const int *rdispls, MPI_Datatype recvtype,
			  MPI_Comm comm, MPI_Request * request,
			  int handle_begin)
{
  if (handle_begin < 0)
    {
      return (MPI_Ialltoallv
	      (sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts,
	       rdispls, recvtype, comm, request));
    }
  else
    {
      return (EXT_MPI_Alltoall_exec_native (handle_begin));
    }
}

int
EXT_MPI_Ialltoallv_wait (MPI_Request * request, int handle_wait)
{
  MPI_Status status;
  if (handle_wait < 0)
    {
      return (MPI_Wait (request, &status));
    }
  else
    {
      return (EXT_MPI_Alltoall_exec_native (handle_wait));
    }
}

int
EXT_MPI_Alltoall_done (int handle)
{
  if (handle >= 0)
    {
      return (EXT_MPI_Alltoall_done_native (handle));
    }
  else
    {
      return (0);
    }
}
