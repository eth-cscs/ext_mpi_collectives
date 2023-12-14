#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ext_mpi.h"
#include "constants.h"
#include "debug_persistent.h"

int ext_mpi_allgatherv_init_debug(const void *sendbuf, int sendcount,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  const int *recvcounts, const int *displs,
                                  MPI_Datatype recvtype, MPI_Comm comm,
                                  MPI_Info info, int *handle) {
  int comm_size_row, type_size, i;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  int world_rank, comm_rank_row, max_sendcount, max_displs, j, k;
#ifdef GPU_ENABLED
  void *sendbuf_hh = NULL, *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL;
#endif
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm, &comm_rank_row);
  max_sendcount = recvcounts[0];
  max_displs = 0;
  for (i = 0; i < comm_size_row; i++) {
    if (recvcounts[i] > max_sendcount)
      max_sendcount = recvcounts[i];
    if (displs[i] > max_displs)
      max_displs = displs[i];
  }
  j = max_displs + max_sendcount;
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    sendbuf_hh = (void *)malloc(sendcount * type_size);
    if (!sendbuf_hh)
      goto error;
    recvbuf_hh = (long int *)malloc(j * type_size);
    if (!recvbuf_hh)
      goto error;
    recvbuf_ref_hh = (void *)malloc(j * type_size);
    if (!recvbuf_ref_hh)
      goto error;
    ext_mpi_gpu_malloc(&recvbuf_ref, j * type_size);
    if (!recvbuf_ref)
      goto error;
    ext_mpi_gpu_malloc(&sendbuf_h, sendcount * type_size);
    if (!sendbuf_h)
      goto error;
    for (i = 0; i < (int)((sendcount * type_size) / (int)sizeof(long int)); i++) {
      ((long int *)sendbuf_hh)[i] = world_rank * max_sendcount + i;
    }
//    if ((sendcount * type_size) % (int)sizeof(long int)){
//      ((int *)sendbuf_hh)[sendcount-1] = world_rank * max_sendcount + max_sendcount - 1;
//    }
    for (i = 0; i < (int)((j * type_size) / (int)sizeof(long int)); i++) {
      ((long int *)recvbuf_hh)[i] = -1;
    }
    ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, sendcount * type_size);
    ext_mpi_gpu_memcpy_hd(recvbuf_ref, recvbuf_hh, j * type_size);
    ext_mpi_gpu_memcpy_hd(recvbuf, recvbuf_hh, j * type_size);
  } else {
#endif
    recvbuf_ref = (void *)malloc(j * type_size);
    if (!recvbuf_ref)
      goto error;
    sendbuf_h = (void *)malloc(sendcount * type_size);
    if (!sendbuf_h)
      goto error;
    for (i = 0; i < (int)((sendcount * type_size) / (int)sizeof(long int)); i++) {
      ((long int *)sendbuf_h)[i] = world_rank * max_sendcount + i;
    }
#ifdef GPU_ENABLED
  }
#endif
  MPI_Allgatherv(sendbuf_h, sendcount, sendtype, recvbuf_ref, recvcounts, displs, recvtype, comm);
  if (ext_mpi_allgatherv_init_general(sendbuf_h, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, info, handle) < 0)
    goto error;
  if (EXT_MPI_Start(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait(*handle) < 0)
    goto error;
  if (EXT_MPI_Done(*handle) < 0)
    goto error;
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, j * type_size);
    ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, j * type_size);
    k = 0;
    for (j = 0; j < comm_size_row; j++) {
      for (i = 0; i < (int)((recvcounts[j] * type_size) / sizeof(long int));
           i++) {
        if (((long int *)
                 recvbuf_hh)[(displs[j] * type_size) / sizeof(long int) + i] !=
            ((long int *)
                 recvbuf_ref_hh)[(displs[j] * type_size) / sizeof(long int) + i]) {
          k = 1;
        }
      }
//      if ((int)((recvcounts[j] * type_size) % sizeof(long int)) {
//      }
    }
  } else {
#endif
    k = 0;
    for (j = 0; j < comm_size_row; j++) {
      for (i = 0; i < (int)((recvcounts[j] * type_size) / sizeof(long int));
           i++) {
        if (((long int *)
                 recvbuf)[(displs[j] * type_size) / sizeof(long int) + i] !=
            ((long int *)
                 recvbuf_ref)[(displs[j] * type_size) / sizeof(long int) + i]) {
          k = 1;
        }
      }
//      if ((int)((recvcounts[j] * type_size) % sizeof(long int)) {
//      }
    }
#ifdef GPU_ENABLED
  }
