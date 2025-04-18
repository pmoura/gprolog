/*-------------------------------------------------------------------------*
 * GNU Prolog                                                              *
 *                                                                         *
 * Part  : mini-assembler to assembler translator                          *
 * File  : ma2asm.c                                                        *
 * Descr.: code generation                                                 *
 * Author: Daniel Diaz                                                     *
 *                                                                         *
 * Copyright (C) 1999-2025 Daniel Diaz                                     *
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
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "../EnginePl/gp_config.h"
#include "../TopComp/copying.c"


#define MA2ASM_FILE

#include "ma_parser.h"
#include "ma_protos.h"

/* we need several maps, in all case the key is a string */
#define MAP_KEY_TYPE char *
#define MAP_KEY_CMP(x, y) strcmp(x, y)

#define MAP_VALUE_TYPE CodeInf
#define MAP_NAME map_cod
#include "../Tools/map_rbtree.h"

#undef MAP_VALUE_TYPE
#undef MAP_NAME
#define MAP_VALUE_TYPE LongInf
#define MAP_NAME map_lng
#define MAP_RE_INCLUDE
#include "../Tools/map_rbtree.h"

#undef MAP_VALUE_TYPE
#undef MAP_NAME
#define MAP_VALUE_TYPE StringInf
#define MAP_NAME map_str
#define MAP_RE_INCLUDE
#include "../Tools/map_rbtree.h"

#undef MAP_VALUE_TYPE
#undef MAP_NAME
#define MAP_VALUE_TYPE DoubleInf
#define MAP_NAME map_dbl
#define MAP_RE_INCLUDE
#include "../Tools/map_rbtree.h"


#if 0
#define DEBUG
#endif


/*---------------------------------*
 * Constants                       *
 *---------------------------------*/

#define DEFAULT_OUTPUT_SUFFIX      ASM_SUFFIX




/*---------------------------------*
 * Type Definitions                *
 *---------------------------------*/

/*---------------------------------*
 * Global Variables                *
 *---------------------------------*/

char *file_name_in;
char *file_name_out;

Bool comment;
Bool pic_code;
Bool ignore_fc;

MapperInf mi;

LabelGen lg_cont; /* local label generator for continuations (always available) see macros Label_Cont_XXX() */

FILE *file_out;

struct map_rbt map_code = MAP_INIT;	/* only filled if pre-pass is activated */
struct map_rbt map_long = MAP_INIT;
struct map_rbt map_string = MAP_INIT;
struct map_rbt map_double = MAP_INIT;	/* only filled used if needed */

char *initializer_fct = NULL;

char local_label[64];
int local_label_count = 0;



/*---------------------------------*
 * Function Prototypes             *
 *---------------------------------*/

StringInf *Record_String(char *str);

DoubleInf *Record_Double(char *ma_str, double dbl_val);

void Switch_Rec(int start, int stop, SwtInf swt[]);

void Switch_Equal(SwtInf *c);

int Switch_Cmp_Int(SwtInf *c1, SwtInf *c2);

void Inst_Out(char *op, char *operands);

void Char_Out(char c);

void String_Out(char *s);

void Int_Out(int d);

void Parse_Arguments(int argc, char *argv[]);

void Display_Help(void);



#define Check_Arg(i, str)      (strncmp(argv[i], str, strlen(argv[i])) == 0)




/*-------------------------------------------------------------------------*
 * MAIN                                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
  int n;

  /* some default values */
  mi.can_produce_pic_code = FALSE;
  mi.needs_pre_pass = FALSE;
  mi.comment_prefix = "#";
  mi.local_symb_prefix = "L";
  mi.string_symb_prefix = "LC";
  mi.double_symb_prefix = "LD";
  mi.strings_need_null = FALSE;
  mi.call_c_reverse_args = FALSE;

  Init_Mapper();

  Parse_Arguments(argc, argv);

  if (file_name_out == NULL)
    file_out = stdout;
  else if ((file_out = fopen(file_name_out, "wt")) == NULL)
    {
      fprintf(stderr, "cannot open output file %s\n", file_name_out);
      exit(1);
    }

