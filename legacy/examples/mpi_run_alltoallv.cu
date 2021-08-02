#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ext_mpi_alltoall_native_gpu.h"

int
main (int argc, char **argv)
{
  int *sendbuf_host, *recvbuf_host, *sendbuf_device, *recvbuf_device, msize,
    handle, i, *sendcounts, *recvcounts, *sdispls, *rdispls, num_cores, num_ports, num_active_ports, throttle;
  double start, stop, deltatmin_ref, deltatmax_ref, deltatmin_my, deltatmax_my, ttt;
  // Initialize the MPI environment
  MPI_Init (NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size (MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank (MPI_COMM_WORLD, &world_rank);

  num_cores = 12;
  num_ports = world_size;
  num_active_ports = num_cores;
  throttle = world_size;

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
  sendbuf_host = (int *) malloc (sdispls[world_size] * sizeof (int));
  recvbuf_host = (int *) malloc (rdispls[world_size] * sizeof (int));
  if (cudaMalloc (&sendbuf_device, sdispls[world_size] * sizeof (int)) != 0)
    exit (2);
  if (cudaMalloc (&recvbuf_device, rdispls[world_size] * sizeof (int)) != 0)
    exit (2);

  for (i = 0; i < sdispls[world_size]; i++)
    {
      sendbuf_host[i] = -22;
    }
  for (i = 0; i < rdispls[world_size]; i++)
    {
      recvbuf_host[i] = -11;
    }
  for (i = 0; i < world_size; i++)
    {
      sendbuf_host[sdispls[i]] = world_rank + i * world_size;
    }

  handle =
    EXT_MPI_Alltoallv_init_native_gpu (sendbuf_device, sendcounts, sdispls, MPI_INT,
		       recvbuf_device, recvcounts, rdispls, MPI_INT,
		       MPI_COMM_WORLD, num_cores, MPI_COMM_NULL, 1, num_active_ports, throttle);

  if (cudaMemcpy
      (sendbuf_device, sendbuf_host, sdispls[world_size] * sizeof (int),
       cudaMemcpyHostToDevice) != 0)
    exit (2);

  EXT_MPI_Alltoall_exec_native_gpu (handle);

  if (cudaMemcpy
      (recvbuf_host, recvbuf_device, rdispls[world_size] * sizeof (int),
       cudaMemcpyDeviceToHost) != 0)
    exit (2);

  EXT_MPI_Alltoall_done_native_gpu (handle);

//  printf ("aaaaaaaaa %d ", world_rank);
  for (i = 0; i < world_size; i++)
    {
//      printf ("%d ", recvbuf_host[rdispls[i]]);
    }
//  printf ("\n");
//MPI_Finalize();
//exit(1);

  free (rdispls);
  free (sdispls);
  free (recvcounts);
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
  MPI_Reduce (&ttt, (void *) &deltatmin_ref, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD);
  MPI_Reduce (&ttt, (void *) &deltatmax_ref, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD);
  for (i = 0; i < world_size; i++)
    {
      sendcounts[i] = msize;
    }
  handle =
    EXT_MPI_Alltoallv_init_native_gpu (sendbuf_device, sendcounts, NULL, MPI_CHAR,
		       recvbuf_device, NULL, NULL, MPI_CHAR, MPI_COMM_WORLD,
		       num_cores, MPI_COMM_NULL, 1, num_active_ports, throttle);
  cudaDeviceSynchronize ();
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      EXT_MPI_Alltoall_exec_native_gpu (handle);
//        MPI_Barrier(MPI_COMM_WORLD);
    }
  cudaDeviceSynchronize ();
  stop = MPI_Wtime ();
  ttt = stop - start;
  MPI_Reduce (&ttt, (void *) &deltatmin_my, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD);
  MPI_Reduce (&ttt, (void *) &deltatmax_my, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD);
  free (sendcounts);
  EXT_MPI_Alltoall_done_native_gpu (handle);
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
  MPI_Reduce (&ttt, (void *) &deltatmin_ref, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD);
  MPI_Reduce (&ttt, (void *) &deltatmax_ref, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD);
  if (world_rank == 0)
    {
      printf ("reference %d %d %d %d %d %d %e %e %e %e\n", world_size, num_cores, msize, num_ports, num_active_ports, throttle, deltatmin_ref, deltatmax_ref, deltatmin_my, deltatmax_my);
    }

  MPI_Finalize ();
}
