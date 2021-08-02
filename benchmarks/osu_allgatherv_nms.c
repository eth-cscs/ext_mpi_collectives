#define BENCHMARK "OSU MPI%s Allgatherv Latency Test"
/*
 * Copyright (C) 2002-2018 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */
#include "osu_util.h"
#ifdef PERSISTENT
#include <mpi-ext.h>
#endif

#ifdef EXT_MPI
#include <mpi/ext_mpi.h>
#ifndef NUM_CORES
#define NUM_CORES 12
#endif
#endif
#ifdef EXT_NOPER
#include <mpi/ext_mpi.h>
#ifndef NUM_CORES
#define NUM_CORES 12
#endif
#endif

int main(int argc, char *argv[])
{
    int i, numprocs, rank, size, disp, handle, flag;
    double latency = 0.0, t_start = 0.0, t_stop = 0.0;
    double timer=0.0;
    double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
    double latency_ref=0.0;
    double timer_ref=0.0;
    char *sendbuf, *recvbuf;
    int *rdispls=NULL, *recvcounts=NULL;
    int po_ret;
    size_t bufsize;
    MPI_Comm new_comm;
    int num_tasks;
#ifdef PERSISTENT
    MPI_Request req;
#endif
    options.bench = COLLECTIVE;
    options.subtype = LAT;

    set_header(HEADER);
    set_benchmark_name("osu_allgather");
    po_ret = process_options(argc, argv);

    if (PO_OKAY == po_ret && NONE != options.accel) {
        if (init_accel()) {
            fprintf(stderr, "Error initializing device\n");
            exit(EXIT_FAILURE);
        }
    }

    MPI_CHECK(MPI_Init(&argc, &argv));
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &numprocs));

    switch (po_ret) {
        case PO_BAD_USAGE:
            print_bad_usage_message(rank);
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_FAILURE);
        case PO_HELP_MESSAGE:
            print_help_message(rank);
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_SUCCESS);
        case PO_VERSION_MESSAGE:
            print_version_message(rank);
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_SUCCESS);
        case PO_OKAY:
            break;
    }

    if(numprocs < 2) {
        if (rank == 0) {
            fprintf(stderr, "This test requires at least two processes\n");
        }

        MPI_CHECK(MPI_Finalize());
        exit(EXIT_FAILURE);
    }

    options.max_message_size=90464;
    if ((options.max_message_size * numprocs) > options.max_mem_limit) {
        options.max_message_size = options.max_mem_limit / numprocs;
    }

    if (allocate_memory_coll((void**)&recvcounts, numprocs*sizeof(int), NONE)) {
        fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
        MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
    }
    if (allocate_memory_coll((void**)&rdispls, numprocs*sizeof(int), NONE)) {
        fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
        MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
    }

    if (allocate_memory_coll((void**)&sendbuf, options.max_message_size, options.accel)) {
        fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
        MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
    }
    set_buffer(sendbuf, options.accel, 1, options.max_message_size);

    bufsize = options.max_message_size * numprocs;
    if (allocate_memory_coll((void**)&recvbuf, bufsize,
                options.accel)) {
        fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
        MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
    }
    set_buffer(recvbuf, options.accel, 0, bufsize);

    print_preamble(rank);

#ifdef EXT_MPI
    EXT_MPI_Init();
#endif
#ifdef EXT_NOPER
    EXT_MPI_Init();
    EXT_MPI_Allocate(MPI_COMM_WORLD, NUM_CORES, MPI_COMM_NULL, 1, 10000000, 100000);
