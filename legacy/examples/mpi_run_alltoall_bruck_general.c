#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "ext_mpi_alltoall.h"

#define MY_CORES_PER_NODE_H 3
#define MY_CORES_PER_NODE_V 4

int
my_color_v (int my_mpi_rank, int vertical_horizontal, int my_cores_per_node_v,
	    int my_cores_per_node_h)
{
  return ((my_mpi_rank / my_cores_per_node_v) % my_cores_per_node_h +
	  (my_mpi_rank / (vertical_horizontal * my_cores_per_node_h)) *
	  my_cores_per_node_h);
}

int
my_color_h (int my_mpi_rank, int vertical_horizontal, int my_cores_per_node_v,
	    int my_cores_per_node_h)
{
  return (((my_mpi_rank / (my_cores_per_node_v * my_cores_per_node_h)) *
	   my_cores_per_node_v) % (vertical_horizontal) +
	  my_mpi_rank % my_cores_per_node_v);
}

int
my_rank_v (int my_mpi_rank, int vertical_horizontal, int my_cores_per_node_v,
	   int my_cores_per_node_h)
{
  return (((my_mpi_rank / (my_cores_per_node_v * my_cores_per_node_h)) *
	   my_cores_per_node_v) % (vertical_horizontal) +
	  my_mpi_rank % my_cores_per_node_v);
}

int
my_rank_h (int my_mpi_rank, int vertical_horizontal, int my_cores_per_node_v,
	   int my_cores_per_node_h)
{
  return ((my_mpi_rank / my_cores_per_node_v) % my_cores_per_node_h +
	  (my_mpi_rank / (vertical_horizontal * my_cores_per_node_h)) *
	  my_cores_per_node_h);
}

int
ADD_MPI_Comm_split_special (MPI_Comm my_comm_all, MPI_Comm * my_comm_v,
			    MPI_Comm * my_comm_h)
{
  int my_mpi_rank, my_mpi_size, vertical_horizontal;
  MPI_Comm_size (MPI_COMM_WORLD, &my_mpi_size);
  MPI_Comm_rank (MPI_COMM_WORLD, &my_mpi_rank);
  int color, rank;
  vertical_horizontal = 1;
  while (vertical_horizontal * vertical_horizontal < my_mpi_size)
    {
      vertical_horizontal++;
    }
  if ((vertical_horizontal * vertical_horizontal != my_mpi_size)
      || (vertical_horizontal % MY_CORES_PER_NODE_H)
      || (vertical_horizontal % MY_CORES_PER_NODE_V))
    {
      return (-2);
    }
  color =
    my_color_v (my_mpi_rank, vertical_horizontal, MY_CORES_PER_NODE_V,
		MY_CORES_PER_NODE_H);
  rank =
    my_rank_v (my_mpi_rank, vertical_horizontal, MY_CORES_PER_NODE_V,
	       MY_CORES_PER_NODE_H);
  MPI_Comm_split (my_comm_all, color, rank, my_comm_v);
  color =
    my_color_h (my_mpi_rank, vertical_horizontal, MY_CORES_PER_NODE_V,
		MY_CORES_PER_NODE_H);
  rank =
    my_rank_h (my_mpi_rank, vertical_horizontal, MY_CORES_PER_NODE_V,
	       MY_CORES_PER_NODE_H);
  MPI_Comm_split (my_comm_all, color, rank, my_comm_h);
  return (0);
}

