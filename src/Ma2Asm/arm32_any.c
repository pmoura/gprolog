/*-------------------------------------------------------------------------*
 * GNU Prolog                                                              *
 *                                                                         *
 * Part  : mini-assembler to assembler translator                          *
 * File  : arm32_any.c                                                     *
 * Descr.: translation file for Linux on arm 32 bits (e.g. armv6l/armv7hf) *
 * Author: Jasper Taylor and Daniel Diaz                                   *
 *                                                                         *
 * Copyright (C) 1999-2021 Daniel Diaz                                     *
 *                                                                         *
 * This file is part of GNU Prolog                                         *
 *                                                                         *
 * GNU Prolog is free software: you can redistribute it and/or             *
 * modify it under the terms of either:                                    *
 *                                                                         *
 *   - the GNU Lesser General Public License as published by the Free      *
 *     Software Foundation; either version 3 of the License, or (at your   *
 *     option) any later version.                                          *
 *                                                                         *
 * or                                                                      *
 *                                                                         *
 *   - the GNU General Public License as published by the Free             *
 *     Software Foundation; either version 2 of the License, or (at your   *
 *     option) any later version.                                          *
 *                                                                         *
 * or both in parallel, as here.                                           *
 *                                                                         *
 * GNU Prolog is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License for more details.                                *
 *                                                                         *
 * You should have received copies of the GNU General Public License and   *
 * the GNU Lesser General Public License along with this program.  If      *
 * not, see http://www.gnu.org/licenses/.                                  *
 *-------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>


/* ARM A32 instruction set                                                                                      
 * https://developer.arm.com/documentation/dui0801/k/A32-and-T32-Instructions?lang=en
 */


/* pl_reg_bank is normally the first mapped register. If it is not mapped 
 * it is because:
 *
 * 1) no registers are used (either none available or --disable-regs).
 *    In that case if is loaded in a callee-save register by engine1.c
 *    (see ASM_REG_BANK)
 *
 * 2) or because NO_MACHINE_REG_FOR_REG_BANK is defined (debug only ?).
 *    In that case Load_Reg_Bank loads it in a callee-save register. 
 *    But this register must not be already used (mapped), 
 *    so we here check no registers are used at all !
 */

#if defined(NO_MACHINE_REG_FOR_REG_BANK) && NB_USED_MACHINE_REGS > 0
#error NO_MACHINE_REG_FOR_REG_BANK can only be defined if no registers are used at all (use --disable_regs)
#endif

#ifdef MAP_REG_BANK
#define ASM_REG_BANK MAP_REG_BANK
#else
#define ASM_REG_BANK "r10"	/* see engine1.c. If NO_MACHINE_REG_FOR_REG_BANK see Load_Reg_Bank */
#endif


#ifdef NO_MACHINE_REG_FOR_REG_BANK
#define Load_Reg_Bank()   Load_Address(ASM_REG_BANK, "pl_reg_bank")
#else
#define Load_Reg_Bank()
#endif




/* To load an immediate (constant or label) use the pseudo-instruction:
 *    ldr, =immediate or address
 * the assembler tries replace it by one instruction 
 * (mov, mvn on armv6, movw/movt on armv7).
 * If not possible, it places the constant in a literal pool and generates a 
 * pc-relative ldr instruction that reads the constant from the literal pool.
 * For this reson, the pool must not be too far else we obtain an error:
 *    invalid literal constant: pool needs to be closer
 * We can inform the assembler to force the emission of the pool with 
 *    .ltorg directive
 * We try to emit it ASAP (e.g. after a branching). We also use a mechanisme
 * to force the pool emission when a maximum number of call_c is reached.
 */
#if 1
#define USE_LDR_PSEUDO_OP_AND_POOL
#define MAX_CALL_C_BEFORE_EMIT_POOL 50
#endif




/*---------------------------------*
 * Constants                       *
 *---------------------------------*/

#define STRING_PREFIX              ".LC"
#define DOUBLE_PREFIX              ".LCD"

#define ASM_DOUBLE_DIRECTIV_PREFIX "0d"

#define BPW                        4
#define MAX_ARGS_IN_REGS           4
#define MAX_DOUBLES_IN_REGS        8

