#ifndef CONSTANTS_H_

#define CONSTANTS_H_

#ifndef GPU_ENABLED
#define SEND_PTR_CPU 0x8000000000000000
#define RECV_PTR_CPU 0x9000000000000000
#else
#define SEND_PTR_GPU 0x8000000000000000
#define SEND_PTR_CPU 0x9000000000000000
#define RECV_PTR_GPU 0xa000000000000000
#define RECV_PTR_CPU 0xb000000000000000
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
#define OPCODE_REDUCE_SUM_DOUBLE 1
#define OPCODE_REDUCE_SUM_LONG_INT 2
#define OPCODE_REDUCE_SUM_FLOAT 3
#define OPCODE_REDUCE_SUM_INT 4
#define OPCODE_REDUCE_USER_CHAR 5
#define OPCODE_REDUCE_USER_DOUBLE 6
#define OPCODE_REDUCE_USER_LONG_INT 7
#define OPCODE_REDUCE_USER_FLOAT 8
#define OPCODE_REDUCE_USER_INT 9

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
#pragma GCC diagnostic pop
#endif
