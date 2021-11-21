#ifndef EXT_MPI_INTERFACE_H_

#define EXT_MPI_INTERFACE_H_

#ifdef __cplusplus
extern "C"
{
#endif

int MPI_Allreduce_init(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Info info, MPI_Request *request);

#ifdef __cplusplus
}
#endif

#endif
