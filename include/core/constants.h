#ifndef CONSTANTS_H_

#define CONSTANTS_H_

#define SEND_PTR_CPU 0x8000000000000000
#define RECV_PTR_CPU 0x9000000000000000
#define SHMEM_PTR_CPU 0xa000000000000000
#ifdef GPU_ENABLED
#define SEND_PTR_GPU 0xb000000000000000
#define RECV_PTR_GPU 0xc000000000000000
#define SHMEM_PTR_GPU 0xd000000000000000
#endif

#define OPCODE_RETURN 0
#define OPCODE_MEMCPY 1
#define OPCODE_MPIIRECV 2
#define OPCODE_MPIISEND 3
#define OPCODE_MPIWAITALL 4
#define OPCODE_SOCKETBARRIER 5
#define OPCODE_SOCKETBSMALL 6
#define OPCODE_NODEBARRIER 7
#define OPCODE_NODEBARRIER_ATOMIC_SET 8
#define OPCODE_NODEBARRIER_ATOMIC_WAIT 9
#define OPCODE_REDUCE 10
#define OPCODE_INVREDUCE 11
#define OPCODE_MPISENDRECV 12
#define OPCODE_MPISEND 13
#define OPCODE_MPIRECV 14
#define OPCODE_LOCALMEM 15
#define OPCODE_MPIWAITANY 16
#define OPCODE_ATTACHED 17
#define OPCODE_REDUCE_WAIT 18
#define OPCODE_MEMORY_FENCE 19
#define OPCODE_MEMORY_FENCE_STORE 20
#define OPCODE_MEMORY_FENCE_LOAD 21
#ifdef GPU_ENABLED
#define OPCODE_GPUSYNCHRONIZE 22
#define OPCODE_GPUKERNEL 23
#define OPCODE_GPUGEMV 24
#endif
#ifdef NCCL_ENABLED
#define OPCODE_START 25
#endif

#define OPCODE_REDUCE_SUM_CHAR 0
#define OPCODE_REDUCE_MIN_CHAR 1
#define OPCODE_REDUCE_MAX_CHAR 2
#define OPCODE_REDUCE_USER_CHAR 3
#define OPCODE_REDUCE_SUM_DOUBLE 4
#define OPCODE_REDUCE_MIN_DOUBLE 5
#define OPCODE_REDUCE_MAX_DOUBLE 6
#define OPCODE_REDUCE_USER_DOUBLE 7
#define OPCODE_REDUCE_SUM_LONG_INT 8
#define OPCODE_REDUCE_MIN_LONG_INT 9
#define OPCODE_REDUCE_MAX_LONG_INT 10
#define OPCODE_REDUCE_USER_LONG_INT 11
#define OPCODE_REDUCE_SUM_FLOAT 12
#define OPCODE_REDUCE_MIN_FLOAT 13
#define OPCODE_REDUCE_MAX_FLOAT 14
#define OPCODE_REDUCE_USER_FLOAT 15
#define OPCODE_REDUCE_SUM_INT 16
#define OPCODE_REDUCE_MIN_INT 17
#define OPCODE_REDUCE_MAX_INT 18
#define OPCODE_REDUCE_USER_INT 19

#define ERROR_MALLOC -1
#define ERROR_SHMEM -2
#define ERROR_SYNTAX -3

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void code_put_char(char **code, char c, int isdryrun) {
  if (!isdryrun)
    *((char *)(*code)) = c;
  *code += sizeof(char);
}

static void code_put_int(char **code, int i, int isdryrun) {
  if (!isdryrun)
    *((int *)(*code)) = i;
  *code += sizeof(int);
}

static void code_put_pointer(char **code, void *p, int isdryrun) {
  if (!isdryrun)
    *((void **)(*code)) = p;
  *code += sizeof(void *);
}

static char code_get_char(char **code) {
  char c;
  c = *((char *)(*code));
  *code += sizeof(char);
  return c;
}

static int code_get_int(char **code) {
  int i;
  i = *((int *)(*code));
  *code += sizeof(int);
  return i;
}

static void *code_get_pointer(char **code) {
  void *p;
  p = *((void **)(*code));
  *code += sizeof(void *);
  return p;
}

static int get_type_size(int reduction_op) {
  switch (reduction_op) {
    case OPCODE_REDUCE_SUM_DOUBLE:
    case OPCODE_REDUCE_MIN_DOUBLE:
    case OPCODE_REDUCE_MAX_DOUBLE:
    case OPCODE_REDUCE_USER_DOUBLE:
      return sizeof(double);
    case OPCODE_REDUCE_SUM_LONG_INT:
    case OPCODE_REDUCE_MIN_LONG_INT:
    case OPCODE_REDUCE_MAX_LONG_INT:
    case OPCODE_REDUCE_USER_LONG_INT:
      return sizeof(long int);
    case OPCODE_REDUCE_SUM_FLOAT:
    case OPCODE_REDUCE_MIN_FLOAT:
    case OPCODE_REDUCE_MAX_FLOAT:
    case OPCODE_REDUCE_USER_FLOAT:
      return sizeof(float);
    case OPCODE_REDUCE_SUM_INT:
    case OPCODE_REDUCE_MIN_INT:
    case OPCODE_REDUCE_MAX_INT:
    case OPCODE_REDUCE_USER_INT:
      return sizeof(int);
    default:
      return 1;
  }
}
#pragma GCC diagnostic pop
#endif