#if 0	  /* done with MAP_INIT */
  map_cod_init(&map_code); 		/* only filled if pre_pass is activated */
  map_lng_init(&map_long);
  map_str_init(&map_string);
  map_dbl_init(&map_double);		/* only filled if dico double is needed */
#endif
  
  Label_Gen_Init(&lg_cont, "cont");	/* available for any mapper */
  
  Asm_Start();

  /* always define a fail label.
   * Could be in the middle of the file for targets with
   * limits on branching offsets (needs a pre-pass) */
  Label("fail");		
  Pl_Fail(2);			/* 2 to force inline (not a preference) */

  if (!Parse_Ma_File(file_name_in, comment))
    {
      fprintf(stderr, "Translation aborted\n");
      exit(1);
    }

  Data_Start(initializer_fct);

  n = map_string.size;
  if (n)
    {
      Dico_String_Start(n);
      map_foreach2(map_str, &map_string, entry)
	{
	  Dico_String(&entry->value);
	}
      Dico_String_Stop(n);
    }

  n = map_double.size;
  if (n)
    {
      Dico_Double_Start(n);
      map_foreach2(map_dbl, &map_double, entry)
        {
          if (comment)
	    Inst_Printf("", "%s %s", mi.comment_prefix, entry->value.cmt_str);
	  Dico_Double(&entry->value);
        }
      Dico_Double_Stop(n);
    }

  n = map_long.size;
  if (n)
    {
      Dico_Long_Start(n);
      map_foreach2(map_lng, &map_long, entry)
	{
	  Dico_Long(&entry->value);
	}
      Dico_Long_Stop(n);
    }

  Data_Stop(initializer_fct);

  Asm_Stop();

  if (file_out != stdout)
    fclose(file_out);

  exit(0);
}




/*-------------------------------------------------------------------------*
 * RECORD_STRING                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
StringInf *
Record_String(char *str)
{
  char label[32];
  bool created;
  struct map_str_entry *entry = map_str_put(&map_string, str, &created);
  StringInf *s = &entry->value;

  if (created)
    {
      s->no = map_string.counter_add - 1;
      s->str = str;
      sprintf(label, "%s%d", mi.string_symb_prefix, s->no);
      s->symb = strdup(label);
    }

  return s;
}




/*-------------------------------------------------------------------------*
 * RECORD_DOUBLE                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
DoubleInf *
Record_Double(char *ma_str, double dbl_val)
{
  char label[32];
  static char cmt[64];
  bool created;
  struct map_dbl_entry *entry = map_dbl_put(&map_double, ma_str, &created);
  DoubleInf *d = &entry->value;

  if (created)
    {
      d->no = map_double.counter_add - 1;
      d->ma_str = ma_str;
      sprintf(label, "%s%d", mi.double_symb_prefix, d->no);
      d->symb = strdup(label);
      d->v.dbl = dbl_val;

      /* check if the double read in MA file is given in a human-readable form */
      char *p = ma_str;
      while(*p && strchr("0123456789.-eE", *p))
	p++;

      d->is_ma_str_human = (*p == '\0');

      if (d->is_ma_str_human)
	d->cmt_str = d->ma_str;
      else
	{
	  sprintf(cmt, "%s = %1.17g", ma_str, dbl_val);
	  d->cmt_str = strdup(cmt);
	}
    }

  return d;
}




/*-------------------------------------------------------------------------*
 * DECLARE_INITIALIZER                                                     *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Declare_Initializer(char *init_fct)
{				/* init_fct: strdup done by the parser */
  initializer_fct = init_fct;
}




/*-------------------------------------------------------------------------*
 * DECL_CODE                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void				/* called if pre_pass */
Decl_Code(CodeInf *c)
{
  /* map_put ensures space for a copy of c
   * name: strdup done by the parser */
  map_cod_put(&map_code, c->name, NULL)->value = *c;
}




/*-------------------------------------------------------------------------*
 * DECL_LABEL                                                              *
 *                                                                         *
 *-------------------------------------------------------------------------*/

