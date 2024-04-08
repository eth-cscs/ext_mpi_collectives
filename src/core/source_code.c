#include "source_code.h"
#include "byte_code.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_source_code(char **shmem,
                               int shmem_size, int *shmemid,
                               char *buffer_in, char **sendbufs, char **recvbufs,
                               int my_size_shared_buf, int barriers_size, char *locmem,
                               int reduction_op, void *func, int *global_ranks,
                               char *code_out, int size_comm, int size_request, void *comm_row,
                               int node_num_cores_row, void *comm_column,
                               int node_num_cores_column,
                               int *shmemid_gpu, char **shmem_gpu, int *gpu_byte_code_counter, int tag, int id) {
  FILE *fp, *fpi;
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend data_irecv_isend;
  char line[1000], *ip = code_out;
  enum eassembler_type estring1a, estring1, estring2;
  int integer1, integer2, integer3, integer4, isdryrun = (code_out == NULL),
      ascii, num_cores, socket_rank, num_sockets_per_node, case_count = 0, i;
  struct header_byte_code header_temp;
  struct header_byte_code *header;
  struct parameters_block *parameters;
  char str[1000];
  buffer_in += ext_mpi_read_parameters(buffer_in, &parameters);
  ascii = parameters->ascii_in;
  num_cores = parameters->socket_row_size*parameters->socket_column_size;
  socket_rank = parameters->socket_rank;
  num_sockets_per_node = parameters->num_sockets_per_node;
  ext_mpi_delete_parameters(parameters);
  memset(&header_temp, 0, sizeof(struct header_byte_code));
  sprintf(str, "source_code_%d_%d.c", socket_rank, id);
  fp = fopen(str, "w+");
  sprintf(str, "source_code_%d_%d.i", socket_rank, id);
  fpi = fopen(str, "w+");
  fprintf(fp, "#include <stdlib.h>\n");
  fprintf(fp, "#include <limits.h>\n");
  fprintf(fp, "#include <string.h>\n\n");
  fprintf(fp, "#ifdef __x86_64__\n");
  fprintf(fp, "#define memory_fence() asm volatile(\"mfence\" :: \\\n");
  fprintf(fp, "                                      : \"memory\")\n");
  fprintf(fp, "#define memory_fence_load() asm volatile(\"lfence\" :: \\\n");
  fprintf(fp, "                                      : \"memory\")\n");
  fprintf(fp, "#define memory_fence_store() asm volatile(\"sfence\" :: \\\n");
  fprintf(fp, "                                      : \"memory\")\n");
  fprintf(fp, "#else\n");
  fprintf(fp, "#define memory_fence() __atomic_thread_fence(__ATOMIC_ACQ_REL)\n");
  fprintf(fp, "#define memory_fence_load() __atomic_thread_fence(__ATOMIC_ACQUIRE)\n");
  fprintf(fp, "#define memory_fence_store() __atomic_thread_fence(__ATOMIC_RELEASE)\n");
  fprintf(fp, "#endif\n");

  fprintf(fp, "int func_source_%d(int jmp, int wait) {\n", id);
  fprintf(fp, "  int i;\n");
  fprintf(fp, "  if (wait) {\n");
  fprintf(fp, "  switch(jmp) {\n");
  fprintf(fpi, "  switch(jmp) {\n");
  fprintf(fp, "  case %d:\n", case_count);
  fprintf(fpi, "  case %d:\n", case_count++);
  if (isdryrun) {
    header = &header_temp;
    header->mpi_user_function = func;
    header->num_cores = num_cores;
    header->socket_rank = socket_rank;
    header->num_sockets_per_node = num_sockets_per_node;
    header->function = NULL;
  } else {
    header = (struct header_byte_code *)ip;
    header->mpi_user_function = func;
    header->barrier_counter_socket = 0;
    header->barrier_counter_node = 0;
    if (shmem) {
      header->barrier_shmem_node = (char **)malloc(num_cores * num_sockets_per_node * sizeof(char *));
      header->barrier_shmem_socket = (char **)malloc(num_cores * sizeof(char *));
      for (i = 0; i < num_cores * num_sockets_per_node; i++) {
        header->barrier_shmem_node[i] = shmem[i] + my_size_shared_buf + 2 * barriers_size;
      }
      for (i = 0; i < num_cores; i++) {
        header->barrier_shmem_socket[i] = shmem[i] + my_size_shared_buf + barriers_size;
      }
    } else {
      header->barrier_shmem_node = NULL;
      header->barrier_shmem_socket = NULL;
    }
    header->barrier_shmem_size = barriers_size;
    header->shmemid = shmemid;
    header->locmem = locmem;
    header->shmem = shmem;
    header->sendbufs = sendbufs;
    header->recvbufs = recvbufs;
    header->node_num_cores_row = node_num_cores_row;
    header->node_num_cores_column = node_num_cores_column;
    header->num_cores = num_cores;
    header->socket_rank = socket_rank;
    header->num_sockets_per_node = num_sockets_per_node;
    header->tag = tag;
    header->function = NULL;
  }
  ip += sizeof(struct header_byte_code);
  while ((integer1 = ext_mpi_read_line(buffer_in, line, ascii)) > 0) {
    buffer_in += integer1;
    ext_mpi_read_assembler_line(line, 0, "sd", &estring1, &integer1);
    if (estring1 == ereturn) {
      fprintf(fp, "  }\n");
      fprintf(fpi, "  }\n");
      fprintf(fp, "  } else {\n");
      fprintf(fp, "#include \"source_code_%d_%d.i\"\n", socket_rank, id);
      fprintf(fp, "  }\n");
      fprintf(fp, "  return 0;\n");
      fprintf(fp, "}\n");
    }
    if (estring1 == eset_node_barrier) {
      if (header->num_cores != 1 || num_sockets_per_node != 1) {
	if (header->barrier_shmem_node) {
	  fprintf(fp, "    (*((int *)(%p)))++;\n", header->barrier_shmem_node[0]);
	  fprintf(fpi, "    (*((int *)(%p)))++;\n", header->barrier_shmem_node[0]);
	}
      }
    }
    if (estring1 == ewait_node_barrier) {
      if (header->num_cores != 1 || num_sockets_per_node != 1) {
        code_put_char(&ip, OPCODE_NODEBARRIER_ATOMIC_WAIT, isdryrun);
	if (header->barrier_shmem_node) {
          fprintf(fp, "  case %d:\n", case_count);
          fprintf(fpi, "  case %d:\n", case_count);
	  fprintf(fp, "    while ((unsigned int)(*((volatile int*)(%p)) - *((int *)(%p))) > INT_MAX);\n", header->barrier_shmem_node[integer1], header->barrier_shmem_node[0]);
	  fprintf(fpi, "    if ((unsigned int)(*((volatile int*)(%p)) - *((int *)(%p))) > INT_MAX) {\n", header->barrier_shmem_node[integer1], header->barrier_shmem_node[0]);
	  fprintf(fpi, "      return %d;\n", case_count++);
	  fprintf(fpi, "    }\n");
	}
      }
    }
    if (estring1 == ewaitall) {
      if (integer1) {
        code_put_char(&ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(&ip, integer1, isdryrun);
        code_put_pointer(&ip, header->locmem, isdryrun);
      }
    }
    if (estring1 == ewaitany) {
      code_put_char(&ip, OPCODE_MPIWAITANY, isdryrun);
      ext_mpi_read_assembler_line(line, 0, "sddsd", &estring1, &integer1,
                                  &integer2, &estring2, &integer3);
      code_put_int(&ip, integer1, isdryrun);
      code_put_int(&ip, integer2, isdryrun);
      code_put_pointer(&ip, header->locmem, isdryrun);
    }
    if (estring1 == eattached) {
      code_put_char(&ip, OPCODE_ATTACHED, isdryrun);
    }
    if ((estring1 == eisend) || (estring1 == eirecv)) {
      ext_mpi_read_irecv_isend(line, &data_irecv_isend);
      estring1 = data_irecv_isend.type;
      estring2 = data_irecv_isend.buffer_type;
      integer1 = data_irecv_isend.offset;
      integer2 = data_irecv_isend.size;
      integer3 = data_irecv_isend.partner;
      integer4 = data_irecv_isend.tag;
      if (estring1 == eisend) {
        code_put_char(&ip, OPCODE_MPIISEND, isdryrun);
      } else {
        code_put_char(&ip, OPCODE_MPIIRECV, isdryrun);
      }
      if (estring2 == esendbufp) {
	if (sendbufs) {
          code_put_pointer(&ip, (void *)(sendbufs[data_irecv_isend.buffer_number] + integer1), isdryrun);
	} else {
          code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	}
      } else {
        if (estring2 == erecvbufp) {
	  if (recvbufs) {
            code_put_pointer(&ip, (void *)(recvbufs[data_irecv_isend.buffer_number] + integer1), isdryrun);
	  } else {
            code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	  }
        } else {
	  if (shmem) {
            code_put_pointer(&ip, (void *)(shmem[data_irecv_isend.buffer_number] + integer1), isdryrun);
	  } else {
            code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	  }
        }
      }
      code_put_int(&ip, integer2, isdryrun);
      code_put_int(&ip, global_ranks[integer3], isdryrun);
      code_put_pointer(&ip, header->locmem + size_request * integer4, isdryrun);
    }
    if (estring1 == enode_barrier) {
      code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
    }
    if (estring1 == esocket_barrier) {
      if (header->num_cores != 1) {
        code_put_char(&ip, OPCODE_SOCKETBARRIER, isdryrun);
      }
    }
    if (estring1 == ememory_fence) {
      fprintf(fp, "    memory_fence();\n");
      fprintf(fpi, "    memory_fence();\n");
    }
    if (estring1 == ememory_fence_store) {
      fprintf(fp, "    memory_fence_store();\n");
      fprintf(fpi, "    memory_fence_store();\n");
    }
    if (estring1 == ememory_fence_load) {
      fprintf(fp, "    memory_fence_load();\n");
      fprintf(fpi, "    memory_fence_load();\n");
    }
    if ((estring1 == ememcpy) || (estring1 == ereduce) || (estring1 == einvreduce) ||
        (estring1 == esreduce) || (estring1 == esinvreduce) || (estring1 == esmemcpy)) {
      ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce);
      estring1 = data_memcpy_reduce.type;
      estring1a = data_memcpy_reduce.buffer_type1;
      integer1 = data_memcpy_reduce.offset1;
      estring2 = data_memcpy_reduce.buffer_type2;
      integer2 = data_memcpy_reduce.offset2;
      integer3 = data_memcpy_reduce.size;
        if ((estring1 == ememcpy) || (estring1 == esmemcpy)) {
	  fprintf(fp, "    memcpy((void *)");
	  fprintf(fpi, "    memcpy((void *)");
          if (estring1a == esendbufp) {
	    if (sendbufs) {
              fprintf(fp, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number1] + integer1));
              fprintf(fpi, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number1] + integer1));
	    }
          } else {
            if (estring1a == erecvbufp) {
	      if (recvbufs) {
	        fprintf(fp, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number1] + integer1));
	        fprintf(fpi, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number1] + integer1));
	      }
            } else {
              if (shmem) {
	        fprintf(fp, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1));
	        fprintf(fpi, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1));
	      }
            }
          }
	  fprintf(fp, ", (void *)");
	  fprintf(fpi, ", (void *)");
          if (estring2 == esendbufp) {
	    if (sendbufs) {
	      fprintf(fp, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number2] + integer2));
	      fprintf(fpi, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number2] + integer2));
	    }
          } else {
            if (estring2 == erecvbufp) {
	      if (recvbufs) {
	        fprintf(fp, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number2] + integer2));
	        fprintf(fpi, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number2] + integer2));
	      }
            } else {
              if (shmem) {
	        fprintf(fp, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number2] + integer2));
	        fprintf(fpi, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number2] + integer2));
	      }
            }
          }
	  fprintf(fp, ", %d);\n", integer3);
	  fprintf(fpi, ", %d);\n", integer3);
	} else {
          switch (reduction_op) {
          case OPCODE_REDUCE_SUM_DOUBLE:
            integer3 /= sizeof(double);
            break;
          case OPCODE_REDUCE_SUM_LONG_INT:
            integer3 /= sizeof(long int);
            break;
          case OPCODE_REDUCE_SUM_FLOAT:
            integer3 /= sizeof(float);
            break;
          case OPCODE_REDUCE_SUM_INT:
            integer3 /= sizeof(int);
            break;
          }
	  fprintf(fp, "    for (i = 0; i < %d; i++) {\n", integer3);
	  fprintf(fpi, "    for (i = 0; i < %d; i++) {\n", integer3);
          switch (reduction_op) {
          case OPCODE_REDUCE_SUM_DOUBLE:
            fprintf(fp, "      ((double *)(");
            fprintf(fpi, "      ((double *)(");
            break;
          case OPCODE_REDUCE_SUM_LONG_INT:
            fprintf(fp, "      ((long int *)(");
            fprintf(fpi, "      ((long int *)(");
            break;
          case OPCODE_REDUCE_SUM_FLOAT:
            fprintf(fp, "      ((float *)(");
            fprintf(fpi, "      ((float *)(");
            break;
          case OPCODE_REDUCE_SUM_INT:
            fprintf(fp, "      ((int *)(");
            fprintf(fpi, "      ((int *)(");
            break;
          }
          if (estring1a == esendbufp) {
	    if (sendbufs) {
              fprintf(fp, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number1] + integer1));
              fprintf(fpi, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number1] + integer1));
	    }
          } else {
            if (estring1a == erecvbufp) {
	      if (recvbufs) {
	        fprintf(fp, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number1] + integer1));
	        fprintf(fpi, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number1] + integer1));
	      }
            } else {
              if (shmem) {
	        fprintf(fp, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1));
	        fprintf(fpi, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1));
	      }
            }
          }
	  fprintf(fp, "))[i] += ");
	  fprintf(fpi, "))[i] += ");
          switch (reduction_op) {
          case OPCODE_REDUCE_SUM_DOUBLE:
            fprintf(fp, "((double *)(");
            fprintf(fpi, "((double *)(");
            break;
          case OPCODE_REDUCE_SUM_LONG_INT:
            fprintf(fp, "((long int *)(");
            fprintf(fpi, "((long int *)(");
            break;
          case OPCODE_REDUCE_SUM_FLOAT:
            fprintf(fp, "((float *)(");
            fprintf(fpi, "((float *)(");
            break;
          case OPCODE_REDUCE_SUM_INT:
            fprintf(fp, "((int *)(");
            fprintf(fpi, "((int *)(");
            break;
          }
          if (estring2 == esendbufp) {
	    if (sendbufs) {
	      fprintf(fp, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number2] + integer2));
	      fprintf(fpi, "%p", (void *)(sendbufs[data_memcpy_reduce.buffer_number2] + integer2));
	    }
          } else {
            if (estring2 == erecvbufp) {
	      if (recvbufs) {
	        fprintf(fp, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number2] + integer2));
	        fprintf(fpi, "%p", (void *)(recvbufs[data_memcpy_reduce.buffer_number2] + integer2));
	      }
            } else {
              if (shmem) {
	        fprintf(fp, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number2] + integer2));
	        fprintf(fpi, "%p", (void *)(shmem[data_memcpy_reduce.buffer_number2] + integer2));
	      }
            }
          }
	  fprintf(fp, "))[i];\n");
	  fprintf(fpi, "))[i];\n");
	  fprintf(fp, "    }\n");
	  fprintf(fpi, "    }\n");
	}
    }
  }
  header->size_to_return = ip - code_out;
  if (code_out) {
    if (comm_row) {
      *((void **)ip) = &ip;
      memcpy(ip + sizeof(void*), comm_row, size_comm);
    } else {
      *((void **)ip) = NULL;
      memset(ip + sizeof(void*), 0, size_comm);
    }
  }
  ip += size_comm + sizeof(void*);
  if (code_out) {
    if (comm_column) {
      *((void **)ip) = &ip;
      memcpy(ip + sizeof(void*), comm_column, size_comm);
    } else {
      *((void **)ip) = NULL;
      memset(ip + sizeof(void*), 0, size_comm);
    }
  }
  ip += size_comm + sizeof(void*);
  fclose(fpi);
  fclose(fp);
  sprintf(str, "gcc -O2 -fpic -c source_code_%d_%d.c", socket_rank, id);
  system(str);
  sprintf(str, "gcc -shared -o compiled_code_%d.so source_code_%d_*.o", socket_rank, socket_rank);
  system(str);
  return (ip - code_out);
}