#define MAX_C_ARGS_IN_C_CODE       32
#define RESERVED_STACK_SPACE       (MAX_C_ARGS_IN_C_CODE - MAX_ARGS_IN_REGS) * BPW


#define MAX_DOUBLES_IN_PRED        2048


#define UN                         ""




/*---------------------------------*
 * Type Definitions                *
 *---------------------------------*/

typedef union
{
  double d;
  int w[2];
}Dbl_2x32;




/*---------------------------------*
 * Global Variables                *
 *---------------------------------*/

int dbl_lc_no = 0;
int dbl_reg_no;

char asm_reg_e[20];

#ifdef USE_LDR_PSEUDO_OP_AND_POOL
LabelGen lg_pool;
#else
LabelGen lg_addr;
#endif


	  /* variables for ma_parser.c / ma2asm.c */

int needs_pre_pass = 0;		/* overwritte var of ma_parser.c */

int can_produce_pic_code = 0;
char *comment_prefix = "#";
char *local_symb_prefix = ".L";
int strings_need_null = 0;
int call_c_reverse_args = 0;

char *inline_asm_data[] = { NULL };




/*---------------------------------*
 * Function Prototypes             *
 *---------------------------------*/


/*-------------------------------------------------------------------------*
 * EMIT_POOL                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Emit_Pool(int after_call_c)
{
#ifdef USE_LDR_PSEUDO_OP_AND_POOL
  static int call_c_since_emit_pool = 0;

  if (after_call_c && ++call_c_since_emit_pool < MAX_CALL_C_BEFORE_EMIT_POOL)
    return;
  
  if (after_call_c)		/* after a call to a C fct needs a branch over the pool */
    {
      Inst_Printf("b", Label_Gen_New(&lg_pool));
      Inst_Printf(".ltorg", "");
      Label_Printf("%s:", Label_Gen_Get(&lg_pool));
    }
  else
    {
      Inst_Printf(".ltorg", "");
    }
  call_c_since_emit_pool = 0;
#endif
}




/*-------------------------------------------------------------------------*
 * NEAREST_IMMEDIATE                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
/* Only certain values can be used for immediate addition, break into
 * separate steps consisting of these values (its good enough for gcc...)
 */
int
Nearest_Immediate(int target)
{
  int close = (target >= 0) ? target : -target;
  int exp = 0;

  while (close > 255)
    {
      exp += 2;
      close = close >> 2;
    }
  close = close << exp;

  return (target >= 0) ? close : -close;
}




/*-------------------------------------------------------------------------*
 * INCREMENT_REG                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Increment_Reg(char *r, int int_val)
{
  /* Could also be something like
   *    ldr R, =int_val
   *    add r, r, R    but which reg R to use ? 
   * R cannot be r0, r3 (agrs), neither r4 (see BEFORE_ARG), 
   * r5 is OK but ifremoved from the global registers in machine.h 
   * and should be saved/restored since it is a callee-save reg (thus push/pop)
   */
  while (int_val)
    {
      int close = Nearest_Immediate(int_val);

      Inst_Printf("add", "%s, %s, #%d", r, r, close);
      int_val -= close;
    }
}




/*-------------------------------------------------------------------------*
 * LOAD_IMMEDIATE                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
/* More info:
 * https://xlogicx.net/ARM_12-bit_Immediates_are_Too_High_Level.html
 * ImmSuggest: https://github.com/XlogicX/ImmSuggest
 * https://alisdair.mcdiarmid.org/arm-immediate-value-encoding/
 */
void
Load_Immediate(char *r, PlLong int_val)
{
#ifdef USE_LDR_PSEUDO_OP_AND_POOL

  Inst_Printf("ldr", "%s, =#%" PL_FMT_d, r, int_val);

#else

#if 0 /* 0-65535 allowable for mov in armv7 (not in armv6) */
  int close = int_val & 65535; 
#else
  int close;

  if (int_val < 0)
    close = ~Nearest_Immediate(~int_val);
  else
    close = Nearest_Immediate(int_val);
#endif
  Inst_Printf("mov", "%s, #%" PL_FMT_d, r, close);
  Increment_Reg(r, int_val - close);
#endif
}




