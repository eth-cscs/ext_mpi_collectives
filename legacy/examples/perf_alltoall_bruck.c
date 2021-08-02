#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "ext_mpi_alltoall.h"
#include "ext_mpi_alltoall_native.h"

int
main (int argc, char **argv)
{
  int my_mpi_size, my_mpi_rank;
  int *sendbuf, *recvbuf, msize, num_cores, num_ports, num_active_ports,
    throttle, handle, i;
  double start, stop, deltatmin_ref, deltatmax_ref, deltatmin_my,
    deltatmax_my, ttt;
  MPI_Init (NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size (MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank (MPI_COMM_WORLD, &world_rank);
  num_cores = 12;
  msize = 1000;
  num_ports = -1;
  num_active_ports = num_cores;
  throttle = -1;
  if (argc == 3)
    {
      num_cores = atoi (argv[1]);
      msize = atoi (argv[2]);
    }
  else
    {
      if (argc == 6)
	{
	  num_cores = atoi (argv[1]);
	  msize = atoi (argv[2]);
	  num_ports = atoi (argv[3]);
	  num_active_ports = atoi (argv[4]);
	  throttle = atoi (argv[5]);
	}
    }

  sendbuf = (int *) malloc (world_size * sizeof (char) * msize);
  recvbuf = (int *) malloc (world_size * sizeof (char) * msize);
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      MPI_Alltoall (sendbuf, msize, MPI_CHAR, recvbuf, msize, MPI_CHAR,
		    MPI_COMM_WORLD);
//        MPI_Barrier(MPI_COMM_WORLD);
    }
  stop = MPI_Wtime ();
  ttt = stop - start;
  MPI_Reduce (&ttt, (void *) &deltatmin_ref, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD);
  MPI_Reduce (&ttt, (void *) &deltatmax_ref, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD);
  if (num_ports <= 0)
    {
      EXT_MPI_Alltoall_init_general (sendbuf, msize, MPI_CHAR, recvbuf, msize,
				     MPI_CHAR, MPI_COMM_WORLD, num_cores,
				     MPI_COMM_NULL, 1, &handle);
      MPI_Barrier (MPI_COMM_WORLD);
      start = MPI_Wtime ();
      for (i = 0; i < 10000; i++)
	{
	  EXT_MPI_Alltoall_exec (sendbuf, msize, MPI_CHAR, recvbuf, msize,
				 MPI_CHAR, MPI_COMM_WORLD, handle);
//        MPI_Barrier(MPI_COMM_WORLD);
	}
      stop = MPI_Wtime ();
      EXT_MPI_Alltoall_done (handle);
    }
  else
    {
      handle =
	EXT_MPI_Alltoall_init_native (sendbuf, msize, MPI_CHAR, recvbuf,
				      msize, MPI_CHAR, MPI_COMM_WORLD,
				      num_cores, MPI_COMM_NULL, 1, num_ports,
				      num_active_ports, throttle);
      MPI_Barrier (MPI_COMM_WORLD);
      start = MPI_Wtime ();
      for (i = 0; i < 10000; i++)
	{
	  EXT_MPI_Alltoall_exec_native (handle);
//        MPI_Barrier(MPI_COMM_WORLD);
	}
      stop = MPI_Wtime ();
      EXT_MPI_Alltoall_done_native (handle);
    }
  ttt = stop - start;
  MPI_Reduce (&ttt, (void *) &deltatmin_my, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD);
  MPI_Reduce (&ttt, (void *) &deltatmax_my, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD);
  if (world_rank == 0)
    {
      printf ("myroutine %d %d %d %d %d %d %e %e %e %e\n", world_size,
	      num_cores, msize, num_ports, num_active_ports, throttle,
	      deltatmin_ref, deltatmax_ref, deltatmin_my, deltatmax_my);
    }
  MPI_Finalize ();
}
