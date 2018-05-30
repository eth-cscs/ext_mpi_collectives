#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ext_mpi_alltoall.h"

int
main (int argc, char **argv)
{
  int my_mpi_size, my_mpi_rank;
  int *sendbuf, *recvbuf, msize, handle, i, *sendcounts, *recvcounts,
    *sdispls, *rdispls;
  double start, stop, deltatmin, deltatmax, ttt;
  // Initialize the MPI environment
  MPI_Init (NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size (MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank (MPI_COMM_WORLD, &world_rank);

  srand (time (NULL) + world_rank);

  sendcounts = (int *) malloc (world_size * sizeof (int));
  recvcounts = (int *) malloc (world_size * sizeof (int));
  sdispls = (int *) malloc ((world_size + 1) * sizeof (int));
  rdispls = (int *) malloc ((world_size + 1) * sizeof (int));
  for (i = 0; i < world_size; i++)
    {
      sendcounts[i] = rand () % 10 + 1;
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

  for (i = 0; i < sdispls[world_size]; i++)
    {
      sendbuf[i] = -22;
    }
  for (i = 0; i < rdispls[world_size]; i++)
    {
      recvbuf[i] = -11;
    }
  for (i = 0; i < world_size; i++)
    {
      sendbuf[sdispls[i]] = world_rank + i * world_size;
    }

  EXT_MPI_Alltoallv_init_general (sendbuf, sendcounts, sdispls, MPI_INT,
				  recvbuf, recvcounts, rdispls, MPI_INT,
				  MPI_COMM_WORLD, 12, MPI_COMM_NULL, 1,
				  &handle);

  EXT_MPI_Alltoallv_exec (sendbuf, sendcounts, sdispls, MPI_INT, recvbuf,
			  recvcounts, rdispls, MPI_INT, MPI_COMM_WORLD,
			  handle);

//  printf ("aaaaaaaaa %d ", world_rank);
  for (i = 0; i < world_size; i++)
    {
//      printf ("%d ", recvbuf[rdispls[i]]);
    }
//  printf ("\n");

  EXT_MPI_Alltoall_done (handle);
//MPI_Finalize ();
//exit (1);

  free (sendbuf);
  free (recvbuf);
  msize = 1000;
  if (argc == 2)
    {
      msize = atoi (argv[1]);
    }
  sendbuf = (int *) malloc (world_size * sizeof (char) * msize);
  recvbuf = (int *) malloc (world_size * sizeof (char) * msize);
  for (i = 0; i < world_size; i++)
    {
      sendcounts[i] = msize;
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
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      MPI_Alltoallv (sendbuf, sendcounts, sdispls, MPI_CHAR, recvbuf,
		     recvcounts, rdispls, MPI_CHAR, MPI_COMM_WORLD);
//        MPI_Barrier(MPI_COMM_WORLD);
    }
  stop = MPI_Wtime ();
  ttt = stop - start;
  MPI_Reduce (&ttt, (void *) &deltatmin, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD);
  MPI_Reduce (&ttt, (void *) &deltatmax, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD);
  if (world_rank == 0)
    {
      printf ("reference %d %e %e\n", world_size, deltatmin, deltatmax);
    }
  EXT_MPI_Alltoallv_init_general (sendbuf, sendcounts, sdispls, MPI_CHAR,
				  recvbuf, recvcounts, rdispls, MPI_CHAR,
				  MPI_COMM_WORLD, 12, MPI_COMM_NULL, 1,
				  &handle);
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      EXT_MPI_Alltoallv_exec (sendbuf, sendcounts, sdispls, MPI_CHAR, recvbuf,
			      recvcounts, rdispls, MPI_CHAR, MPI_COMM_WORLD,
			      handle);
//        MPI_Barrier(MPI_COMM_WORLD);
    }
  stop = MPI_Wtime ();
  ttt = stop - start;
  MPI_Reduce (&ttt, (void *) &deltatmin, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD);
  MPI_Reduce (&ttt, (void *) &deltatmax, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD);
  if (world_rank == 0)
    {
      printf ("myroutine %d %e %e\n", world_size, deltatmin, deltatmax);
    }
  free (rdispls);
  free (sdispls);
  free (recvcounts);
  free (sendcounts);
  EXT_MPI_Alltoall_done (handle);
  // Finalize the MPI environment.
  MPI_Finalize ();
}