#endif

    for(num_tasks=numprocs; num_tasks > options.count_down; num_tasks /=2) {
      if (rank<num_tasks){
        MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &new_comm);
        size=options.max_message_size;

        MPI_CHECK(MPI_Barrier(new_comm));

        disp = 0;
        for ( i = 0; i < numprocs; i++) {
            if (i>1){
              size=0;
            }
            recvcounts[i] = size;
            rdispls[i] = disp;
            disp += size;
        }
        size=recvcounts[rank];

#ifdef EXT_MPI
        EXT_MPI_Allgatherv_init_general (sendbuf, size, MPI_CHAR,
                                  recvbuf, recvcounts, rdispls, MPI_CHAR,
                                  new_comm, NUM_CORES, MPI_COMM_NULL,
                                  1, &handle);
#endif
#ifdef PERSISTENT
        MPIX_Allgatherv_init(sendbuf, size, MPI_CHAR, recvbuf, recvcounts, rdispls, MPI_CHAR, new_comm, MPI_INFO_NULL, &req);
#endif
#ifdef EXT_NOPER
        EXT_MPI_Init_general (new_comm, NUM_CORES, MPI_COMM_NULL, 1, &handle);
#endif

        MPI_CHECK(MPI_Barrier(new_comm));
        options.iterations = 1;
        flag = 1;
        while (flag){
            timer_ref=0.0;
            timer=0.0;
            for(i=0; i < options.iterations; i++) {
                t_start = MPI_Wtime();
                MPI_CHECK(MPI_Allgatherv(sendbuf, size, MPI_CHAR, recvbuf, recvcounts, rdispls, MPI_CHAR, new_comm));
                t_stop=MPI_Wtime();

                timer_ref+=t_stop-t_start;
                MPI_CHECK(MPI_Barrier(new_comm));

                t_start = MPI_Wtime();
#ifdef EXT_MPI
                EXT_MPI_Exec (handle);
                EXT_MPI_Wait (handle);
#else
#ifdef PERSISTENT
                MPI_Start(&req);
                MPI_Wait(&req, MPI_STATUS_IGNORE);
#else
#ifdef EXT_NOPER
                EXT_MPI_Allgatherv (sendbuf, size, MPI_CHAR, recvbuf, recvcounts, rdispls, MPI_CHAR, handle);
#else
                MPI_CHECK(MPI_Allgatherv(sendbuf, size, MPI_CHAR, recvbuf, recvcounts, rdispls, MPI_CHAR, new_comm));
#endif
#endif
#endif
                t_stop=MPI_Wtime();

                timer+=t_stop-t_start;
                MPI_CHECK(MPI_Barrier(new_comm));
            }
            flag = (timer_ref < 2e0) && (timer < 2e0);
            MPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MIN, new_comm);
            MPI_CHECK(MPI_Barrier(new_comm));
            options.iterations *= 2;
        }
        options.iterations /= 2;
        latency_ref = (double)(timer_ref * 1e6) / options.iterations;
        latency = (double)(timer * 1e6) / options.iterations;

#ifdef EXT_MPI
        EXT_MPI_Done (handle);
#endif
#ifdef PERSISTENT
        MPI_Request_free(&req);
#endif
#ifdef EXT_NOPER
        EXT_MPI_Done_general (handle);
#endif

        MPI_CHECK(MPI_Barrier(new_comm));

        MPI_CHECK(MPI_Reduce(&latency_ref, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0,
                new_comm));
        MPI_CHECK(MPI_Reduce(&latency_ref, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0,
                new_comm));
        MPI_CHECK(MPI_Reduce(&latency_ref, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                new_comm));
        avg_time = avg_time/num_tasks;

        if (rank == 0){
          printf("# iterations %d\n", options.iterations);
          printf("%d ", num_tasks/NUM_CORES);
          printf("%d ", size);
          printf("%e %e %e ", avg_time, min_time, max_time);
        }

        MPI_CHECK(MPI_Reduce(&latency, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0,
                new_comm));
        MPI_CHECK(MPI_Reduce(&latency, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0,
                new_comm));
        MPI_CHECK(MPI_Reduce(&latency, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                new_comm));
        avg_time = avg_time/num_tasks;

        if (rank == 0){
          printf("%e %e %e\n", avg_time, min_time, max_time);
        }
        MPI_CHECK(MPI_Barrier(new_comm));
    }else{
      MPI_Comm_split(MPI_COMM_WORLD, 1, rank-num_tasks, &new_comm);
    }
      MPI_Comm_free(&new_comm);
    }

    free_buffer(rdispls, NONE);
    free_buffer(recvcounts, NONE);
    free_buffer(sendbuf, options.accel);
    free_buffer(recvbuf, options.accel);

    MPI_CHECK(MPI_Finalize());

    if (NONE != options.accel) {
        if (cleanup_accel()) {
            fprintf(stderr, "Error cleaning up device\n");
            exit(EXIT_FAILURE);
        }
    }

    return EXIT_SUCCESS;
}
/* vi: set sw=4 sts=4 tw=80: */
