#include "raw_code_merge.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int generate_raw_code_merge(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, i, nin_new, flag, flag3, flag4, flag5,
      bo1, bo2, o1, o2, size, bo1_, bo2_, o1_, o2_, size_, o1__, o2__;
  char line[1000], line2[1000];
  enum eassembler_type estring1, estring2, estring3, estring4, estring5,
      estring1_, estring2_, estring3_, estring4_, estring5_;
  struct parameters_block *parameters;
  nbuffer_in += i = read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag3 =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if ((flag3 > 0) &&
        (flag4 = read_assembler_line_s(line, &estring1, 0) >= 0)) {
      flag = 1;
      if ((estring1 == ememcpy) || (estring1 == ereduce) ||
          (estring1 == ememcp_) || (estring1 == ereduc_)) {
        if (read_assembler_line_ssdsdsdsdd(line, &estring1, &estring2, &bo1,
                                           &estring3, &o1, &estring4, &bo2,
                                           &estring5, &o2, &size, 0) >= 0) {
          nin_new = nbuffer_in;
          flag5 =
              read_line(buffer_in + nbuffer_in, line2, parameters->ascii_in);
          if (read_assembler_line_ssdsdsdsdd(
                  line2, &estring1_, &estring2_, &bo1_, &estring3_, &o1_,
                  &estring4_, &bo2_, &estring5_, &o2_, &size_, 0) >= 0) {
            while ((flag5 > 0) && (estring1 == estring1_) && (bo1 == bo1_) &&
                   (bo2 == bo2_) && (o1 + size == o1_) && (o2 + size == o2_) &&
                   (o1 != o2_)) {
              size += size_;
              nin_new += flag5;
              flag5 =
                  read_line(buffer_in + nin_new, line2, parameters->ascii_in);
              flag = 0;
              if (flag5 > 0) {
                if (read_assembler_line_ssdsdsdsdd(
                        line2, &estring1_, &estring2_, &bo1_, &estring3_, &o1_,
                        &estring4_, &bo2_, &estring5_, &o2_, &size_, 0) < 0) {
                  o1_ = o2_ = 0;
                }
              }
            }
            nbuffer_in = nin_new;
            if (!flag) {
              nbuffer_out += write_assembler_line_ssdsdsdsdd(
                  buffer_out + nbuffer_out, estring1, estring2, bo1, estring3,
                  o1, estring4, bo2, estring5, o2, size, parameters->ascii_out);
            } else {
              while ((flag5 > 0) && (estring1 == estring1_) && (bo1 == bo1_) &&
                     (bo2 == bo2_) && (o1 - size == o1_) &&
                     (o2 - size == o2_)) {
                size += size_;
                nin_new += flag5;
                flag5 =
                    read_line(buffer_in + nin_new, line2, parameters->ascii_in);
                flag = 0;
                o1__ = o1_;
                o2__ = o2_;
                if (flag5 > 0) {
                  if (read_assembler_line_ssdsdsdsdd(
                          line2, &estring1_, &estring2_, &bo1_, &estring3_,
                          &o1_, &estring4_, &bo2_, &estring5_, &o2_, &size_,
                          0) < 0) {
                    o1_ = o2_ = 0;
                  }
                }
              }
              nbuffer_in = nin_new;
              if (!flag) {
                nbuffer_out += write_assembler_line_ssdsdsdsdd(
                    buffer_out + nbuffer_out, estring1, estring2, bo1, estring3,
                    o1__, estring4, bo2, estring5, o2__, size,
                    parameters->ascii_out);
              }
            }
          }
        } else {
          if (read_assembler_line_ssdsdd(line, &estring1, &estring2, &o1,
                                         &estring3, &o2, &size, 0) >= 0) {
            nin_new = nbuffer_in;
            flag5 = read_line(buffer_in + nin_new, line2, parameters->ascii_in);
            if (read_assembler_line_ssdsdd(line2, &estring1_, &estring2_, &o1_,
                                           &estring3_, &o2_, &size_, 0) >= 0) {
              while ((flag5 > 0) && (estring1 == estring1_) &&
                     (o1 + size == o1_) && (o2 + size == o2_) && (o1 != o2_)) {
                size += size_;
                nin_new += flag5;
                flag5 =
                    read_line(buffer_in + nin_new, line2, parameters->ascii_in);
                flag = 0;
                if (flag5 > 0) {
                  if (read_assembler_line_ssdsdd(line2, &estring1_, &estring2_,
                                                 &o1_, &estring3_, &o2_, &size_,
                                                 0) < 0) {
                    o1_ = o2_ = 0;
                  }
                }
              }
              nbuffer_in = nin_new;
              if (!flag) {
                nbuffer_out += write_assembler_line_ssdsdd(
                    buffer_out + nbuffer_out, estring1, estring2, o1, estring3,
                    o2, size, parameters->ascii_out);
              } else {
                while ((flag5 > 0) && (estring1 == estring1_) &&
                       (o1 - size == o1_) && (o2 - size == o2_)) {
                  size += size_;
                  nin_new += flag5;
                  flag5 = read_line(buffer_in + nin_new, line2,
                                    parameters->ascii_in);
                  flag = 0;
                  o1__ = o1_;
                  o2__ = o2_;
                  if (flag5 > 0) {
                    if (read_assembler_line_ssdsdd(line2, &estring1_,
                                                   &estring2_, &o1_, &estring3_,
                                                   &o2_, &size_, 0) < 0) {
                      o1_ = o2_ = 0;
                    }
                  }
                }
                nbuffer_in = nin_new;
                if (!flag) {
                  nbuffer_out += write_assembler_line_ssdsdd(
                      buffer_out + nbuffer_out, estring1, estring2, o1__,
                      estring3, o2__, size, parameters->ascii_out);
                }
              }
            }
          }
        }
      }
      if (flag) {
        nbuffer_out +=
            write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    } else {
      flag = 0;
    }
  } while (flag3);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  delete_parameters(parameters);
  return (nbuffer_out);
error:
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
