#define BENCHMARK "OSU MPI%s Allreduce Latency Test"
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
#endif
#ifndef NUM_CORES
#define NUM_CORES 12
#endif
#ifdef EXT_NOPER
#include <mpi/ext_mpi.h>
#ifndef NUM_CORES
#define NUM_CORES 12
#endif
#endif

int main(int argc, char *argv[])
{
    int i, numprocs, rank, size, handle, flag;
    double latency_ref=0.0;
    double latency = 0.0, t_start = 0.0, t_stop = 0.0;
    double timer_ref=0.0;
    double timer=0.0;
    double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
    double *sendbuf, *recvbuf;
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
    set_benchmark_name("osu_allreduce");
    options.count_down = 100000;
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

    if (options.max_message_size > options.max_mem_limit) {
        if (rank == 0) {
            fprintf(stderr, "Warning! Increase the Max Memory Limit to be able to run up to %ld bytes.\n"
                            "Continuing with max message size of %ld bytes\n", 
                            options.max_message_size, options.max_mem_limit);
        }
        options.max_message_size = options.max_mem_limit;
    }

    options.min_message_size /= sizeof(double);
    if (options.min_message_size < MIN_MESSAGE_SIZE) {
        options.min_message_size = MIN_MESSAGE_SIZE;
    }

    bufsize = sizeof(double)*(options.max_message_size/sizeof(double));
    if (allocate_memory_coll((void**)&sendbuf, bufsize, options.accel)) {
        fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
        MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
    }
    set_buffer(sendbuf, options.accel, 1, bufsize);

    bufsize = sizeof(double)*(options.max_message_size/sizeof(double));
    if (allocate_memory_coll((void**)&recvbuf, bufsize, options.accel)) {
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
    EXT_MPI_Allocate(MPI_COMM_WORLD, NUM_CORES, MPI_COMM_NULL, 1, 1000000000, 10000000);
#endif

    for(num_tasks=numprocs; num_tasks > options.count_down; num_tasks -= NUM_CORES) {
      if (rank<num_tasks){
        MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &new_comm);
    for(size=options.min_message_size; size*sizeof(double) <= options.max_message_size; size *= 2) {

        if(size > LARGE_MESSAGE_SIZE) {
            options.skip = options.skip_large;
            options.iterations = options.iterations_large;
        }

#ifdef EXT_MPI
        EXT_MPI_Reduce_init_general (sendbuf, recvbuf, size, MPI_DOUBLE,
                                  MPI_SUM, 0, new_comm, NUM_CORES, MPI_COMM_NULL,
                                  1, &handle);
#endif
#ifdef PERSISTENT
        MPIX_Allreduce_init(sendbuf, recvbuf, size, MPI_DOUBLE, MPI_SUM, new_comm, MPI_INFO_NULL, &req);
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
                MPI_CHECK(MPI_Reduce(sendbuf, recvbuf, size, MPI_DOUBLE, MPI_SUM, 0, new_comm));
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
                EXT_MPI_Allreduce (sendbuf, recvbuf, size, MPI_DOUBLE, MPI_SUM, handle);
#else
                MPI_CHECK(MPI_Reduce(sendbuf, recvbuf, size, MPI_DOUBLE, MPI_SUM, 0, new_comm));
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
          printf("%ld ", size*sizeof(double));
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
//        print_stats(rank, size * sizeof(double), avg_time, min_time, max_time);
        MPI_CHECK(MPI_Barrier(new_comm));
    }
    }else{
      MPI_Comm_split(MPI_COMM_WORLD, 1, rank-num_tasks, &new_comm);
    }
      MPI_Comm_free(&new_comm);
    }

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