/* declare labels which can be far (see Is_Symbol_Close_Enough) */

void				/* called if pre_pass AND by mappers */
Decl_Label(char *name, int approx_inst_line)
{
  CodeInf c_lab;
  c_lab.name = strdup(name);
  c_lab.approx_inst_line = approx_inst_line;
  c_lab.type = CODE_TYPE_LABEL;
  c_lab.global = FALSE;
  Decl_Code(&c_lab);
}




/*-------------------------------------------------------------------------*
 * DECL_LONG                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Decl_Long(LongInf *l)
{
  /* map_put ensures space for a copy of l
   * name: strdup done by the parser */
  map_lng_put(&map_long, l->name, NULL)->value = *l;
}




/*-------------------------------------------------------------------------*
 * IS_CODE_DEFINED                                                         *
 *                                                                         *
 * Needs a pre-pass.                                                       *
 *-------------------------------------------------------------------------*/
int
Is_Code_Defined(char *name)
{
#ifdef DEBUG
  if (!mi.needs_pre_pass)
    printf("WARNING: %s:%d needs a pre-pass\n",  __FILE__, __LINE__);
#endif

  return map_cod_contains(&map_code, name);
}




/*-------------------------------------------------------------------------*
 * GET_CODE_INFOS                                                          *
 *                                                                         *
 * Needs a pre-pass.                                                       *
 *-------------------------------------------------------------------------*/
CodeInf *
Get_Code_Infos(char *name)
{
  struct map_cod_entry *entry = map_cod_get(&map_code, name);

#ifdef DEBUG
  if (!mi.needs_pre_pass)
    printf("WARNING: %s:%d needs a pre-pass\n",  __FILE__, __LINE__);
#endif

  return (entry == NULL) ? NULL : &entry->value;
}




/*-------------------------------------------------------------------------*
 * GET_LONG_INFOS                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
LongInf *
Get_Long_Infos(char *name)
{
  struct map_lng_entry *entry = map_lng_get(&map_long, name);

  return (entry == NULL) ? NULL : &entry->value;
}




/*-------------------------------------------------------------------------*
 * SCOPE_OF_SYMBOL                                                         *
 *                                                                         *
 * Need a pre-pass.                                                        *
 * global: a global symbol ? (else local)                                  *
 * Return: type of code (or CODE_TYPE_NONE)                                *
 *-------------------------------------------------------------------------*/

/* A symbol not defined (not seen as code, local label, long) can be:
 * - foreign_long, foreign_double (thus external)
 * - an external predicate or C function (e.g. function associated to a WAM 
 *   instruction like Get_Integer() or Pl_Un_Term() which is generated when 
 *   compiling foreign directive).
 * - a local symbol (strings, double constants)
 * Only the last case is local and we know it begins with . or a machine prefix (e.g. L)
 */
CodeType
Scope_Of_Symbol(char *name, Bool *global)
{
  CodeInf *c;
  LongInf *l;

#ifdef DEBUG
  if (!mi.needs_pre_pass)
    printf("WARNING: %s:%d needs a pre-pass\n",  __FILE__, __LINE__);
#endif

  c = Get_Code_Infos(name);
  if (c)
    {
      *global = c->global;
      return c->type;
    }

  l = Get_Long_Infos(name);
  if (l)
    {
#ifdef DEBUG
      printf("Long: %s  %d\n", name, l->global);
#endif
      *global = l->global;
      return CODE_TYPE_NONE;
    }

#define Has_Prefix(p) (*p && strncmp(name, p, strlen(p)) == 0)

  /* test other local symbols (not recorded in map_code/map_long, e.g. strings) */
  if (*name == '.' ||
      Has_Prefix(mi.local_symb_prefix) ||
      Has_Prefix(mi.string_symb_prefix) ||
      Has_Prefix(mi.double_symb_prefix))
    {
#ifdef DEBUG
      printf(".local: %s   (mi.local_symb_prefix: <%s>)\n", name, mi.local_symb_prefix);
#endif
      *global = FALSE; /* local symbols associated to string, double */
      return CODE_TYPE_NONE;
    }

#ifdef DEBUG
    printf("NOT FOUND: %s\n", name);
#endif
  
  /* not defined, thus (external) global symbol, cannot be more precide
   * most of the time it is a code (Get_Integer) but can be a data (foreign_long/double) 
   */
  *global = TRUE;
  return CODE_TYPE_C;	
}




