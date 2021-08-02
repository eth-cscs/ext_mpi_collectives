#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>

#define CORES_PER_NODE 12

#define NUM_BARRIERS 4

static int shmemid = -1;
static char volatile *shmem = NULL;

static int barrier_count = 0;

static void node_barrier(int num_cores) {
  __sync_fetch_and_add(shmem + barrier_count, 1);
  while (shmem[barrier_count] != num_cores) {
    ;
  }
  shmem[(barrier_count + NUM_BARRIERS - 1) % NUM_BARRIERS] = 0;
  barrier_count = (barrier_count + 1) % NUM_BARRIERS;
}

static int setup_shared_memory(MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int size_shared,
                               int *shmemid, char volatile **shmem, char fill,
                               int numfill) {
  MPI_Comm my_comm_node_h, my_comm_node_v;
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column, my_mpi_size_column;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  MPI_Comm_split(comm_row, my_mpi_rank_row / my_cores_per_node_row,
                 my_mpi_rank_row % my_cores_per_node_row, &my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                   my_mpi_rank_column % my_cores_per_node_column,
                   &my_comm_node_v);
  }
  if ((*shmem) != NULL) {
    MPI_Comm_free(&my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Comm_free(&my_comm_node_v);
    }
    return 1;
  }
  if ((my_mpi_rank_row % my_cores_per_node_row == 0) &&
      (my_mpi_rank_column % my_cores_per_node_column == 0)) {
    (*shmemid) = shmget(IPC_PRIVATE, size_shared, IPC_CREAT | 0666);
  }
  MPI_Bcast(shmemid, 1, MPI_INT, 0, my_comm_node_h);
  MPI_Barrier(my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Bcast(shmemid, 1, MPI_INT, 0, my_comm_node_v);
    MPI_Barrier(my_comm_node_v);
  }
  (*shmem) = (char *)shmat(*shmemid, NULL, 0);
  if ((*shmem) == NULL)
    exit(2);
  MPI_Barrier(my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Barrier(my_comm_node_v);
    MPI_Barrier(my_comm_node_h);
  }
  if (!((my_mpi_rank_row % my_cores_per_node_row == 0) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0))) {
    (*shmemid) = -1;
  } else {
    memset((void *)*shmem, 77, size_shared);
    memset((void *)*shmem, fill, numfill);
  }
  MPI_Barrier(my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Barrier(my_comm_node_v);
    MPI_Barrier(my_comm_node_h);
  }
  MPI_Comm_free(&my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_free(&my_comm_node_v);
  }
  return 0;
}

