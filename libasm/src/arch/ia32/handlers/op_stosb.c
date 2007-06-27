/**
 * @file op_stosb.c
 * @ingroup handlers_ia32
** $Id: op_stosb.c,v 1.4 2007-06-27 11:25:12 heroine Exp $
**
*/
#include <libasm.h>
#include <libasm-int.h>

/*
  <instruction func="op_stosb" opcode="0xaa"/>
*/

int op_stosb(asm_instr *new, u_char *opcode, u_int len, asm_processor *proc) 
{
    new->ptr_instr = opcode;
  new->instr = ASM_STOSB;
  new->len += 1;
  
#if LIBASM_USE_OPERAND_VECTOR
  new->len += asm_operand_fetch(&new->op1, opcode, ASM_OTYPE_YDEST, new);
  new->len += asm_operand_fetch(&new->op2, opcode, ASM_OTYPE_XSRC, new);
#else
  new->op1.type = ASM_OTYPE_YDEST;
  new->op2.type = ASM_OTYPE_XSRC;
  
  new->op1.prefix = ASM_PREFIX_ES;
  new->op1.regset = ASM_REGSET_R32;
  new->op1.content = ASM_OP_FIXED | ASM_OP_BASE | ASM_OP_REFERENCE;
  new->op1.baser = ASM_REG_EDI;
  new->op1.len = 0;
  
  new->op2.len = 0;
  new->op2.content = ASM_OP_BASE;
  new->op2.regset = ASM_REGSET_R8;
  new->op2.baser = ASM_REG_AL;
#endif  
  return (new->len);
}