/*-------------------------------------------------------------------------*
 * LOAD_ADDRESS                                                            *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Load_Address(char *r, char *addr)
{
#ifdef USE_LDR_PSEUDO_OP_AND_POOL
  Inst_Printf("ldr", "%s, =%s", r, addr);
#else
  /* Direct address loading only works over a certain range -- to avoid
   * limits we have to include address as a literal... */
  Inst_Printf("ldr", "%s, %s-4", r, Label_Gen_New(&lg_addr));
  Inst_Printf("b", Label_Gen_Get(&lg_addr));
  Emit_Pool(0);
  Inst_Printf(".word", "%s", addr);
  Label_Printf("%s:", Label_Gen_Get(&lg_addr));
#endif
}




/*-------------------------------------------------------------------------*
 * ASM_START                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Asm_Start(void)
{
#ifdef MAP_REG_E
  strcpy(asm_reg_e, MAP_REG_E);
#else
  strcpy(asm_reg_e, "r11");
#endif

#ifdef USE_LDR_PSEUDO_OP_AND_POOL
  Label_Gen_Init(&lg_pool, "pool");
#else
  Label_Gen_Init(&lg_addr, "addr");
#endif
  
  Label_Printf(".text");
#if 0                           /* see Fail_Ret() */
  Label("fail");
  Pl_Fail();
#endif
}




/*-------------------------------------------------------------------------*
 * ASM_STOP                                                                *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Asm_Stop(void)
{
#ifdef __ELF__
  Inst_Printf(".section", ".note.GNU-stack,\"\"");
#endif
}




/*-------------------------------------------------------------------------*
 * CODE_START                                                              *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Code_Start(char *label, int prolog, int global)
{
  Label_Printf("");
  Inst_Printf(".align", "2");
  Inst_Printf(".fpu", "vfp");
  Inst_Printf(".type", "%s, %%function", label);

  if (global)
    Inst_Printf(".global", "%s", label);

  Label(label);

  if (!prolog)
    {
      Inst_Printf("push", "{r4, lr}");
      Inst_Printf("sub", "sp, sp, #%d", RESERVED_STACK_SPACE);
    }

}




/*-------------------------------------------------------------------------*
 * CODE_STOP                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Code_Stop(void)
{
}




/*-------------------------------------------------------------------------*
 * LABEL                                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Label(char *label)
{
  Label_Printf("");
  Label_Printf("%s:", label);
}




/*-------------------------------------------------------------------------*
 * RELOAD_E_IN_REGISTER                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Reload_E_In_Register(void)
{
#ifndef MAP_REG_E
  Load_Reg_Bank();
  Inst_Printf("ldr", "%s, [%s, #%d]", asm_reg_e, ASM_REG_BANK, MAP_OFFSET_E);
#endif
}




/*-------------------------------------------------------------------------*
 * PL_JUMP                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Jump(char *label)
{
  Inst_Printf("b", "%s", label);
  Emit_Pool(0);
}




/*-------------------------------------------------------------------------*
 * PREP_CP                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Prep_CP(void)
{
#ifdef MAP_REG_CP
  Load_Address(MAP_REG_CP, Label_Cont_New());
#else
  Load_Address("r2", Label_Cont_New());
  Load_Reg_Bank();
  Inst_Printf("str", "r2, [%s, #%d]", ASM_REG_BANK, MAP_OFFSET_CP);
#endif
}




/*-------------------------------------------------------------------------*
 * HERE_CP                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Here_CP(void)
{
  Emit_Pool(0);
  Label_Printf("%s:", Label_Cont_Get());
}




/*-------------------------------------------------------------------------*
 * PL_CALL                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Call(char *label)
{
  Prep_CP();
  Pl_Jump(label);
  Here_CP();
}




/*-------------------------------------------------------------------------*
 * PL_FAIL                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Fail(void)
{
#ifdef MAP_REG_B
  Inst_Printf("ldr", "pc, [" MAP_REG_B ", #-4]");
#else
  Load_Reg_Bank();
  Inst_Printf("ldr", "r3, [%s, #%d]", ASM_REG_BANK, MAP_OFFSET_B);
  Inst_Printf("ldr", "pc, [r3, #-4]");
#endif
  Emit_Pool(0);
}




/*-------------------------------------------------------------------------*
 * PL_RET                                                                  *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Ret(void)
{
#ifdef MAP_REG_CP
  Inst_Printf("mov", "pc, " MAP_REG_CP);
#else
  Load_Reg_Bank();
  Inst_Printf("ldr", "pc, [%s, #%d]", ASM_REG_BANK, MAP_OFFSET_CP);
#endif
  Emit_Pool(0);
}




/*-------------------------------------------------------------------------*
 * JUMP                                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump(char *label)
{
  Inst_Printf("b", "%s", label);
  Emit_Pool(0);
}




/*-------------------------------------------------------------------------*
 * MOVE_FROM_REG_X                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_From_Reg_X(int index)
{
  Load_Reg_Bank();
  Inst_Printf("ldr", "r2, [%s, #%d]", ASM_REG_BANK, index * BPW);
}




/*-------------------------------------------------------------------------*
 * LOAD_STORE_REG_Y                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Load_Store_Reg_Y(char *ldr_str, char *r, int index)
{
  int offset = Y_OFFSET(index);	/* we know it is negative anyway... */

  if (offset >= -4095 && offset <= 4095)
    {
      Inst_Printf(ldr_str, "%s, [%s, #%d]", r, asm_reg_e, offset);
    }
  else
    {
      /* Needs another register R for the mov R, E. 
       * In case of a ldr we can use the same destination register r.
       * In case of a str we cannot use r because it is the source register to store.
       * We cannot use r0..r3, (call_c args) neither a gloal reg used in machine.h. 
       * r4 is OK 
       */
      Inst_Printf("mov", "r4, %s", asm_reg_e);
      Increment_Reg("r4", offset);
      Inst_Printf(ldr_str, "%s, [r4]", r);
    }
}




