#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ext_mpi_alltoall.h"

int
main (int argc, char **argv)
{
  MPI_Status status;
  int my_mpi_size, my_mpi_rank;
  int *sendbuf, *recvbuf, msize, num_cores, handle, *sendcounts, *recvcounts,
    *sdispls, *rdispls, i, j;
  // Initialize the MPI environment
  MPI_Init (NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size (MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank (MPI_COMM_WORLD, &world_rank);
  num_cores = 1;
  msize = 1;
  if (argc == 3)
    {
      num_cores = atoi (argv[1]);
      msize = atoi (argv[2]);
    }
  else
    {
      if (world_rank == 0)
	{
	  printf ("arguments: num_cores msize\n");
	}
      MPI_Finalize ();
      exit (1);
    }

  srand (time (NULL));

  sendcounts = (int *) malloc (world_size * sizeof (int));
  recvcounts = (int *) malloc (world_size * sizeof (int));
  sdispls = (int *) malloc ((world_size + 1) * sizeof (int));
  rdispls = (int *) malloc ((world_size + 1) * sizeof (int));
  if (world_rank == 0)
    {
      for (i = 1; i < world_size; i++)
	{
	  for (j = 0; j < world_size; j++)
	    {
	      sendcounts[j] = rand () % (msize + 1);
	      sendcounts[j] = 10;
	    }
	  MPI_Send (sendcounts, world_size, MPI_INT, i, 0, MPI_COMM_WORLD);
	}
      for (j = 0; j < world_size; j++)
	{
	  sendcounts[j] = rand () % (msize + 1);
	}
    }
  else
    {
      MPI_Recv (sendcounts, world_size, MPI_INT, 0, 0, MPI_COMM_WORLD,
		&status);
    }
  MPI_Alltoall (sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT,
		MPI_COMM_WORLD);
  sdispls[0] = 0;
  rdispls[0] = 0;
  for (i = 1; i < world_size + 1; i++)
    {
      sdispls[i] = sdispls[i - 1] + sendcounts[i - 1];
      rdispls[i] = rdispls[i - 1] + recvcounts[i - 1];
    }

  sendbuf = (int *) malloc (sdispls[world_size] * sizeof (int));
  recvbuf = (int *) malloc (rdispls[world_size] * sizeof (int));

  for (i = 0; i < world_size; i++)
    {
      for (j = 0; j < recvcounts[i]; j++)
	{
	  recvbuf[rdispls[i] + j] = -11;
	}
    }
  for (i = 0; i < world_size; i++)
    {
      for (j = 0; j < sendcounts[i]; j++)
	{
	  sendbuf[sdispls[i] + j] = world_rank + i * world_size;
	}
    }

  EXT_MPI_Alltoallv_init_general (sendbuf, sendcounts, sdispls, MPI_INT,
				  recvbuf, recvcounts, rdispls, MPI_INT,
				  MPI_COMM_WORLD, num_cores, MPI_COMM_NULL, 1,
				  &handle);
  EXT_MPI_Alltoallv_exec (sendbuf, sendcounts, sdispls, MPI_INT, recvbuf,
			  recvcounts, rdispls, MPI_INT, MPI_COMM_WORLD,
			  handle);
  EXT_MPI_Alltoall_done (handle);

  for (i = 0; i < world_size; i++)
    {
      for (j = 0; j < recvcounts[i]; j++)
	{
	  if (recvbuf[rdispls[i] + j] != world_rank * world_size + i)
	    {
	      printf ("error in communication\n");
	      exit (1);
	    }
	}
    }

  free (sendbuf);
  free (recvbuf);
  free (rdispls);
  free (sdispls);
  free (recvcounts);
  free (sendcounts);
  MPI_Finalize ();
  if (world_rank == 0)
    {
      printf ("success\n");
    }
  return (0);
}