#endif
  if (k) {
    printf("logical error in EXT_MPI_Allgatherv %d\n", world_rank);
    exit(1);
  }
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    ext_mpi_gpu_free(sendbuf_h);
    ext_mpi_gpu_free(recvbuf_ref);
    free(recvbuf_ref_hh);
    free(recvbuf_hh);
    free(sendbuf_hh);
  } else {
#endif
    free(sendbuf_h);
    free(recvbuf_ref);
#ifdef GPU_ENABLED
  }
#endif
  return 0;
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
}

int ext_mpi_gatherv_init_debug(const void *sendbuf, int sendcount,
                               MPI_Datatype sendtype, void *recvbuf,
                               const int *recvcounts, const int *displs,
                               MPI_Datatype recvtype, int root,
                               MPI_Comm comm,
                               MPI_Info info, int *handle) {
  int comm_size_row, type_size, world_rank, comm_rank_row, max_sendcount, max_displs, i, j, k;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm, &comm_rank_row);
  max_sendcount = recvcounts[0];
  max_displs = 0;
  for (i = 0; i < comm_size_row; i++) {
    if (recvcounts[i] > max_sendcount)
      max_sendcount = recvcounts[i];
    if (displs[i] > max_displs)
      max_displs = displs[i];
  }
  j = max_displs + max_sendcount;
  recvbuf_ref = (void *)malloc(j * type_size);
  if (!recvbuf_ref)
    goto error;
  sendbuf_h = (void *)malloc(sendcount * type_size);
  if (!sendbuf_h)
    goto error;
  for (i = 0; i < (int)((sendcount * type_size) / (int)sizeof(long int)); i++) {
    ((long int *)sendbuf_h)[i] = world_rank * max_sendcount + i;
  }
  MPI_Gatherv(sendbuf_h, sendcount, sendtype, recvbuf_ref, recvcounts, displs, recvtype, root, comm);
  if (ext_mpi_gatherv_init_general(sendbuf_h, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm, info, handle) < 0)
    goto error;
  if (EXT_MPI_Start(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait(*handle) < 0)
    goto error;
  if (EXT_MPI_Done(*handle) < 0)
    goto error;
  if (root == comm_rank_row) {
    k = 0;
    for (j = 0; j < comm_size_row; j++) {
      for (i = 0; i < (int)((recvcounts[j] * type_size) / (int)sizeof(long int));
           i++) {
        if (((long int *)
                 recvbuf)[(displs[j] * type_size) / (int)sizeof(long int) + i] !=
            ((long int *)
                 recvbuf_ref)[(displs[j] * type_size) / (int)sizeof(long int) + i]) {
          k = 1;
        }
      }
    }
    if (k) {
      printf("logical error in EXT_MPI_Allgatherv %d\n", world_rank);
      exit(1);
    }
  }
  free(sendbuf_h);
  free(recvbuf_ref);
  return 0;
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
}

