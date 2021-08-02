#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "mpi_alltoall_general.h"

int
main (int argc, char **argv)
{
  int *sendbuf_host, *recvbuf_host, *sendbuf_device, *recvbuf_device, msize,
    handle, i;
  double start, stop, deltatmin, deltatmax, ttt;
  // Initialize the MPI environment
  MPI_Init (NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size (MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank (MPI_COMM_WORLD, &world_rank);

  sendbuf_host = (int *) malloc (world_size * sizeof (int));
  recvbuf_host = (int *) malloc (world_size * sizeof (int));
  if (cudaMalloc (&sendbuf_device, world_size * sizeof (int)) != 0)
    exit (2);
  if (cudaMalloc (&recvbuf_device, world_size * sizeof (int)) != 0)
    exit (2);

  for (i = 0; i < world_size; i++)
    {
      sendbuf_host[i] = world_rank + i * world_size;
      recvbuf_host[i] = -11;
    }

  handle =
    MY_Alltoall_init (sendbuf_device, 1, MPI_INT, recvbuf_device, 1, MPI_INT,
		      MPI_COMM_WORLD, 12, MPI_COMM_NULL, 1);

  if (cudaMemcpy
      (sendbuf_device, sendbuf_host, world_size * sizeof (int),
       cudaMemcpyHostToDevice) != 0)
    exit (2);

  MY_Alltoall (handle);

  if (cudaMemcpy
      (recvbuf_host, recvbuf_device, world_size * sizeof (int),
       cudaMemcpyDeviceToHost) != 0)
    exit (12);

  MY_Alltoall_done (handle);

//  printf ("aaaaaaaaa %d ", world_rank);
  for (i = 0; i < world_size; i++)
    {
//      printf ("%d ", recvbuf_host[i]);
    }
//  printf ("\n");
//MPI_Finalize ();
//exit(1);

  free (sendbuf_host);
  free (recvbuf_host);
  cudaFree (sendbuf_device);
  cudaFree (recvbuf_device);

  msize = 1000;
  if (argc == 2)
    {
      msize = atoi (argv[1]);
    }
  if (cudaMalloc (&sendbuf_device, msize * sizeof (char) * world_size) != 0)
    exit (2);
  if (cudaMemset (sendbuf_device, 0, msize) != 0)
    exit (2);
  if (cudaMalloc (&recvbuf_device, msize * sizeof (char) * world_size) != 0)
    exit (2);
  if (cudaMemset (recvbuf_device, 0, msize) != 0)
    exit (2);
  cudaDeviceSynchronize ();
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      MPI_Alltoall (sendbuf_device, msize, MPI_CHAR, recvbuf_device, msize,
		    MPI_CHAR, MPI_COMM_WORLD);
//        MPI_Barrier(MPI_COMM_WORLD);
    }
  cudaDeviceSynchronize ();
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
  handle =
    MY_Alltoall_init (sendbuf_device, msize, MPI_CHAR, recvbuf_device, msize,
		      MPI_CHAR, MPI_COMM_WORLD, 12, MPI_COMM_NULL, 1);
  cudaDeviceSynchronize ();
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      MY_Alltoall (handle);
//        MPI_Barrier(MPI_COMM_WORLD);
    }
  cudaDeviceSynchronize ();
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
  MY_Alltoall_done (handle);
  cudaDeviceSynchronize ();
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      MPI_Alltoall (sendbuf_device, msize, MPI_CHAR, recvbuf_device, msize,
		    MPI_CHAR, MPI_COMM_WORLD);
//        MPI_Barrier(MPI_COMM_WORLD);
    }
  cudaDeviceSynchronize ();
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

  MPI_Finalize ();
}