int main(int argc, char **argv) {
  char *sendbuf, *recvbuf, *tempbuf;
  int mpi_size, mpi_rank;
  int dest, source, sendcount, recvcount, sendcount_max, cores, parallel,
      iterations, i, j, k, start, step_size, sendcount_array_max,
      sendcount_array[100000];
  double wtime, wtime_sum;
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);

  // Get the number of processes
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

  if (mpi_size % CORES_PER_NODE != 0) {
    printf("wrong number of MPI ranks\n");
    exit(1);
  }

  // Get the rank of the process
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

  start = 50;
  iterations = 100;
  sendcount_array_max = 0;
  for (i = 100; i < 10000; i += 100) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 10000; i < 100000; i += 10000) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 100000; i < 1000000; i += 100000) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 1000000; i < 10000000; i += 1000000) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 10000000; i < 100000000; i += 10000000) {
    //        sendcount_array[sendcount_array_max++] = i;
  }

  setup_shared_memory(MPI_COMM_WORLD, CORES_PER_NODE, MPI_COMM_NULL, 1,
                      sendcount_array[sendcount_array_max - 1], &shmemid,
                      &shmem, 0, NUM_BARRIERS);

  sendbuf = (char *)(shmem + NUM_BARRIERS);
  //    sendbuf = malloc(sendcount_max);
  recvbuf = (char *)malloc(sendcount_array[sendcount_array_max - 1]);
  tempbuf =
      (char *)malloc(sendcount_array[sendcount_array_max - 1] * CORES_PER_NODE);
  parallel = 1;
  for (cores = 1; cores <= CORES_PER_NODE && cores < mpi_size / CORES_PER_NODE;
       cores++) {
    for (k = 0; k < sendcount_array_max; k++) {
      sendcount = sendcount_array[k];
      recvcount = sendcount;
      wtime_sum = 0e0;
      step_size = 1;
      for (i = 0; i < start; i++) {
        if (mpi_rank % CORES_PER_NODE >= cores) {
          dest = -1;
        } else {
          dest = ((mpi_rank / CORES_PER_NODE +
                   step_size * (mpi_rank % CORES_PER_NODE + 1)) %
                  (mpi_size / CORES_PER_NODE)) *
                     CORES_PER_NODE +
                 mpi_rank % CORES_PER_NODE;
          source = ((mpi_rank / CORES_PER_NODE + mpi_size -
                     step_size * (mpi_rank % CORES_PER_NODE + 1)) %
                    (mpi_size / CORES_PER_NODE)) *
                       CORES_PER_NODE +
                   mpi_rank % CORES_PER_NODE;
        }
        node_barrier(CORES_PER_NODE);
        if (dest >= 0) {
          MPI_Sendrecv(sendbuf, sendcount, MPI_CHAR, dest, 0, recvbuf,
                       recvcount, MPI_CHAR, source, 0, MPI_COMM_WORLD,
                       MPI_STATUS_IGNORE);
        }
        node_barrier(CORES_PER_NODE);
        for (j = 0; j < 1; j++) {
          //                    memcpy(tempbuf+j*sendcount_max, recvbuf,
          //                    sendcount_max);
        }
        step_size *= (cores + 1);
        if (step_size >= mpi_size / CORES_PER_NODE) {
          step_size = 1;
        }
      }
      for (i = 0; i < iterations; i++) {
        if (mpi_rank % CORES_PER_NODE >= cores) {
          dest = -1;
        } else {
          dest = ((mpi_rank / CORES_PER_NODE +
                   step_size * (mpi_rank % CORES_PER_NODE + 1)) %
                  (mpi_size / CORES_PER_NODE)) *
                     CORES_PER_NODE +
                 mpi_rank % CORES_PER_NODE;
          source = ((mpi_rank / CORES_PER_NODE + mpi_size -
                     step_size * (mpi_rank % CORES_PER_NODE + 1)) %
                    (mpi_size / CORES_PER_NODE)) *
                       CORES_PER_NODE +
                   mpi_rank % CORES_PER_NODE;
        }
        node_barrier(CORES_PER_NODE);
        wtime = MPI_Wtime();
        if (dest >= 0) {
          MPI_Sendrecv(sendbuf, sendcount, MPI_CHAR, dest, 0, recvbuf,
                       recvcount, MPI_CHAR, source, 0, MPI_COMM_WORLD,
                       MPI_STATUS_IGNORE);
        }
        node_barrier(CORES_PER_NODE);
        wtime = MPI_Wtime() - wtime;
        for (j = 0; j < 1; j++) {
          //                    memcpy(tempbuf+j*sendcount_max, recvbuf,
          //                    sendcount_max);
        }
        step_size *= (cores + 1);
        if (step_size >= mpi_size / CORES_PER_NODE) {
          step_size = 1;
        }
        wtime_sum += wtime;
      }
      if (mpi_rank == 0) {
        MPI_Reduce(MPI_IN_PLACE, &wtime_sum, 1, MPI_DOUBLE, MPI_MAX, 0,
                   MPI_COMM_WORLD);
        //                wtime_sum /= (mpi_size/CORES_PER_NODE*cores*parallel);
        wtime_sum /= iterations;
        printf("%d %d %d %d %e\n", mpi_size, cores, parallel, sendcount,
               wtime_sum);
      } else {
        MPI_Reduce(&wtime_sum, &wtime_sum, 1, MPI_DOUBLE, MPI_MAX, 0,
                   MPI_COMM_WORLD);
      }
      MPI_Barrier(MPI_COMM_WORLD);
    }
  }

  // Finalize the MPI environment.
  MPI_Finalize();
}