int ext_mpi_reduce_scatter_init_debug(
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm, MPI_Info info, int *handle) {
  int comm_size_row, type_size, i, j;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  int world_rank, comm_rank_row, tsize;
#ifdef GPU_ENABLED
  void *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL, *sendbuf_hh = NULL;
#endif
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm, &comm_rank_row);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    tsize = 0;
    for (i = 0; i < comm_size_row; i++) {
      tsize += recvcounts[i];
    }
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      recvbuf_ref_hh = malloc(recvcounts[comm_rank_row] * type_size);
      if (!recvbuf_ref_hh)
        goto error;
      sendbuf_hh = malloc(tsize * type_size);
      if (!sendbuf_hh)
        goto error;
      for (i = 0; i < (tsize * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_hh)[i] = world_rank * tsize + i;
      }
      if ((tsize * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_hh)[tsize-1] = world_rank * tsize + tsize - 1;
      }
      ext_mpi_gpu_malloc(&recvbuf_ref, recvcounts[comm_rank_row] * type_size);
      ext_mpi_gpu_malloc(&sendbuf_h, tsize * type_size);
      ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, tsize * type_size);
    } else {
#endif
      recvbuf_ref = malloc(recvcounts[comm_rank_row] * type_size);
      if (!recvbuf_ref)
        goto error;
      sendbuf_h = malloc(tsize * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (tsize * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rank * tsize + i;
      }
      if ((tsize * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_h)[tsize-1] = world_rank * tsize + tsize - 1;
      }
#ifdef GPU_ENABLED
    }
#endif
    if (type_size == sizeof(long int)){
      MPI_Reduce_scatter(sendbuf_h, recvbuf_ref, recvcounts, MPI_LONG, op, comm);
      if (ext_mpi_reduce_scatter_init_general(sendbuf_h, recvbuf, recvcounts, MPI_LONG, op, comm, info, handle) < 0)
        goto error;
    }else{
      MPI_Reduce_scatter(sendbuf_h, recvbuf_ref, recvcounts, MPI_INT, op, comm);
      if (ext_mpi_reduce_scatter_init_general(sendbuf_h, recvbuf, recvcounts, MPI_INT, op, comm, info, handle) < 0)
        goto error;
    }
    if (EXT_MPI_Start(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait(*handle) < 0)
      goto error;
    if (EXT_MPI_Done(*handle) < 0)
      goto error;
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      recvbuf_hh = malloc(recvcounts[comm_rank_row] * type_size);
      if (!recvbuf_hh)
        goto error;
      ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, recvcounts[comm_rank_row] * type_size);
      ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, recvcounts[comm_rank_row] * type_size);
      j = 0;
      for (i = 0; i < (recvcounts[comm_rank_row] * type_size) / (int)sizeof(long int); i++) {
        if (((long int *)recvbuf_hh)[i] != ((long int *)recvbuf_ref_hh)[i]) {
          j = 1;
        }
      }
      if ((recvcounts[comm_rank_row] * type_size) % (int)sizeof(long int)){
        if (((int *)recvbuf_hh)[recvcounts[comm_rank_row]-1] != ((int *)recvbuf_ref_hh)[recvcounts[comm_rank_row]-1]) {
          j = 1;
        }
      }
      free(sendbuf_hh);
      free(recvbuf_hh);
      free(recvbuf_ref_hh);
      ext_mpi_gpu_free(sendbuf_h);
      ext_mpi_gpu_free(recvbuf_ref);
    } else {
#endif
      j = 0;
      for (i = 0; i < (recvcounts[comm_rank_row] * type_size) / (int)sizeof(long int); i++) {
        if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
          j = 1;
        }
      }
      if ((recvcounts[comm_rank_row] * type_size) % (int)sizeof(long int)){
        if (((int *)recvbuf)[recvcounts[comm_rank_row]-1] != ((int *)recvbuf_ref)[recvcounts[comm_rank_row]-1]) {
          j = 1;
        }
      }
      free(sendbuf_h);
      free(recvbuf_ref);
#ifdef GPU_ENABLED
    }
#endif
    if (j) {
      printf("logical error in EXT_MPI_Reduce_scatter %d\n", world_rank);
      exit(1);
    }
  }
  return 0;
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
}