/*-------------------------------------------------------------------------*
 * IS_SYMBOL_CLOSE_ENOUGH                                                  *
 *                                                                         *
 *-------------------------------------------------------------------------*/
Bool
Is_Symbol_Close_Enough(char *name, int dist_max)
{
  CodeInf *c;
  int line_def;
  int dist;

#ifdef DEBUG
  if (!mi.needs_pre_pass)
    printf("WARNING: %s:%d needs a pre-pass\n",  __FILE__, __LINE__);
#endif

  /* see ma_parser.c for info on approx inst line */
  if (name == NULL || strcmp(name, "fail") == 0) /* fail is created at start of asm file (not in bt_dico) */
    line_def = 0;
  else if ((c = Get_Code_Infos(name)) != NULL)
    line_def = c->approx_inst_line;
  else
    line_def = nb_appox_inst_line;
#if 0
  if (strcmp(name, "Zpred1_1459") == 0)
  //if (cur_line_no > 141360 && cur_line_no < 141370)
    printf("%s: line_def: %d  cur_line_no: %d  cur_approx_inst_line: %d\n", name, line_def, cur_line_no, cur_approx_inst_line);
#endif
  dist = abs(line_def - cur_approx_inst_line);

  return dist < dist_max;
}




/*-------------------------------------------------------------------------*
 * CALL_C                                                                  *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Call_C(char *fct_name, Bool fc, int nb_args, int nb_args_in_words, ArgInf arg[])
{
  unsigned i;			/* unsigned is important for the loop */
  int inc;
  int offset = 0;
  StringInf *s;
  DoubleInf *d;

  if (ignore_fc)
    fc = FALSE;

  Call_C_Start(fct_name, fc, nb_args, nb_args_in_words);

  if (!mi.call_c_reverse_args)
    i = 0, inc = 1;
  else
    i = nb_args - 1, inc = -1;

  for (; i < (unsigned) nb_args; i += inc)
    {
      switch (arg[i].type)
	{
	case INTEGER:
	  offset += Call_C_Arg_Int(offset, arg[i].int_val);
	  break;

	case FLOAT:		/* strdup done by the parser */
	  d = Record_Double(arg[i].str_val, arg[i].dbl_val);
	  if (comment)
	    Inst_Printf("", "%s %s", mi.comment_prefix, d->cmt_str);
	  offset += Call_C_Arg_Double(offset, d);
	  break;

	case STRING:		/* strdup done by the parser */
	  s = Record_String(arg[i].str_val);
	  if (comment)
	    Inst_Printf("", "%s %s", mi.comment_prefix, s->str);
	  offset += Call_C_Arg_String(offset, s);
	  break;

	case MEM:
	  offset += Call_C_Arg_Mem_L(offset, arg[i].adr_of, arg[i].str_val, arg[i].index);
	  break;

	case X_REG:
	  offset += Call_C_Arg_Reg_X(offset, arg[i].adr_of, arg[i].index);
	  break;

	case Y_REG:
	  offset += Call_C_Arg_Reg_Y(offset, arg[i].adr_of, arg[i].index);
	  break;

	case FL_ARRAY:
	  offset += Call_C_Arg_Foreign_L(offset, arg[i].adr_of, arg[i].index);
	  break;

	case FD_ARRAY:
	  offset += Call_C_Arg_Foreign_D(offset, arg[i].adr_of, arg[i].index);
	  break;

	default:		/* for the compiler */
	  ;
	}
    }

  Call_C_Invoke(fct_name, fc, nb_args, nb_args_in_words);

  Call_C_Stop(fct_name, nb_args);
}




