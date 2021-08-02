program run_alltoall_bruck

  implicit none

  include 'mpif.h'

  integer :: my_mpi_size, my_mpi_rank, handle, i, world_size, world_rank, ierr, handle2
  integer, allocatable :: sendbuf(:), recvbuf(:)
! Initialize the MPI environment
  call MPI_Init(ierr)

! Get the number of processes
  call MPI_Comm_size(MPI_COMM_WORLD, world_size, ierr)

! Get the rank of the process
  call MPI_Comm_rank(MPI_COMM_WORLD, world_rank, ierr)

  allocate(sendbuf(world_size))
  allocate(recvbuf(world_size))

  do i = 1, world_size
      sendbuf(i) = world_rank + (i-1) * world_size
      recvbuf(i) = -11
  end do

  call MY_Alltoall_init(sendbuf, 1, MPI_INT, recvbuf, 1, MPI_INT, MPI_COMM_WORLD, 12, MPI_COMM_NULL, 1, handle);
 
  call MY_Alltoall_nonblocking(handle, handle2);

  call MY_Alltoall(handle, ierr);
  call MY_Alltoall(handle2, ierr);

  write (*, fmt="(1x,a,i0)", advance="no") "aaaaaaaaa ", world_rank
  do i = 1, world_size
      write (*, fmt="(1x,i0)", advance="no") recvbuf(i)
  end do
  write (*,*)

  call MY_Alltoall_done (handle, ierr);
  call MY_Alltoall_done (handle2, ierr);
  call MPI_Finalize (ierr);

end program run_alltoall_bruck