int ext_mpi_scatterv_init_debug(const void *sendbuf, const int *sendcounts, const int *displs,
                                MPI_Datatype sendtype, void *recvbuf,
                                int recvcount, MPI_Datatype recvtype,
                                int root, MPI_Comm comm,
                                MPI_Info info, int *handle) {
  int comm_size_row, type_size, world_rank, comm_rank_row, i, j, k;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_rank(comm, &comm_rank_row);
  recvbuf_ref = (long int *)malloc(recvcount * type_size);
  if (!recvbuf_ref)
    goto error;
  sendbuf_h = malloc(
      (displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) * type_size);
  if (!sendbuf_h)
    goto error;
  k = 0;
  for (i = 0; i < comm_size_row; i++) {
    for (j = 0; j < (sendcounts[i] * type_size) / (int)sizeof(long int); j++) {
      ((long int *)((char *)sendbuf_h + displs[i] * type_size))[j] =
          world_rank *
              (((displs[comm_size_row - 1] + sendcounts[comm_size_row - 1]) *
                type_size) /
               (int)sizeof(long int)) +
          k++;
    }
  }
  MPI_Scatterv(sendbuf_h, sendcounts, displs, MPI_LONG, recvbuf_ref, recvcount, MPI_LONG, root, comm);
  if (ext_mpi_scatterv_init_general(sendbuf_h, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm, info, handle) < 0)
    goto error;
  if (EXT_MPI_Start(*handle) < 0)
    goto error;
  if (EXT_MPI_Wait(*handle) < 0)
    goto error;
  if (EXT_MPI_Done(*handle) < 0)
    goto error;
  j = 0;
  for (i = 0; i < (recvcount * type_size) / (int)sizeof(long int); i++) {
    if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
      j = 1;
    }
  }
  if (j) {
    printf("logical error in EXT_MPI_Scatterv %d\n", world_rank);
    exit(1);
  }
  free(sendbuf_h);
  free(recvbuf_ref);
  return 0;
error:
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
}