/*-------------------------------------------------------------------------*
 * SWITCH_RET                                                              *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Switch_Ret(int nb_swt, SwtInf swt[])
{
  qsort((void *) swt, nb_swt, sizeof(SwtInf), (int (*)(const void *, const void *)) Switch_Cmp_Int);
#if 0
  printf("SWITCH_RET with %d entries  at approx: %d\n", nb_swt, cur_approx_inst_line);
#endif
  Switch_Rec(0, nb_swt - 1, swt);
}




/*-------------------------------------------------------------------------*
 * SWITCH_REC                                                              *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Switch_Rec(int start, int stop, SwtInf swt[])
{
  int mid;
  char lab_cont[32];

  switch (stop - start + 1)	/* nb elements */
    {
    case 1:
      Switch_Equal(swt + start);
      Pl_Fail(FALSE);
      break;

    case 2:
      Switch_Equal(swt + start);
      Switch_Equal(swt + stop);
      Pl_Fail(FALSE);
      break;

    case 3:
      Switch_Equal(swt + start);
      Switch_Equal(swt + start + 1);
      Switch_Equal(swt + stop);
      Pl_Fail(FALSE);
      break;

    default:
      mid = (start + stop) / 2;
      Switch_Equal(swt + mid);
      strcpy(lab_cont, Label_Cont_New());
      Decl_Label(lab_cont, cur_approx_inst_line + mid);
#if 0      
      printf(" label for mid: %d (%ld) \t-> %s at approx inst: %d\n", mid, swt[mid].int_val, lab_cont, cur_approx_inst_line + mid);
#endif
      Jump_If_Greater(lab_cont); /* comes after a Switch_Equal (thus Cmp_Ret_And_Int is done) */
      Switch_Rec(start, mid - 1, swt);
      Label(lab_cont);
      Switch_Rec(mid + 1, stop, swt);
    }
}




/*-------------------------------------------------------------------------*
 * SWITCH_EQUAL                                                            *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Switch_Equal(SwtInf *c)
{
  Cmp_Ret_And_Int(c->int_val);
  Jump_If_Equal(c->label);
}




/*-------------------------------------------------------------------------*
 * SWITCH_CMP_INT                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Switch_Cmp_Int(SwtInf *c1, SwtInf *c2)
{
  return (int) (c1->int_val - c2->int_val);
}




/*-------------------------------------------------------------------------*
 * LABEL_GEN_INIT                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Label_Gen_Init(LabelGen *g, char *prefix)
{
  g->prefix = prefix;
  g->no = 0;
  g->label[0] = '\0';
}




/*-------------------------------------------------------------------------*
 * LABEL_GEN_NEW                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
char *
Label_Gen_New(LabelGen *g)
{
  sprintf(g->label, "%s%s%d", mi.local_symb_prefix, g->prefix, ++g->no);
  return g->label;
}




/*-------------------------------------------------------------------------*
 * LABEL_GEN_GET                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
char *
Label_Gen_Get(LabelGen *g)
{
  return g->label;
}




/*-------------------------------------------------------------------------*
 * LABEL_GEN_NO                                                            *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Label_Gen_No(LabelGen *g)
{
  return g->no;
}




/*-------------------------------------------------------------------------*
 * LABEL_PRINTF                                                            *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Label_Printf(char *label, ...)
{
  va_list arg_ptr;

  va_start(arg_ptr, label);

  vfprintf(file_out, label, arg_ptr);

  va_end(arg_ptr);
  fputc('\n', file_out);
}




/*-------------------------------------------------------------------------*
 * INST_PRINTF                                                             *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Inst_Printf(char *op, char *operands, ...)
{
  va_list arg_ptr;

  va_start(arg_ptr, operands);

  fprintf(file_out, "\t%s\t", op);
  vfprintf(file_out, operands, arg_ptr);

  va_end(arg_ptr);
  fputc('\n', file_out);
}




/*-------------------------------------------------------------------------*
 * INST_OUT                                                                *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Inst_Out(char *op, char *operands)
{
  fprintf(file_out, "\t%s\t%s\n", op, operands);
}




/*-------------------------------------------------------------------------*
 * CHAR_OUT                                                                *
 *                                                                         *
 * Only needed by mappers inlining assembly code.                          *
 *-------------------------------------------------------------------------*/
