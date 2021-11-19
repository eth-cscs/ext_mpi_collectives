#ifndef CONSTANTS_H_

#define CONSTANTS_H_

#define OPCODE_RETURN 0
#define OPCODE_MEMCPY 1
#define OPCODE_MPIIRECV 2
#define OPCODE_MPIISEND 3
#define OPCODE_MPIWAITALL 4
#define OPCODE_NODEBARRIER 5
#define OPCODE_SETNUMCORES 6
#define OPCODE_SETNODERANK 7
#define OPCODE_REDUCE 8
#define OPCODE_MPISENDRECV 9
#define OPCODE_MPISEND 10
#define OPCODE_MPIRECV 11
#define OPCODE_LOCALMEM 12
#ifdef GPU_ENABLED
#define OPCODE_GPUSYNCHRONIZE 13
#define OPCODE_GPUKERNEL 14
#endif

#define OPCODE_REDUCE_SUM_DOUBLE 0
#define OPCODE_REDUCE_SUM_LONG_INT 1
#define OPCODE_REDUCE_SUM_FLOAT 2
#define OPCODE_REDUCE_SUM_INT 3

#define ERROR_MALLOC -1
#define ERROR_SHMEM -2
#define ERROR_SYNTAX -3

inline void code_put_char(char **code, char c, int isdryrun) {
  if (!isdryrun)
    *((char *)(*code)) = c;
  *code += sizeof(char);
}

inline void code_put_int(char **code, int i, int isdryrun) {
  if (!isdryrun)
    *((int *)(*code)) = i;
  *code += sizeof(int);
}

inline void code_put_pointer(char **code, void *p, int isdryrun) {
  if (!isdryrun)
    *((void **)(*code)) = p;
  *code += sizeof(void *);
}

inline char code_get_char(char **code) {
  char c;
  c = *((char *)(*code));
  *code += sizeof(char);
  return c;
}

inline int code_get_int(char **code) {
  int i;
  i = *((int *)(*code));
  *code += sizeof(int);
  return i;
}

inline void *code_get_pointer(char **code) {
  void *p;
  p = *((void **)(*code));
  *code += sizeof(void *);
  return p;
}

#endif