int ext_mpi_allreduce_init_debug(const void *sendbuf, void *recvbuf, int count,
                                 MPI_Datatype datatype, MPI_Op op,
                                 MPI_Comm comm,
                                 MPI_Info info, int *handle) {
  int comm_size_row, comm_rank_row;
  int type_size, i, j;
#ifdef GPU_ENABLED
  void *sendbuf_hh = NULL, *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL;
#endif
  int world_rankd;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    sendbuf_hh = (long int *)malloc(count * type_size);
    if (!sendbuf_hh)
      goto error;
    recvbuf_hh = (long int *)malloc(count * type_size);
    if (!recvbuf_hh)
      goto error;
    recvbuf_ref_hh = (long int *)malloc(count * type_size);
    if (!recvbuf_ref_hh)
      goto error;
    ext_mpi_gpu_malloc(&recvbuf_ref, count * type_size);
    if (!recvbuf_ref)
      goto error;
    if (sendbuf != MPI_IN_PLACE){
      ext_mpi_gpu_malloc(&sendbuf_h, count * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_hh)[i] = world_rankd * count + i;
        ((long int *)recvbuf_hh)[i] = -1;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_hh)[count-1] = world_rankd * count + count - 1;
      }
      ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, count * type_size);
      ext_mpi_gpu_memcpy_hd(recvbuf, recvbuf_hh, count * type_size);
    }else{
      sendbuf_h = MPI_IN_PLACE;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)recvbuf_hh)[i] = world_rankd * count + i;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)recvbuf_hh)[count-1] = world_rankd * count + count - 1;
      }
      ext_mpi_gpu_memcpy_hd(recvbuf, recvbuf_hh, count * type_size);
      ext_mpi_gpu_memcpy_hd(recvbuf_ref, recvbuf_hh, count * type_size);
    }
    if (type_size == sizeof(long int)){
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_LONG, op, comm_row);
      if (ext_mpi_allreduce_init_general(sendbuf_h, recvbuf, count, MPI_LONG, op, comm, info, handle)<0)
        goto error;
    }else{
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_INT, op, comm_row);
      if (etx_mpi_allreduce_init_general(sendbuf_h, recvbuf, count, MPI_INT, op, comm, info, handle)<0)
        goto error;
    }
    if (EXT_MPI_Start(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait(*handle) < 0)
      goto error;
    if (EXT_MPI_Done(*handle) < 0)
      goto error;
    ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, count * type_size);
    ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, count * type_size);
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      if (((long int *)recvbuf_hh)[i] != ((long int *)recvbuf_ref_hh)[i]) {
        j = 1;
      }
    }
    if ((count * type_size) % (int)sizeof(long int)){
      if (((int *)recvbuf_hh)[count-1] != ((int *)recvbuf_ref_hh)[count-1]){
        j = 1;
      }
    }
    if (sendbuf != MPI_IN_PLACE){
      ext_mpi_gpu_free(sendbuf_h);
    }
    free(recvbuf_ref_hh);
    free(recvbuf_hh);
    free(sendbuf_hh);
  } else {
#endif
    recvbuf_ref = (long int *)malloc(count * type_size);
    if (!recvbuf_ref)
      goto error;
    if (sendbuf != MPI_IN_PLACE){
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rankd * count + i;
        ((long int *)recvbuf)[i] = -1;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_h)[count-1] = world_rankd * count + count - 1;
      }
    }else{
      sendbuf_h = MPI_IN_PLACE;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)recvbuf)[i] = ((long int *)recvbuf_ref)[i] = world_rankd * count + i;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)recvbuf)[count-1] = ((int *)recvbuf_ref)[count-1] = world_rankd * count + count - 1;
      }
    }
    if (type_size == sizeof(long int)){
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_LONG, op, comm);
      if (ext_mpi_allreduce_init_general(sendbuf_h, recvbuf, count, MPI_LONG, op, comm, info, handle)<0)
        goto error;
    }else{
      PMPI_Allreduce(sendbuf_h, recvbuf_ref, count, MPI_INT, op, comm);
      if (ext_mpi_allreduce_init_general(sendbuf_h, recvbuf, count, MPI_INT, op, comm, info, handle)<0)
        goto error;
    }
    if (EXT_MPI_Start(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait(*handle) < 0)
      goto error;
    if (EXT_MPI_Done(*handle) < 0)
      goto error;
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
        j = 1;
      }
    }
    if ((count * type_size) % (int)sizeof(long int)){
      if (((int *)recvbuf)[count-1] != ((int *)recvbuf_ref)[count-1]){
        j = 1;
      }
    }
    if (sendbuf != MPI_IN_PLACE){
      free(sendbuf_h);
    }
    free(recvbuf_ref);
#ifdef GPU_ENABLED
  }
#endif
  if (j) {
    printf("logical error in EXT_MPI_Allreduce %d\n", world_rankd);
    exit(1);
  }
  return 0;
error:
#ifdef GPU_ENABLED
  free(recvbuf_ref_hh);
  free(recvbuf_hh);
  free(sendbuf_hh);
  if (sendbuf != MPI_IN_PLACE){
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      ext_mpi_gpu_free(sendbuf_h);
    }
    sendbuf_h = NULL;
  }
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    ext_mpi_gpu_free(recvbuf_ref);
    recvbuf_ref = NULL;
  }
#endif
  if (sendbuf != MPI_IN_PLACE){
    free(sendbuf_h);
  }
  free(recvbuf_ref);
  return ERROR_MALLOC;
}