void
Char_Out(char c)
{
  fprintf(file_out, "%c", c);
}




/*-------------------------------------------------------------------------*
 * STRING_OUT                                                              *
 *                                                                         *
 * Only needed by mappers inlining assembly code.                          *
 *-------------------------------------------------------------------------*/
void
String_Out(char *s)
{
  fprintf(file_out, "%s", s);
}




/*-------------------------------------------------------------------------*
 * INT_OUT                                                                 *
 *                                                                         *
 * Only needed by mappers inlining assembly code.                          *
 *-------------------------------------------------------------------------*/
void
Int_Out(int d)
{
  fprintf(file_out, "%d", d);
}




/*-------------------------------------------------------------------------*
 * PARSE_ARGUMENTS                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Parse_Arguments(int argc, char *argv[])
{
  static char str[1024];
  int i;


  file_name_in = file_name_out = NULL;
  comment = FALSE;
  pic_code = FALSE;
  ignore_fc = FALSE;

  for (i = 1; i < argc; i++)
    {
      if (*argv[i] == '-' && argv[i][1] != '\0')
	{
	  if (Check_Arg(i, "-o") || Check_Arg(i, "--output"))
	    {
	      if (++i >= argc)
		{
		  fprintf(stderr, "FILE missing after %s option\n", argv[i - 1]);
		  exit(1);
		}

	      file_name_out = argv[i];
	      continue;
	    }

	  if (Check_Arg(i, "--pic") || Check_Arg(i, "-fPIC"))
	    {
	      if (mi.can_produce_pic_code)
		pic_code = TRUE;
	      else
		fprintf(stderr, "ignored option %s - cannot produce PIC code for this architecture\n", argv[i]);
	      continue;
	    }

	  if (Check_Arg(i, "--ignore-fast"))
	    {
	      ignore_fc = TRUE;
	      continue;
	    }

	  if (Check_Arg(i, "--comment"))
	    {
	      comment = TRUE;
	      continue;
	    }

	  if (Check_Arg(i, "--version"))
	    {
	      Display_Copying("Mini-Assembly to Assembly Compiler");
	      exit(0);
	    }

	  if (Check_Arg(i, "-h") || Check_Arg(i, "--help"))
	    {
	      Display_Help();
	      exit(0);
	    }

	  fprintf(stderr, "unknown option %s - try ma2asm --help\n", argv[i]);
	  exit(1);
	}

      if (file_name_in != NULL)
	{
	  fprintf(stderr, "input file already specified (%s)\n",
		  file_name_in);
	  exit(1);
	}
      file_name_in = argv[i];
    }

  if (file_name_in != NULL && strcmp(file_name_in, "-") == 0)
    file_name_in = NULL;

  if (file_name_out == NULL && file_name_in != NULL)
    {
      strcpy(str, file_name_in);
      i = (int) strlen(str);
      if (strcmp(str + i - 3, ".ma") == 0)
	strcpy(str + i - 3, DEFAULT_OUTPUT_SUFFIX);
      else
	strcpy(str + i, DEFAULT_OUTPUT_SUFFIX);
      file_name_out = str;
    }

  if (file_name_out != NULL && strcmp(file_name_out, "-") == 0)
    file_name_out = NULL;
}




/*-------------------------------------------------------------------------*
 * DISPLAY_HELP                                                            *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Display_Help(void)
#define L(msg)  fprintf(stderr, "%s\n", msg)
{
  L("Usage: ma2asm [option...] file");
  L("");
  L("Options:");
  L("  -o FILE, --output FILE      set output file name");
  L("  --pic, -fPIC, --dynamic     produce position independent code (PIC)");
  L("  --ignore-fast               ignore fast call (FC) declarations");
  L("  --comment                   include comments in the output file");
  L("  -h, --help                  print this help and exit");
  L("  --version                   print version number and exit");
  L("");
  L("'-' can be given as <file> for the standard input/output");
  L("");
  L("Report bugs to bug-prolog@gnu.org.");
}

#undef L