/*-------------------------------------------------------------------------*
 * MOVE_FROM_REG_Y                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_From_Reg_Y(int index)
{
  Load_Store_Reg_Y("ldr", "r2", index);
}




/*-------------------------------------------------------------------------*
 * MOVE_TO_REG_X                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_To_Reg_X(int index)
{
  Load_Reg_Bank();
  Inst_Printf("str", "r2, [%s, #%d]", ASM_REG_BANK, index * BPW);
}




/*-------------------------------------------------------------------------*
 * MOVE_TO_REG_Y                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_To_Reg_Y(int index)
{
  Load_Store_Reg_Y("str", "r2", index);
}




/*-------------------------------------------------------------------------*
 * CALL_C_START                                                            *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Call_C_Start(char *fct_name, int fc, int nb_args, int nb_args_in_words,
	     char **p_inline)
{
  dbl_reg_no = 0;
}




#define STACK_OFFSET(offset)   (offset_excl_doubles - MAX_ARGS_IN_REGS) * BPW
#define DBL_RET_WORDS          2

#define BEFORE_ARG				\
{						\
 char r[16];  /* 4 is enough but avoid gcc warning, see -Wformat-overflow */ \
 int offset_excl_doubles = offset-2*dbl_reg_no; \
   						\
  if (offset_excl_doubles < MAX_ARGS_IN_REGS)	\
    sprintf(r, "r%d", offset_excl_doubles + 0);	\
  else						\
    strcpy(r, "r4");




#define AFTER_ARG					\
  if (offset_excl_doubles >= MAX_ARGS_IN_REGS)	{       \
    Inst_Printf("str", "%s, [sp, #%d]", r,		\
		STACK_OFFSET(offset_excl_doubles));	\
  }                                             	\
}