int ext_mpi_reduce_init_debug(const void *sendbuf, void *recvbuf, int count,
                              MPI_Datatype datatype, MPI_Op op, int root,
                              MPI_Comm comm,
                              MPI_Info info, int *handle) {
  int comm_size_row, comm_rank_row, type_size, i, j;
#ifdef GPU_ENABLED
  void *recvbuf_hh = NULL, *recvbuf_ref_hh = NULL, *sendbuf_hh = NULL;
#endif
  int world_rankd;
  void *recvbuf_ref = NULL, *sendbuf_h = NULL;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(sendbuf)) {
      ext_mpi_gpu_malloc(&sendbuf_h, count * type_size);
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      ext_mpi_gpu_malloc(&recvbuf_ref, count * type_size);
      if (!recvbuf_ref)
        goto error;
      recvbuf_hh = (long int *)malloc(count * type_size);
      if (!recvbuf_hh)
        goto error;
      recvbuf_ref_hh = (long int *)malloc(count * type_size);
      if (!recvbuf_ref_hh)
        goto error;
      sendbuf_hh = (long int *)malloc(count * type_size);
      if (!recvbuf_ref_hh)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_hh)[i] = world_rankd * count + i;
      }
      ext_mpi_gpu_memcpy_hd(sendbuf_h, sendbuf_hh, count * type_size);
      if (ext_mpi_reduce_init_general(sendbuf, recvbuf, count, MPI_LONG, op, root, comm, info, handle) < 0)
        goto error;
      MPI_Reduce(sendbuf, recvbuf_ref, count, MPI_LONG, MPI_SUM, root, comm);
      EXT_MPI_Start(*handle);
      EXT_MPI_Wait(*handle);
      ext_mpi_gpu_memcpy_dh(recvbuf_hh, recvbuf, count * type_size);
      ext_mpi_gpu_memcpy_dh(recvbuf_ref_hh, recvbuf_ref, count * type_size);
      j = 0;
      if (root == comm_rank_row) {
        for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
          if (((long int *)recvbuf_hh)[i] != ((long int *)recvbuf_ref_hh)[i]) {
            j = 1;
          }
        }
      }
      free(recvbuf_ref_hh);
      free(recvbuf_hh);
      free(sendbuf_hh);
      ext_mpi_gpu_free(recvbuf_ref);
      ext_mpi_gpu_free(sendbuf_h);
    } else {
#endif
      recvbuf_ref = (long int *)malloc(count * type_size);
      if (!recvbuf_ref)
        goto error;
      sendbuf_h = (long int *)malloc(count * type_size);
      if (!sendbuf_h)
        goto error;
      for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
        ((long int *)sendbuf_h)[i] = world_rankd * count + i;
      }
      if ((count * type_size) % (int)sizeof(long int)){
        ((int *)sendbuf_h)[count-1] = world_rankd * count + count - 1;
      }
      if (type_size == sizeof(long int)){
        MPI_Reduce(sendbuf_h, recvbuf_ref, count, MPI_LONG, op, root, comm);
        if (ext_mpi_reduce_init_general(sendbuf_h, recvbuf, count, MPI_LONG, op, root, comm, info, handle) < 0)
          goto error;
      }else{
        MPI_Reduce(sendbuf_h, recvbuf_ref, count, MPI_INT, op, root, comm);
        if (ext_mpi_reduce_init_general(sendbuf_h, recvbuf, count, MPI_INT, op, root, comm, info, handle) < 0)
          goto error;
      }
      if (EXT_MPI_Start(*handle) < 0)
        goto error;
      if (EXT_MPI_Wait(*handle) < 0)
        goto error;
      if (EXT_MPI_Done(*handle) < 0)
        goto error;
      j = 0;
      if (root == comm_rank_row) {
        for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
          if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
            j = 1;
          }
        }
        if ((count * type_size) % (int)sizeof(long int)){
          if (((int *)recvbuf)[count-1] != ((int *)recvbuf_ref)[count-1]) {
            j = 1;
          }
        }
      }
      free(sendbuf_h);
      free(recvbuf_ref);
#ifdef GPU_ENABLED
    }
#endif
    if (j) {
      printf("logical error in EXT_MPI_Reduce %d\n", world_rankd);
      exit(1);
    }
  }
  return 0;
