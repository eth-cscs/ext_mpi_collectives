#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "ext_mpi_alltoall.h"

int
main (int argc, char **argv)
{
  int my_mpi_size, my_mpi_rank;
  int *sendbuf, *recvbuf, msize, handle, i, j, num_cores;
  double start, stop, deltatmin, deltatmax, ttt;
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

  sendbuf = (int *) malloc (world_size * msize * sizeof (int));
  recvbuf = (int *) malloc (world_size * msize * sizeof (int));

  for (i = 0; i < world_size; i++)
    {
      for (j = 0; j < msize; j++)
	{
	  sendbuf[i * msize + j] = world_rank + i * world_size;
	  recvbuf[i * msize + j] = -11;
	}
    }

  EXT_MPI_Alltoall_init_general (sendbuf, msize, MPI_INT, recvbuf, msize,
				 MPI_INT, MPI_COMM_WORLD, 12, MPI_COMM_NULL,
				 1, &handle);

  EXT_MPI_Alltoall_exec (sendbuf, msize, MPI_INT, recvbuf, msize, MPI_INT,
			 MPI_COMM_WORLD, handle);

  EXT_MPI_Alltoall_done (handle);

  for (i = 0; i < world_size; i++)
    {
      for (j = 0; j < msize; j++)
	{
	  if (recvbuf[i * msize + j] != world_rank * world_size + i)
	    {
	      printf ("error in communication\n");
	      exit (1);
	    }
	}
    }
  free (sendbuf);
  free (recvbuf);
  MPI_Finalize ();
  if (world_rank == 0)
    {
      printf ("success\n");
    }
  return (0);
}