int
main (int argc, char **argv)
{
  MPI_Comm my_comm_h, my_comm_v;
  int *sendbuf, *recvbuf, msize, vh_size, handle_v, handle_h, i;
  double start, stop, deltatmin, deltatmax, ttt;
  // Initialize the MPI environment
  MPI_Init (NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size (MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank (MPI_COMM_WORLD, &world_rank);

  ADD_MPI_Comm_split_special (MPI_COMM_WORLD, &my_comm_v, &my_comm_h);

  vh_size = 1;
  while (vh_size * vh_size < world_size)
    vh_size++;
  if (vh_size * vh_size != world_size)
    exit (1);

  sendbuf = malloc (vh_size * sizeof (int));
  recvbuf = malloc (vh_size * sizeof (int));

  int group_size_v, group_rank_v, group_size_h, group_rank_h;
  MPI_Comm_size (my_comm_v, &group_size_v);
  MPI_Comm_rank (my_comm_v, &group_rank_v);
  MPI_Comm_size (my_comm_h, &group_size_h);
  MPI_Comm_rank (my_comm_h, &group_rank_h);

  for (i = 0; i < vh_size; i++)
    {
      sendbuf[i] =
	group_rank_v + i * group_size_v +
	group_size_h * group_size_v * group_rank_h;
      recvbuf[i] = -11;
    }

  EXT_MPI_Alltoall_init_general (sendbuf, 1, MPI_INT, recvbuf, 1, MPI_INT,
				 my_comm_v, MY_CORES_PER_NODE_V, my_comm_h,
				 MY_CORES_PER_NODE_H, &handle_v);
  EXT_MPI_Alltoall_exec (sendbuf, 1, MPI_INT, recvbuf, 1, MPI_INT, my_comm_v,
			 handle_v);
  EXT_MPI_Alltoall_done (handle_v);

//    printf("aaaaaaaaa %d ", group_rank_v+group_size_v*group_rank_h);
  for (i = 0; i < vh_size; i++)
    {
//        printf("%d ", recvbuf[i]);
    }
//    printf("\n");

  for (i = 0; i < vh_size; i++)
    {
      sendbuf[i] =
	group_rank_h + i * group_size_h +
	group_size_h * group_size_v * group_rank_v;
      recvbuf[i] = -11;
    }

  EXT_MPI_Alltoall_init_general (sendbuf, 1, MPI_INT, recvbuf, 1, MPI_INT,
				 my_comm_h, MY_CORES_PER_NODE_H, my_comm_v,
				 MY_CORES_PER_NODE_V, &handle_h);
  EXT_MPI_Alltoall_exec (sendbuf, 1, MPI_INT, recvbuf, 1, MPI_INT, my_comm_h,
			 handle_h);
  EXT_MPI_Alltoall_done (handle_h);

//    printf("bbbbbbbbb %d ", group_rank_h+group_size_h*group_rank_v);
  for (i = 0; i < vh_size; i++)
    {
//        printf("%d ", recvbuf[i]);
    }
//    printf("\n");
//MPI_Finalize();
//exit(1);

  free (sendbuf);
  free (recvbuf);
  msize = 1000;
  if (argc == 2)
    {
      msize = atoi (argv[1]);
    }
  sendbuf = malloc (vh_size * sizeof (char) * msize);
  recvbuf = malloc (vh_size * sizeof (char) * msize);
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      MPI_Alltoall (sendbuf, msize, MPI_CHAR, recvbuf, msize, MPI_CHAR,
		    my_comm_v);
      MPI_Alltoall (sendbuf, msize, MPI_CHAR, recvbuf, msize, MPI_CHAR,
		    my_comm_h);
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
  EXT_MPI_Alltoall_init_general (sendbuf, msize, MPI_CHAR, recvbuf, msize,
				 MPI_CHAR, my_comm_v, MY_CORES_PER_NODE_V,
				 my_comm_h, MY_CORES_PER_NODE_H, &handle_v);
  EXT_MPI_Alltoall_init_general (sendbuf, msize, MPI_CHAR, recvbuf, msize,
				 MPI_CHAR, my_comm_h, MY_CORES_PER_NODE_H,
				 my_comm_v, MY_CORES_PER_NODE_V, &handle_h);
  MPI_Barrier (MPI_COMM_WORLD);
  start = MPI_Wtime ();
  for (i = 0; i < 10000; i++)
    {
      EXT_MPI_Alltoall_exec (sendbuf, msize, MPI_CHAR, recvbuf, msize,
			     MPI_CHAR, my_comm_v, handle_v);
      EXT_MPI_Alltoall_exec (sendbuf, msize, MPI_CHAR, recvbuf, msize,
			     MPI_CHAR, my_comm_h, handle_h);
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
  EXT_MPI_Alltoall_done (handle_v);
  EXT_MPI_Alltoall_done (handle_h);

  // Finalize the MPI environment.
  MPI_Finalize ();
}