error:
#ifdef GPU_ENABLED
  free(recvbuf_ref_hh);
  free(recvbuf_hh);
  free(sendbuf_hh);
  if (ext_mpi_gpu_is_device_pointer(sendbuf)){
    ext_mpi_gpu_free(recvbuf_ref);
    recvbuf_ref = NULL;
    ext_mpi_gpu_free(sendbuf_h);
    sendbuf_h = NULL;
  }
#endif
  free(sendbuf_h);
  free(recvbuf_ref);
  return ERROR_MALLOC;
}

int ext_mpi_bcast_init_debug(void *buffer, int count, MPI_Datatype datatype,
                             int root, MPI_Comm comm,
                             MPI_Info info, int *handle) {
  int comm_size_row, comm_rank_row, type_size, i, j;
#ifdef GPU_ENABLED
  void *buffer_hh = NULL, *buffer_ref_hh = NULL;
#endif
  int world_rankd;
  void *buffer_ref = NULL, *buffer_org = NULL;
  MPI_Comm_size(comm, &comm_size_row);
  MPI_Comm_rank(comm, &comm_rank_row);
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(buffer)) {
    ext_mpi_gpu_malloc(&buffer_ref, count * type_size);
    if (!buffer_ref)
      goto error;
    buffer_hh = (long int *)malloc(count * type_size);
    if (!buffer_hh)
      goto error;
    buffer_ref_hh = (long int *)malloc(count * type_size);
    if (!buffer_ref_hh)
      goto error;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      ((long int *)buffer_hh)[i] = world_rankd * count + i + 73;
    }
    ext_mpi_gpu_memcpy_hd(buffer_ref, buffer_hh, count * type_size);
    MPI_Bcast(buffer_hh, count, MPI_LONG, root, comm_row);
    if (ext_mpi_bcast_init_general(buffer_ref, count, datatype, root, comm, info, handle) < 0)
    if (EXT_MPI_Start(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait(*handle) < 0)
      goto error;
    if (EXT_MPI_Done(*handle) < 0)
      goto error;
    ext_mpi_gpu_memcpy_dh(buffer_ref_hh, buffer_ref, count * type_size);
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      if (((long int *)buffer_ref_hh)[i] != ((long int *)buffer_hh)[i]) {
        j = 1;
      }
    }
    free(buffer_ref_hh);
    free(buffer_hh);
    ext_mpi_gpu_free(buffer_ref);
  } else {
#endif
    buffer_ref = (long int *)malloc(count * type_size);
    if (!buffer_ref)
      goto error;
    buffer_org = (long int *)malloc(count * type_size);
    if (!buffer_org)
      goto error;
    memcpy(buffer_org, buffer, count * type_size);
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      ((long int *)buffer_ref)[i] = ((long int *)buffer)[i] =
          world_rankd * count + i + 1073;
    }
    MPI_Bcast(buffer_ref, count, datatype, root, comm);
    if (ext_mpi_bcast_init_general(buffer, count, datatype, root, comm, info, handle) < 0)
      goto error;
    if (EXT_MPI_Start(*handle) < 0)
      goto error;
    if (EXT_MPI_Wait(*handle) < 0)
      goto error;
    if (EXT_MPI_Done(*handle) < 0)
      goto error;
    j = 0;
    for (i = 0; i < (count * type_size) / (int)sizeof(long int); i++) {
      if (((long int *)buffer)[i] != ((long int *)buffer_ref)[i]) {
        j = 1;
      }
    }
    memcpy(buffer, buffer_org, count * type_size);
    free(buffer_org);
    free(buffer_ref);
#ifdef GPU_ENABLED
  }
#endif
  if (j) {
    printf("logical error in EXT_MPI_Bcast %d\n", world_rankd);
    exit(1);
  }
  return 0;
error:
#ifdef GPU_ENABLED
  free(buffer_ref_hh);
  free(buffer_hh);
  if (ext_mpi_gpu_is_device_pointer(buffer)) {
    ext_mpi_gpu_free(buffer_ref);
    buffer_ref = NULL;
  }
#endif
  free(buffer_org);
  free(buffer_ref);
  return ERROR_MALLOC;
}