#define AFTER_ARG_DBL					\
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_INT                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Int(int offset, PlLong int_val)
{
  BEFORE_ARG;

  Load_Immediate(r, int_val);

  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_DOUBLE                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Double(int offset, double dbl_val)
{
  BEFORE_ARG;

  Inst_Printf("b", "%s%d", DOUBLE_PREFIX, dbl_lc_no);
  Emit_Pool(0);
  Inst_Printf(".align", "2");
#if 0
  Inst_Printf(".double", ASM_DOUBLE_DIRECTIV_PREFIX "%1.20e", dbl_val);
#else
  Dbl_2x32 z;
  z.d = dbl_val;
  Inst_Printf(".word", "%d", z.w[0]);
  Inst_Printf(".word", "%d", z.w[1]);
#endif
  Label_Printf("%s%d:", DOUBLE_PREFIX, dbl_lc_no);
  Inst_Printf("vldr.64", "d%d, %s%d-8",
	      dbl_reg_no++, DOUBLE_PREFIX, dbl_lc_no++);

  AFTER_ARG_DBL;

  return DBL_RET_WORDS;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_STRING                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_String(int offset, int str_no)
{
  BEFORE_ARG;

  char labl[16];

  sprintf(labl, "%s%d", STRING_PREFIX, str_no);
  Load_Address(r, labl);

  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_MEM_L                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Mem_L(int offset, int adr_of, char *name, int index)
{
  BEFORE_ARG;

  Load_Address(r, name);
  Increment_Reg(r, index * BPW);
  if (!adr_of)
    Inst_Printf("ldr", "%s, [%s]", r, r);

  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_REG_X                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Reg_X(int offset, int adr_of, int index)
{
  BEFORE_ARG;

  Load_Reg_Bank();
  Inst_Printf("mov", "%s, %s", r, ASM_REG_BANK);

  if (adr_of)
    Increment_Reg(r, index * BPW);
  else
    Inst_Printf("ldr", "%s, [%s, #%d]", r, r, index * BPW);

  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_REG_Y                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Reg_Y(int offset, int adr_of, int index)
{
  BEFORE_ARG;

  Inst_Printf("mov", "%s, %s", r, asm_reg_e);
  if (adr_of)
    Increment_Reg(r, Y_OFFSET(index));
  else
    Load_Store_Reg_Y("ldr", r, index);

  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_FOREIGN_L                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Foreign_L(int offset, int adr_of, int index)
{
  return Call_C_Arg_Mem_L(offset, adr_of, "pl_foreign_long", index);
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_FOREIGN_D                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Foreign_D(int offset, int adr_of, int index)
{
  if (adr_of)
    return Call_C_Arg_Mem_L(offset, adr_of, "pl_foreign_double", index * 2);

  BEFORE_ARG;

  Inst_Printf("ldr", "r3, =pl_foreign_double");
  Increment_Reg("r3", index * 8);
  Inst_Printf("vldr.64", "d%d, [r3]", dbl_reg_no++);

  AFTER_ARG_DBL;

  return DBL_RET_WORDS;
}




/*-------------------------------------------------------------------------*
 * CALL_C_INVOKE                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Call_C_Invoke(char *fct_name, int fc, int nb_args, int nb_args_in_words)
{
  Inst_Printf("bl", "%s", fct_name);
  Emit_Pool(1);			/* only if too many call_c */
}




/*-------------------------------------------------------------------------*
 * CALL_C_STOP                                                             *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Call_C_Stop(char *fct_name, int nb_args, char **p_inline)
{
#ifndef MAP_REG_E
  if (p_inline && INL_ACCESS_INFO(p_inline))
    reload_e = 1;
#endif
}




/*-------------------------------------------------------------------------*
 * JUMP_RET                                                                *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump_Ret(void)
{
  Inst_Printf("mov", "pc, r0");
  Emit_Pool(0);
}




/*-------------------------------------------------------------------------*
 * FAIL_RET                                                                *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Fail_Ret(void)
{
  Inst_Printf("cmp", "r0, #0");
  Inst_Printf("bne", Label_Cont_New());
#if 0				/* see Asm_Start() */
  Inst_Printf("b", "fail");
#else
  Pl_Fail();
#endif
  Emit_Pool(0);				     /* take advantage of branching to emit the pool */
  Label_Printf("%s:", Label_Cont_Get());
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_MEM_L                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Mem_L(char *name, int index)
{
  int offset = index * BPW;
  Load_Address("r3", name);
  if (offset <= 4095)
    {
      Inst_Printf("str", "r0, [r3, #%d]", offset);
    }
  else
    {
      Increment_Reg("r3", offset);
      Inst_Printf("str", "r0, [r3]");
    }
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_REG_X                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Reg_X(int index)
{				/* similar to Move_To_Reg_X */
  Load_Reg_Bank();
  Inst_Printf("str", "r0, [%s, #%d]", ASM_REG_BANK, index * BPW);
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_REG_Y                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Reg_Y(int index)
{
  Load_Store_Reg_Y("str", "r0", index);
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_FOREIGN_L                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Foreign_L(int index)
{
  Move_Ret_To_Mem_L("pl_foreign_long", index);
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_FOREIGN_D                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Foreign_D(int index)
{
  Inst_Printf("ldr", "r3, =pl_foreign_double");
  Increment_Reg("r3", index * 8);
  Inst_Printf("vstr.64", "d0, [r3]");
}




/*-------------------------------------------------------------------------*
 * CMP_RET_AND_INT                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Cmp_Ret_And_Int(PlLong int_val)
{
  if (Nearest_Immediate(int_val) == int_val)
    Inst_Printf("cmp", "r0, #%" PL_FMT_d, int_val);
  else
    {
      Load_Immediate("r3", int_val);
      Inst_Printf("cmp", "r0, r3");
    }
}




/*-------------------------------------------------------------------------*
 * JUMP_IF_EQUAL                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump_If_Equal(char *label)
{
  Inst_Printf("beq", "%s", label);
}




/*-------------------------------------------------------------------------*
 * JUMP_IF_GREATER                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump_If_Greater(char *label)
{
  Inst_Printf("bgt", "%s", label);
}




/*-------------------------------------------------------------------------*
 * C_RET                                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
C_Ret(void)
{
#if 0
  Inst_Printf("nop", "@ gcc is always right"); /* why ? */
#endif
  Inst_Printf("add", "sp, #%d", RESERVED_STACK_SPACE);
  Inst_Printf("pop", "{r4, pc}");
  Emit_Pool(0);
}




/*-------------------------------------------------------------------------*
 * DICO_STRING_START                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_String_Start(int nb_consts)
{
  /* str1.4 implies 4 bytes alignment, flags: M=Merge, S=Strings A=Alloc */
  /* then add a .align 2 before each string entry or a .space n after each string */
  Inst_Printf(".section", "%s", ".rodata.str1.4,\"aMS\",%progbits,1");
  /* or else  Inst_Printf(".section", "%s", ".rodata.str1.1,\"aMS\",%progbits,1"); */
}




/*-------------------------------------------------------------------------*
 * DICO_STRING                                                             *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_String(int str_no, char *asciiz)
{
  /* gas .align on arm is same as .p2align, .align 2 means multiple of 2^2 = 4 */
  Inst_Printf(".align", "2");
  Label_Printf("%s%d:", STRING_PREFIX, str_no);
  Inst_Printf(".asciz", "%s", asciiz);
#if 0 /* NB: .space 0 generates a Warning: .space repeat count is zero, ignored */
  Inst_Printf(".space", "4 - (.-%s%d) % 4", STRING_PREFIX, str_no);
#endif
}




/*-------------------------------------------------------------------------*
 * DICO_STRING_STOP                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_String_Stop(int nb_consts)
{
}




/*-------------------------------------------------------------------------*
 * DICO_LONG_START                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_Long_Start(int nb_longs)
{
  Label_Printf(".data");
  Inst_Printf(".align", "4");
}




/*-------------------------------------------------------------------------*
 * DICO_LONG                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_Long(char *name, int global, VType vtype, PlLong value)
{
  switch (vtype)
    {
    case NONE:
      value = 1;		/* then in case ARRAY_SIZE */
    case ARRAY_SIZE:
      if (!global)
	Inst_Printf(".lcomm", "%s,%" PL_FMT_d, name, value * BPW);
      else
	Inst_Printf(".comm", UN "%s,%" PL_FMT_d, name, value * BPW);
      break;

    case INITIAL_VALUE:
      if (global)
	Inst_Printf(".global", "%s", name);
      Label_Printf("%s:", name);
      Inst_Printf(".long", "%" PL_FMT_d, value);
      break;
    }
}




/*-------------------------------------------------------------------------*
 * DICO_LONG_STOP                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_Long_Stop(int nb_longs)
{
}




/*-------------------------------------------------------------------------*
 * DATA_START                                                              *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Data_Start(char *initializer_fct)
{
  if (initializer_fct == NULL)
    return;

  Inst_Printf(".section", ".init_array,\"aw\"");
  Inst_Printf(".align", "2");
  Inst_Printf(".word", "%s", initializer_fct);
}




/*-------------------------------------------------------------------------*
 * DATA_STOP                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Data_Stop(char *initializer_fct)
{
  if (initializer_fct == NULL)
    return;

#if 0
  Label_Printf(".data");
  Label_Printf(UN "obj_chain_stop:");

  Inst_Printf(".long", UN "obj_chain_start");
#endif
}