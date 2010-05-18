/* Copyright (C) 1995,1996,1997,1998,1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010
 * Free Software Foundation, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */



#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "libguile/__scm.h"
#include "libguile/_scm.h"
#include "libguile/continuations.h"
#include "libguile/eq.h"
#include "libguile/list.h"
#include "libguile/macros.h"
#include "libguile/memoize.h"
#include "libguile/modules.h"
#include "libguile/srcprop.h"
#include "libguile/ports.h"
#include "libguile/print.h"
#include "libguile/strings.h"
#include "libguile/throw.h"
#include "libguile/validate.h"





#define CAR(x)   SCM_CAR(x)
#define CDR(x)   SCM_CDR(x)
#define CAAR(x)  SCM_CAAR(x)
#define CADR(x)  SCM_CADR(x)
#define CDAR(x)  SCM_CDAR(x)
#define CDDR(x)  SCM_CDDR(x)
#define CADDR(x) SCM_CADDR(x)
#define CDDDR(x) SCM_CDDDR(x)
#define CADDDR(x) SCM_CADDDR(x)


static const char s_bad_expression[] = "Bad expression";
static const char s_expression[] = "Missing or extra expression in";
static const char s_missing_expression[] = "Missing expression in";
static const char s_extra_expression[] = "Extra expression in";
static const char s_empty_combination[] = "Illegal empty combination";
static const char s_missing_body_expression[] = "Missing body expression in";
static const char s_mixed_body_forms[] = "Mixed definitions and expressions in";
static const char s_bad_define[] = "Bad define placement";
static const char s_missing_clauses[] = "Missing clauses";
static const char s_misplaced_else_clause[] = "Misplaced else clause";
static const char s_bad_case_clause[] = "Bad case clause";
static const char s_bad_case_labels[] = "Bad case labels";
static const char s_duplicate_case_label[] = "Duplicate case label";
static const char s_bad_cond_clause[] = "Bad cond clause";
static const char s_missing_recipient[] = "Missing recipient in";
static const char s_bad_variable[] = "Bad variable";
static const char s_bad_bindings[] = "Bad bindings";
static const char s_bad_binding[] = "Bad binding";
static const char s_duplicate_binding[] = "Duplicate binding";
static const char s_bad_exit_clause[] = "Bad exit clause";
static const char s_bad_formals[] = "Bad formals";
static const char s_bad_formal[] = "Bad formal";
static const char s_duplicate_formal[] = "Duplicate formal";
static const char s_splicing[] = "Non-list result for unquote-splicing";
static const char s_bad_slot_number[] = "Bad slot number";


/* Signal a syntax error.  We distinguish between the form that caused the
 * error and the enclosing expression.  The error message will print out as
 * shown in the following pattern.  The file name and line number are only
 * given when they can be determined from the erroneous form or from the
 * enclosing expression.
 *
 * <filename>: In procedure memoization:
 * <filename>: In file <name>, line <nr>: <error-message> in <expression>.  */

SCM_SYMBOL (syntax_error_key, "syntax-error");

/* The prototype is needed to indicate that the function does not return.  */
static void
syntax_error (const char* const, const SCM, const SCM) SCM_NORETURN;

static void 
syntax_error (const char* const msg, const SCM form, const SCM expr)
{
  SCM msg_string = scm_from_locale_string (msg);
  SCM filename = SCM_BOOL_F;
  SCM linenr = SCM_BOOL_F;
  const char *format;
  SCM args;

  if (scm_is_pair (form))
    {
      filename = scm_source_property (form, scm_sym_filename);
      linenr = scm_source_property (form, scm_sym_line);
    }

  if (scm_is_false (filename) && scm_is_false (linenr) && scm_is_pair (expr))
    {
      filename = scm_source_property (expr, scm_sym_filename);
      linenr = scm_source_property (expr, scm_sym_line);
    }

  if (!SCM_UNBNDP (expr))
    {
      if (scm_is_true (filename))
	{
	  format = "In file ~S, line ~S: ~A ~S in expression ~S.";
	  args = scm_list_5 (filename, linenr, msg_string, form, expr);
	}
      else if (scm_is_true (linenr))
	{
	  format = "In line ~S: ~A ~S in expression ~S.";
	  args = scm_list_4 (linenr, msg_string, form, expr);
	}
      else
	{
	  format = "~A ~S in expression ~S.";
	  args = scm_list_3 (msg_string, form, expr);
	}
    }
  else
    {
      if (scm_is_true (filename))
	{
	  format = "In file ~S, line ~S: ~A ~S.";
	  args = scm_list_4 (filename, linenr, msg_string, form);
	}
      else if (scm_is_true (linenr))
	{
	  format = "In line ~S: ~A ~S.";
	  args = scm_list_3 (linenr, msg_string, form);
	}
      else
	{
	  format = "~A ~S.";
	  args = scm_list_2 (msg_string, form);
	}
    }

  scm_error (syntax_error_key, "memoization", format, args, SCM_BOOL_F);
}


/* Shortcut macros to simplify syntax error handling. */
#define ASSERT_SYNTAX(cond, message, form)		\
  { if (SCM_UNLIKELY (!(cond)))			\
      syntax_error (message, form, SCM_UNDEFINED); }
#define ASSERT_SYNTAX_2(cond, message, form, expr)	\
  { if (SCM_UNLIKELY (!(cond)))			\
      syntax_error (message, form, expr); }




/* {Evaluator memoized expressions}
 */

scm_t_bits scm_tc16_memoized;

#define MAKMEMO(n, args) 	(scm_cell (scm_tc16_memoized | ((n) << 16), (scm_t_bits)(args)))

#define MAKMEMO_BEGIN(exps) \
  MAKMEMO (SCM_M_BEGIN, exps)
#define MAKMEMO_IF(test, then, else_) \
  MAKMEMO (SCM_M_IF, scm_cons (test, scm_cons (then, else_)))
#define FIXED_ARITY(nreq) \
  scm_list_1 (SCM_I_MAKINUM (nreq))
#define REST_ARITY(nreq, rest) \
  scm_list_2 (SCM_I_MAKINUM (nreq), rest)
#define FULL_ARITY(nreq, rest, nopt, kw, inits, alt) \
  scm_list_n (SCM_I_MAKINUM (nreq), rest, SCM_I_MAKINUM (nopt), kw, inits, \
              alt, SCM_UNDEFINED)
#define MAKMEMO_LAMBDA(body, arity) \
  MAKMEMO (SCM_M_LAMBDA, (scm_cons (body, arity)))
#define MAKMEMO_LET(inits, body) \
  MAKMEMO (SCM_M_LET, scm_cons (inits, body))
#define MAKMEMO_QUOTE(exp) \
  MAKMEMO (SCM_M_QUOTE, exp)
#define MAKMEMO_DEFINE(var, val) \
  MAKMEMO (SCM_M_DEFINE, scm_cons (var, val))
#define MAKMEMO_DYNWIND(in, expr, out) \
  MAKMEMO (SCM_M_DYNWIND, scm_cons (in, scm_cons (expr, out)))
#define MAKMEMO_WITH_FLUIDS(fluids, vals, expr) \
  MAKMEMO (SCM_M_WITH_FLUIDS, scm_cons (fluids, scm_cons (vals, expr)))
#define MAKMEMO_APPLY(proc, args)\
  MAKMEMO (SCM_M_APPLY, scm_list_2 (proc, args))
#define MAKMEMO_CONT(proc) \
  MAKMEMO (SCM_M_CONT, proc)
#define MAKMEMO_CALL_WITH_VALUES(prod, cons) \
  MAKMEMO (SCM_M_CALL_WITH_VALUES, scm_cons (prod, cons))
#define MAKMEMO_CALL(proc, nargs, args) \
  MAKMEMO (SCM_M_CALL, scm_cons (proc, scm_cons (SCM_I_MAKINUM (nargs), args)))
#define MAKMEMO_LEX_REF(n) \
  MAKMEMO (SCM_M_LEXICAL_REF, SCM_I_MAKINUM (n))
#define MAKMEMO_LEX_SET(n, val) \
  MAKMEMO (SCM_M_LEXICAL_SET, scm_cons (SCM_I_MAKINUM (n), val))
#define MAKMEMO_TOP_REF(var) \
  MAKMEMO (SCM_M_TOPLEVEL_REF, var)
#define MAKMEMO_TOP_SET(var, val) \
  MAKMEMO (SCM_M_TOPLEVEL_SET, scm_cons (var, val))
#define MAKMEMO_MOD_REF(mod, var, public) \
  MAKMEMO (SCM_M_MODULE_REF, scm_cons (mod, scm_cons (var, public)))
#define MAKMEMO_MOD_SET(val, mod, var, public) \
  MAKMEMO (SCM_M_MODULE_SET, scm_cons (val, scm_cons (mod, scm_cons (var, public))))
#define MAKMEMO_PROMPT(tag, exp, handler) \
  MAKMEMO (SCM_M_PROMPT, scm_cons (tag, scm_cons (exp, handler)))


/* Primitives for the evaluator */
scm_t_bits scm_tc16_memoizer;
#define SCM_MEMOIZER_P(x) (SCM_SMOB_PREDICATE (scm_tc16_memoizer, (x)))
#define SCM_MEMOIZER(M) (SCM_SMOB_OBJECT_1 (M))



/* This table must agree with the list of M_ constants in memoize.h */
static const char *const memoized_tags[] =
{
  "begin",
  "if",
  "lambda",
  "let",
  "quote",
  "define",
  "dynwind",
  "with-fluids",
  "apply",
  "call/cc",
  "call-with-values",
  "call",
  "lexical-ref",
  "lexical-set!",
  "toplevel-ref",
  "toplevel-set!",
  "module-ref",
  "module-set!",
  "prompt",
};

static int
scm_print_memoized (SCM memoized, SCM port, scm_print_state *pstate)
{
  scm_puts ("#<memoized ", port);
  scm_write (scm_unmemoize_expression (memoized), port);
  scm_puts (">", port);
  return 1;
}

static SCM scm_m_at (SCM xorig, SCM env);
static SCM scm_m_atat (SCM xorig, SCM env);
static SCM scm_m_and (SCM xorig, SCM env);
static SCM scm_m_begin (SCM xorig, SCM env);
static SCM scm_m_cond (SCM xorig, SCM env);
static SCM scm_m_define (SCM x, SCM env);
static SCM scm_m_with_fluids (SCM xorig, SCM env);
static SCM scm_m_eval_when (SCM xorig, SCM env);
static SCM scm_m_if (SCM xorig, SCM env);
static SCM scm_m_lambda (SCM xorig, SCM env);
static SCM scm_m_lambda_star (SCM xorig, SCM env);
static SCM scm_m_case_lambda (SCM xorig, SCM env);
static SCM scm_m_case_lambda_star (SCM xorig, SCM env);
static SCM scm_m_let (SCM xorig, SCM env);
static SCM scm_m_letrec (SCM xorig, SCM env);
static SCM scm_m_letstar (SCM xorig, SCM env);
static SCM scm_m_or (SCM xorig, SCM env);
static SCM scm_m_quote (SCM xorig, SCM env);
static SCM scm_m_set_x (SCM xorig, SCM env);




static SCM
memoize_env_ref_macro (SCM env, SCM x)
{
  SCM var;
  for (; scm_is_pair (env); env = CDR (env))
    if (scm_is_eq (x, CAR (env)))
      return SCM_BOOL_F; /* lexical */

  var = scm_module_variable (env, x);
  if (scm_is_true (var) && scm_is_true (scm_variable_bound_p (var))
      && (scm_is_true (scm_macro_p (scm_variable_ref (var)))
          || SCM_MEMOIZER_P (scm_variable_ref (var))))
    return scm_variable_ref (var);
  else
    return SCM_BOOL_F; /* anything else */
}

static int
memoize_env_var_is_free (SCM env, SCM x)
{
  for (; scm_is_pair (env); env = CDR (env))
    if (scm_is_eq (x, CAR (env)))
      return 0; /* bound */
  return 1; /* free */
}

static int
memoize_env_lexical_index (SCM env, SCM x)
{
  int i = 0;
  for (; scm_is_pair (env); env = CDR (env), i++)
    if (scm_is_eq (x, CAR (env)))
      return i; /* bound */
  return -1; /* free */
}

static SCM
memoize_env_extend (SCM env, SCM vars)
{
  return scm_append (scm_list_2 (vars, env));
}

static SCM
memoize (SCM exp, SCM env)
{
  if (scm_is_pair (exp))
    {
      SCM car;
      scm_t_macro_primitive trans = NULL;
      SCM macro = SCM_BOOL_F, memoizer = SCM_BOOL_F;
      
      car = CAR (exp);
      if (scm_is_symbol (car))
        macro = memoize_env_ref_macro (env, car);
      
      if (scm_is_true (scm_macro_p (macro)))
        trans = scm_i_macro_primitive (macro);
      else if (SCM_MEMOIZER_P (macro))
        memoizer = SCM_MEMOIZER (macro);

      if (trans)
        return trans (exp, env);
      else
        {
          SCM args = SCM_EOL;
          int nargs = 0;
          SCM proc = CAR (exp);
          
          for (exp = CDR (exp); scm_is_pair (exp); exp = CDR (exp), nargs++)
            args = scm_cons (memoize (CAR (exp), env), args);
          if (scm_is_null (exp))
            {
              if (scm_is_true (memoizer))
                return scm_apply (memoizer, scm_reverse_x (args, SCM_UNDEFINED),
                                  SCM_EOL);
              else
                return MAKMEMO_CALL (memoize (proc, env),
                                     nargs,
                                     scm_reverse_x (args, SCM_UNDEFINED));
            }
        
          else
            syntax_error ("expected a proper list", exp, SCM_UNDEFINED);
        }
    }
  else if (scm_is_symbol (exp))
    {
      int i = memoize_env_lexical_index (env, exp);
      if (i < 0)
        return MAKMEMO_TOP_REF (exp);
      else
        return MAKMEMO_LEX_REF (i);
    }
  else
    return MAKMEMO_QUOTE (exp);
}

static SCM
memoize_exprs (SCM forms, const SCM env)
{
  SCM ret = SCM_EOL;

  for (; !scm_is_null (forms); forms = CDR (forms))
    ret = scm_cons (memoize (CAR (forms), env), ret);
  return scm_reverse_x (ret, SCM_UNDEFINED);
}

static SCM
memoize_sequence (const SCM forms, const SCM env)
{
  ASSERT_SYNTAX (scm_ilength (forms) >= 1, s_bad_expression,
                 scm_cons (scm_sym_begin, forms));
  if (scm_is_null (CDR (forms)))
    return memoize (CAR (forms), env);
  else
    return MAKMEMO_BEGIN (memoize_exprs (forms, env));
}



/* Memoization.  */

#define SCM_SYNTAX(RANAME, STR, CFN)  \
SCM_SNARF_HERE(static const char RANAME[]=STR)\
SCM_SNARF_INIT(scm_c_define (RANAME, scm_i_make_primitive_macro (RANAME, CFN)))


/* True primitive syntax */
SCM_SYNTAX (s_at, "@", scm_m_at);
SCM_SYNTAX (s_atat, "@@", scm_m_atat);
SCM_SYNTAX (s_begin, "begin", scm_m_begin);
SCM_SYNTAX (s_define, "define", scm_m_define);
SCM_SYNTAX (s_with_fluids, "with-fluids", scm_m_with_fluids);
SCM_SYNTAX (s_eval_when, "eval-when", scm_m_eval_when);
SCM_SYNTAX (s_if, "if", scm_m_if);
SCM_SYNTAX (s_lambda, "lambda", scm_m_lambda);
SCM_SYNTAX (s_let, "let", scm_m_let);
SCM_SYNTAX (s_quote, "quote", scm_m_quote);
SCM_SYNTAX (s_set_x, "set!", scm_m_set_x);

/* Convenient syntax during boot, expands to primitive syntax. Replaced after
   psyntax boots. */
SCM_SYNTAX (s_and, "and", scm_m_and);
SCM_SYNTAX (s_cond, "cond", scm_m_cond);
SCM_SYNTAX (s_letrec, "letrec", scm_m_letrec);
SCM_SYNTAX (s_letstar, "let*", scm_m_letstar);
SCM_SYNTAX (s_or, "or", scm_m_or);
SCM_SYNTAX (s_lambda_star, "lambda*", scm_m_lambda_star);
SCM_SYNTAX (s_case_lambda, "case-lambda", scm_m_case_lambda);
SCM_SYNTAX (s_case_lambda_star, "case-lambda*", scm_m_case_lambda_star);

SCM_GLOBAL_SYMBOL (scm_sym_apply, "apply");
SCM_GLOBAL_SYMBOL (scm_sym_arrow, "=>");
SCM_GLOBAL_SYMBOL (scm_sym_at, "@");
SCM_GLOBAL_SYMBOL (scm_sym_atat, "@@");
SCM_GLOBAL_SYMBOL (scm_sym_at_call_with_values, "@call-with-values");
SCM_GLOBAL_SYMBOL (scm_sym_atapply, "@apply");
SCM_GLOBAL_SYMBOL (scm_sym_atcall_cc, "@call-with-current-continuation");
SCM_GLOBAL_SYMBOL (scm_sym_begin, "begin");
SCM_GLOBAL_SYMBOL (scm_sym_case, "case");
SCM_GLOBAL_SYMBOL (scm_sym_cond, "cond");
SCM_GLOBAL_SYMBOL (scm_sym_define, "define");
SCM_GLOBAL_SYMBOL (scm_sym_at_dynamic_wind, "@dynamic-wind");
SCM_GLOBAL_SYMBOL (scm_sym_with_fluids, "with-fluids");
SCM_GLOBAL_SYMBOL (scm_sym_else, "else");
SCM_GLOBAL_SYMBOL (scm_sym_eval_when, "eval-when");
SCM_GLOBAL_SYMBOL (scm_sym_if, "if");
SCM_GLOBAL_SYMBOL (scm_sym_lambda, "lambda");
SCM_GLOBAL_SYMBOL (scm_sym_let, "let");
SCM_GLOBAL_SYMBOL (scm_sym_letrec, "letrec");
SCM_GLOBAL_SYMBOL (scm_sym_letstar, "let*");
SCM_GLOBAL_SYMBOL (scm_sym_or, "or");
SCM_GLOBAL_SYMBOL (scm_sym_at_prompt, "@prompt");
SCM_GLOBAL_SYMBOL (scm_sym_quote, "quote");
SCM_GLOBAL_SYMBOL (scm_sym_set_x, "set!");
SCM_SYMBOL (sym_lambda_star, "lambda*");
SCM_SYMBOL (sym_case_lambda, "case-lambda");
SCM_SYMBOL (sym_case_lambda_star, "case-lambda*");
SCM_SYMBOL (sym_eval, "eval");
SCM_SYMBOL (sym_load, "load");

SCM_GLOBAL_SYMBOL (scm_sym_unquote, "unquote");
SCM_GLOBAL_SYMBOL (scm_sym_quasiquote, "quasiquote");
SCM_GLOBAL_SYMBOL (scm_sym_uq_splicing, "unquote-splicing");

SCM_KEYWORD (kw_allow_other_keys, "allow-other-keys");
SCM_KEYWORD (kw_optional, "optional");
SCM_KEYWORD (kw_key, "key");
SCM_KEYWORD (kw_rest, "rest");


static SCM
scm_m_at (SCM expr, SCM env SCM_UNUSED)
{
  ASSERT_SYNTAX (scm_ilength (expr) == 3, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (CADR (expr)) > 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_is_symbol (CADDR (expr)), s_bad_expression, expr);

  return MAKMEMO_MOD_REF (CADR (expr), CADDR (expr), SCM_BOOL_T);
}

static SCM
scm_m_atat (SCM expr, SCM env SCM_UNUSED)
{
  ASSERT_SYNTAX (scm_ilength (expr) == 3, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (CADR (expr)) > 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_is_symbol (CADDR (expr)), s_bad_expression, expr);

  return MAKMEMO_MOD_REF (CADR (expr), CADDR (expr), SCM_BOOL_F);
}

static SCM
scm_m_and (SCM expr, SCM env)
{
  const SCM cdr_expr = CDR (expr);

  if (scm_is_null (cdr_expr))
    return MAKMEMO_QUOTE (SCM_BOOL_T);
  ASSERT_SYNTAX (scm_is_pair (cdr_expr), s_bad_expression, expr);

  if (scm_is_null (CDR (cdr_expr)))
    return memoize (CAR (cdr_expr), env);
  else
    return MAKMEMO_IF (memoize (CAR (cdr_expr), env),
                       scm_m_and (cdr_expr, env),
                       MAKMEMO_QUOTE (SCM_BOOL_F));
}

static SCM
scm_m_begin (SCM expr, SCM env)
{
  const SCM cdr_expr = CDR (expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 1, s_bad_expression, expr);
  return MAKMEMO_BEGIN (memoize_exprs (cdr_expr, env));
}

static SCM
scm_m_cond (SCM expr, SCM env)
{
  /* Check, whether 'else or '=> is a literal, i. e. not bound to a value. */
  const int else_literal_p = memoize_env_var_is_free (env, scm_sym_else);
  const int arrow_literal_p = memoize_env_var_is_free (env, scm_sym_arrow);

  const SCM clauses = CDR (expr);
  SCM clause_idx;
  SCM ret, loc;

  ASSERT_SYNTAX (scm_ilength (clauses) >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (clauses) >= 1, s_missing_clauses, expr);

  ret = scm_cons (SCM_UNDEFINED, MAKMEMO_QUOTE (SCM_UNSPECIFIED));
  loc = ret;

  for (clause_idx = clauses;
       !scm_is_null (clause_idx);
       clause_idx = CDR (clause_idx))
    {
      SCM test;

      const SCM clause = CAR (clause_idx);
      const long length = scm_ilength (clause);
      ASSERT_SYNTAX_2 (length >= 1, s_bad_cond_clause, clause, expr);

      test = CAR (clause);
      if (scm_is_eq (test, scm_sym_else) && else_literal_p)
	{
	  const int last_clause_p = scm_is_null (CDR (clause_idx));
          ASSERT_SYNTAX_2 (length >= 2,
                           s_bad_cond_clause, clause, expr);
          ASSERT_SYNTAX_2 (last_clause_p,
                           s_misplaced_else_clause, clause, expr);
          SCM_SETCDR (loc,
                      memoize (scm_cons (scm_sym_begin, CDR (clause)), env));
	}
      else if (length >= 2
               && scm_is_eq (CADR (clause), scm_sym_arrow)
               && arrow_literal_p)
        {
          SCM tmp = scm_gensym (scm_from_locale_string ("cond "));
          SCM i;
          SCM new_env = scm_cons (tmp, env);
          ASSERT_SYNTAX_2 (length > 2, s_missing_recipient, clause, expr);
          ASSERT_SYNTAX_2 (length == 3, s_extra_expression, clause, expr);
          i = MAKMEMO_IF (MAKMEMO_LEX_REF (0),
                          MAKMEMO_CALL (memoize (CADDR (clause),
                                                 scm_cons (tmp, new_env)),
                                        1,
                                        scm_list_1 (MAKMEMO_LEX_REF (0))),
                          MAKMEMO_QUOTE (SCM_UNSPECIFIED));
          SCM_SETCDR (loc, 
                      MAKMEMO_LET (scm_list_1 (memoize (CAR (clause), env)),
                                   i));
          env = new_env;
          loc = scm_last_pair (SCM_MEMOIZED_ARGS (i));
	}
      /* FIXME length == 1 case */
      else
        {
          SCM i = MAKMEMO_IF (memoize (CAR (clause), env),
                              memoize (scm_cons (scm_sym_begin, CDR (clause)), env),
                              MAKMEMO_QUOTE (SCM_UNSPECIFIED));
          SCM_SETCDR (loc, i);
          loc = scm_last_pair (SCM_MEMOIZED_ARGS (i));
        }
    }

  return CDR (ret);
}

/* According to Section 5.2.1 of R5RS we first have to make sure that the
   variable is bound, and then perform the `(set! variable expression)'
   operation.  However, EXPRESSION _can_ be evaluated before VARIABLE is
   bound.  This means that EXPRESSION won't necessarily be able to assign
   values to VARIABLE as in `(define foo (begin (set! foo 1) (+ foo 1)))'.  */
static SCM
scm_m_define (SCM expr, SCM env)
{
  const SCM cdr_expr = CDR (expr);
  SCM body;
  SCM variable;

  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 2, s_missing_expression, expr);
  ASSERT_SYNTAX (!scm_is_pair (env), s_bad_define, expr);

  body = CDR (cdr_expr);
  variable = CAR (cdr_expr);

  if (scm_is_pair (variable))
    {
      ASSERT_SYNTAX_2 (scm_is_symbol (CAR (variable)), s_bad_variable, variable, expr);
      return MAKMEMO_DEFINE (CAR (variable),
                             memoize (scm_cons (scm_sym_lambda,
                                                scm_cons (CDR (variable), body)),
                                      env));
    }
  ASSERT_SYNTAX_2 (scm_is_symbol (variable), s_bad_variable, variable, expr);
  ASSERT_SYNTAX (scm_ilength (body) == 1, s_expression, expr);
  return MAKMEMO_DEFINE (variable, memoize (CAR (body), env));
}

static SCM
scm_m_with_fluids (SCM expr, SCM env)
{
  SCM binds, fluids, vals;
  ASSERT_SYNTAX (scm_ilength (expr) >= 3, s_bad_expression, expr);
  binds = CADR (expr);
  ASSERT_SYNTAX_2 (scm_ilength (binds) >= 0, s_bad_bindings, binds, expr);
  for (fluids = SCM_EOL, vals = SCM_EOL;
       scm_is_pair (binds);
       binds = CDR (binds))
    {
      SCM binding = CAR (binds);
      ASSERT_SYNTAX_2 (scm_ilength (CAR (binds)) == 2, s_bad_binding,
                       binding, expr);
      fluids = scm_cons (memoize (CAR (binding), env), fluids);
      vals = scm_cons (memoize (CADR (binding), env), vals);
    }

  return MAKMEMO_WITH_FLUIDS (scm_reverse_x (fluids, SCM_UNDEFINED),
                              scm_reverse_x (vals, SCM_UNDEFINED),
                              memoize_sequence (CDDR (expr), env));
}

static SCM
scm_m_eval_when (SCM expr, SCM env)
{
  ASSERT_SYNTAX (scm_ilength (expr) >= 3, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (CADR (expr)) > 0, s_bad_expression, expr);

  if (scm_is_true (scm_memq (sym_eval, CADR (expr)))
      || scm_is_true (scm_memq (sym_load, CADR (expr))))
    return MAKMEMO_BEGIN (memoize_exprs (CDDR (expr), env));
  else
    return MAKMEMO_QUOTE (SCM_UNSPECIFIED);
}

static SCM
scm_m_if (SCM expr, SCM env SCM_UNUSED)
{
  const SCM cdr_expr = CDR (expr);
  const long length = scm_ilength (cdr_expr);
  ASSERT_SYNTAX (length == 2 || length == 3, s_expression, expr);
  return MAKMEMO_IF (memoize (CADR (expr), env),
                     memoize (CADDR (expr), env),
                     ((length == 3)
                      ? memoize (CADDDR (expr), env)
                      : MAKMEMO_QUOTE (SCM_UNSPECIFIED)));
}

/* A helper function for memoize_lambda to support checking for duplicate
 * formal arguments: Return true if OBJ is `eq?' to one of the elements of
 * LIST or to the CDR of the last cons.  Therefore, LIST may have any of the
 * forms that a formal argument can have:
 *   <rest>, (<arg1> ...), (<arg1> ...  .  <rest>) */
static int
c_improper_memq (SCM obj, SCM list)
{
  for (; scm_is_pair (list); list = CDR (list))
    {
      if (scm_is_eq (CAR (list), obj))
        return 1;
    }
  return scm_is_eq (list, obj);
}

static SCM
scm_m_lambda (SCM expr, SCM env SCM_UNUSED)
{
  SCM formals;
  SCM formals_idx;
  SCM formal_vars = SCM_EOL;
  SCM body;
  int nreq = 0;

  const SCM cdr_expr = CDR (expr);
  const long length = scm_ilength (cdr_expr);
  ASSERT_SYNTAX (length >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (length >= 2, s_missing_expression, expr);

  /* Before iterating the list of formal arguments, make sure the formals
   * actually are given as either a symbol or a non-cyclic list.  */
  formals = CAR (cdr_expr);
  if (scm_is_pair (formals))
    {
      /* Dirk:FIXME:: We should check for a cyclic list of formals, and if
       * detected, report a 'Bad formals' error.  */
    }
  else
    {
      ASSERT_SYNTAX_2 (scm_is_symbol (formals) || scm_is_null (formals),
                       s_bad_formals, formals, expr);
    }

  /* Now iterate the list of formal arguments to check if all formals are
   * symbols, and that there are no duplicates.  */
  formals_idx = formals;
  while (scm_is_pair (formals_idx))
    {
      const SCM formal = CAR (formals_idx);
      const SCM next_idx = CDR (formals_idx);
      ASSERT_SYNTAX_2 (scm_is_symbol (formal), s_bad_formal, formal, expr);
      ASSERT_SYNTAX_2 (!c_improper_memq (formal, next_idx),
                       s_duplicate_formal, formal, expr);
      nreq++;
      formal_vars = scm_cons (formal, formal_vars);
      formals_idx = next_idx;
    }
  ASSERT_SYNTAX_2 (scm_is_null (formals_idx) || scm_is_symbol (formals_idx),
                   s_bad_formal, formals_idx, expr);
  if (scm_is_symbol (formals_idx))
    formal_vars = scm_cons (formals_idx, formal_vars);

  body = memoize_sequence (CDDR (expr), memoize_env_extend (env, formal_vars));

  if (scm_is_symbol (formals_idx))
    return MAKMEMO_LAMBDA (body, REST_ARITY (nreq, SCM_BOOL_T));
  else
    return MAKMEMO_LAMBDA (body, FIXED_ARITY (nreq));
}

static SCM
scm_m_lambda_star (SCM expr, SCM env)
{
  SCM req, opt, kw, allow_other_keys, rest, formals, body;
  SCM inits, kw_indices;
  int nreq, nopt;

  const long length = scm_ilength (expr);
  ASSERT_SYNTAX (length >= 1, s_bad_expression, expr);
  ASSERT_SYNTAX (length >= 3, s_missing_expression, expr);

  formals = CADR (expr);
  body = CDDR (expr);

  nreq = nopt = 0;
  req = opt = kw = SCM_EOL;
  rest = allow_other_keys = SCM_BOOL_F;

  while (scm_is_pair (formals) && scm_is_symbol (CAR (formals)))
    {
      nreq++;
      req = scm_cons (CAR (formals), req);
      formals = scm_cdr (formals);
    }

  if (scm_is_pair (formals) && scm_is_eq (CAR (formals), kw_optional))
    {
      formals = CDR (formals);
      while (scm_is_pair (formals)
             && (scm_is_symbol (CAR (formals)) || scm_is_pair (CAR (formals))))
        {
          nopt++;
          opt = scm_cons (CAR (formals), opt);
          formals = scm_cdr (formals);
        }
    }
  
  if (scm_is_pair (formals) && scm_is_eq (CAR (formals), kw_key))
    {
      formals = CDR (formals);
      while (scm_is_pair (formals)
             && (scm_is_symbol (CAR (formals)) || scm_is_pair (CAR (formals))))
        {
          kw = scm_cons (CAR (formals), kw);
          formals = scm_cdr (formals);
        }
    }
  
  if (scm_is_pair (formals) && scm_is_eq (CAR (formals), kw_allow_other_keys))
    {
      formals = CDR (formals);
      allow_other_keys = SCM_BOOL_T;
    }
  
  if (scm_is_pair (formals) && scm_is_eq (CAR (formals), kw_rest))
    {
      if (scm_ilength (formals) != 2)
        syntax_error (s_bad_formals, CADR (expr), expr);
      else
        rest = CADR (formals);
    }
  else if (scm_is_symbol (formals))
    rest = formals;
  else if (!scm_is_null (formals))
    syntax_error (s_bad_formals, CADR (expr), expr);
  else
    rest = SCM_BOOL_F;
  
  /* Now, iterate through them a second time, building up an expansion-time
     environment, checking, expanding and canonicalizing the opt/kw init forms,
     and eventually memoizing the body as well. Note that the rest argument, if
     any, is expanded before keyword args, thus necessitating the second
     pass.

     Also note that the specific environment during expansion of init
     expressions here needs to coincide with the environment when psyntax
     expands. A lot of effort for something that is only used in the bootstrap
     memoizer, you say? Yes. Yes it is.
  */

  inits = SCM_EOL;

  /* nreq is already set, and req is already reversed: simply extend. */
  env = memoize_env_extend (env, req);
  
  /* Build up opt inits and env */
  opt = scm_reverse_x (opt, SCM_EOL);
  while (scm_is_pair (opt))
    {
      SCM x = CAR (opt);
      if (scm_is_symbol (x))
        inits = scm_cons (MAKMEMO_QUOTE (SCM_BOOL_F), inits);
      else if (scm_ilength (x) == 2 && scm_is_symbol (CAR (x)))
        inits = scm_cons (memoize (CADR (x), env), inits);
      else
        syntax_error (s_bad_formals, CADR (expr), expr);
      env = scm_cons (scm_is_symbol (x) ? x : CAR (x), env);
      opt = CDR (opt);
    }
      
  /* Process rest before keyword args */
  if (scm_is_true (rest))
    env = scm_cons (rest, env);

  /* Build up kw inits, env, and kw-indices alist */
  if (scm_is_null (kw))
    kw_indices = SCM_BOOL_F;
  else
    {
      int idx = nreq + nopt + (scm_is_true (rest) ? 1 : 0);

      kw_indices = SCM_EOL;
      kw = scm_reverse_x (kw, SCM_EOL);
      while (scm_is_pair (kw))
        {
          SCM x, sym, k, init;
          x = CAR (kw);
          if (scm_is_symbol (x))
            {
              sym = x;
              init = SCM_BOOL_F;
              k = scm_symbol_to_keyword (sym);
            }
          else if (scm_ilength (x) == 2 && scm_is_symbol (CAR (x)))
            {
              sym = CAR (x);
              init = CADR (x);
              k = scm_symbol_to_keyword (sym);
            }
          else if (scm_ilength (x) == 3 && scm_is_symbol (CAR (x))
                   && scm_is_keyword (CADDR (x)))
            {
              sym = CAR (x);
              init = CADR (x);
              k = CADDR (x);
            }
          else
            syntax_error (s_bad_formals, CADR (expr), expr);

          kw_indices = scm_acons (k, SCM_I_MAKINUM (idx++), kw_indices);
          inits = scm_cons (memoize (init, env), inits);
          env = scm_cons (sym, env);
          kw = CDR (kw);
        }
      kw_indices = scm_cons (allow_other_keys,
                             scm_reverse_x (kw_indices, SCM_UNDEFINED));
    }

  /* We should check for no duplicates, but given that psyntax does this
     already, we can punt on it here... */

  inits = scm_reverse_x (inits, SCM_UNDEFINED);
  body = memoize_sequence (body, env);

  if (scm_is_false (kw_indices) && scm_is_false (rest) && !nopt)
    return MAKMEMO_LAMBDA (body, FIXED_ARITY (nreq));
  if (scm_is_false (kw_indices) && !nopt)
    return MAKMEMO_LAMBDA (body, REST_ARITY (nreq, SCM_BOOL_T));
  else
    return MAKMEMO_LAMBDA (body, FULL_ARITY (nreq, rest, nopt, kw_indices, inits,
                                             SCM_BOOL_F));
}

static SCM
patch_case_lambda (SCM a, SCM b)
{
  SCM mx, body, rest, kw_indices, inits;
  int nreq, nopt;

  mx = SCM_SMOB_OBJECT_1 (a);
  body = CAR (mx);
  mx = CDR (mx);

  if (scm_is_null (CDR (mx)))
    {
      nreq = scm_to_int16 (CAR (mx));
      rest = SCM_BOOL_F;
      nopt = 0;
      kw_indices = SCM_BOOL_F;
      inits = SCM_EOL;
    }
  else if (scm_is_null (CDDR (mx)))
    {
      nreq = scm_to_int16 (CAR (mx));
      rest = CADR (mx);
      nopt = 0;
      kw_indices = SCM_BOOL_F;
      inits = SCM_EOL;
    }
  else
    {
      nreq = scm_to_int16 (CAR (mx));
      rest = CADR (mx);
      nopt = scm_to_int16 (CADDR (mx));
      kw_indices = CADDDR (mx);
      inits = CADR (CDDDR (mx));
    }

  return MAKMEMO_LAMBDA
    (body, FULL_ARITY (nreq, rest, nopt, kw_indices, inits, b));
}

static SCM
scm_m_case_lambda (SCM expr, SCM env)
{
  SCM ret, clauses;

  const long length = scm_ilength (expr);
  ASSERT_SYNTAX (length >= 1, s_bad_expression, expr);
  ASSERT_SYNTAX (length >= 2, s_missing_expression, expr);

  clauses = scm_reverse (CDR (expr));
  ret = SCM_BOOL_F;
  
  for (; scm_is_pair (clauses); clauses = CDR (clauses))
    ret = patch_case_lambda
      (scm_m_lambda (scm_cons (scm_sym_lambda, CAR (clauses)), env), ret);
  
  return ret;
}

static SCM
scm_m_case_lambda_star (SCM expr, SCM env)
{
  SCM ret, clauses;

  const long length = scm_ilength (expr);
  ASSERT_SYNTAX (length >= 1, s_bad_expression, expr);
  ASSERT_SYNTAX (length >= 2, s_missing_expression, expr);

  clauses = scm_reverse (CDR (expr));
  ret = SCM_BOOL_F;
  
  for (; scm_is_pair (clauses); clauses = CDR (clauses))
    ret = patch_case_lambda
      (scm_m_lambda_star (scm_cons (sym_lambda_star, CAR (clauses)), env), ret);
  
  return ret;
}

/* Check if the format of the bindings is ((<symbol> <init-form>) ...).  */
static void
check_bindings (const SCM bindings, const SCM expr)
{
  SCM binding_idx;

  ASSERT_SYNTAX_2 (scm_ilength (bindings) >= 0,
                   s_bad_bindings, bindings, expr);

  binding_idx = bindings;
  for (; !scm_is_null (binding_idx); binding_idx = CDR (binding_idx))
    {
      SCM name;         /* const */

      const SCM binding = CAR (binding_idx);
      ASSERT_SYNTAX_2 (scm_ilength (binding) == 2,
                       s_bad_binding, binding, expr);

      name = CAR (binding);
      ASSERT_SYNTAX_2 (scm_is_symbol (name), s_bad_variable, name, expr);
    }
}

/* The bindings, which must have the format ((v1 i1) (v2 i2) ... (vn in)), are
 * transformed to the lists (vn .. v2 v1) and (i1 i2 ... in). If a duplicate
 * variable name is detected, an error is signalled. */
static int
transform_bindings (const SCM bindings, const SCM expr,
                    SCM *const rvarptr, SCM *const initptr)
{
  SCM rvariables = SCM_EOL;
  SCM rinits = SCM_EOL;
  SCM binding_idx = bindings;
  int n = 0;
  for (; !scm_is_null (binding_idx); binding_idx = CDR (binding_idx))
    {
      const SCM binding = CAR (binding_idx);
      const SCM CDR_binding = CDR (binding);
      const SCM name = CAR (binding);
      ASSERT_SYNTAX_2 (scm_is_false (scm_c_memq (name, rvariables)),
                       s_duplicate_binding, name, expr);
      rvariables = scm_cons (name, rvariables);
      rinits = scm_cons (CAR (CDR_binding), rinits);
      n++;
    }
  *rvarptr = rvariables;
  *initptr = scm_reverse_x (rinits, SCM_UNDEFINED);
  return n;
}

/* This function is a helper function for memoize_let.  It transforms
 * (let name ((var init) ...) body ...) into
 * ((letrec ((name (lambda (var ...) body ...))) name) init ...)
 * and memoizes the expression.  It is assumed that the caller has checked
 * that name is a symbol and that there are bindings and a body.  */
static SCM
memoize_named_let (const SCM expr, SCM env)
{
  SCM rvariables;
  SCM inits;
  int nreq;

  const SCM cdr_expr = CDR (expr);
  const SCM name = CAR (cdr_expr);
  const SCM cddr_expr = CDR (cdr_expr);
  const SCM bindings = CAR (cddr_expr);
  check_bindings (bindings, expr);

  nreq = transform_bindings (bindings, expr, &rvariables, &inits);

  env = scm_cons (name, env);
  return MAKMEMO_LET
    (scm_list_1 (MAKMEMO_QUOTE (SCM_UNDEFINED)),
     MAKMEMO_BEGIN
     (scm_list_2 (MAKMEMO_LEX_SET
                  (0,
                   MAKMEMO_LAMBDA (memoize_sequence
                                   (CDDDR (expr),
                                    memoize_env_extend (env, rvariables)),
                                   FIXED_ARITY (nreq))),
                  MAKMEMO_CALL (MAKMEMO_LEX_REF (0),
                                nreq,
                                memoize_exprs (inits, env)))));
}

/* (let ((v1 i1) (v2 i2) ...) body) with variables v1 .. vn and initializers
 * i1 .. in is transformed to (#@let (vn ... v2 v1) (i1 i2 ...) body).  */
static SCM
scm_m_let (SCM expr, SCM env)
{
  SCM bindings;

  const SCM cdr_expr = CDR (expr);
  const long length = scm_ilength (cdr_expr);
  ASSERT_SYNTAX (length >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (length >= 2, s_missing_expression, expr);

  bindings = CAR (cdr_expr);
  if (scm_is_symbol (bindings))
    {
      ASSERT_SYNTAX (length >= 3, s_missing_expression, expr);
      return memoize_named_let (expr, env);
    }

  check_bindings (bindings, expr);
  if (scm_is_null (bindings))
    return memoize_sequence (CDDR (expr), env);
  else
    {
      SCM rvariables;
      SCM inits;
      transform_bindings (bindings, expr, &rvariables, &inits);
      return MAKMEMO_LET (memoize_exprs (inits, env),
                          memoize_sequence (CDDR (expr),
                                            memoize_env_extend (env, rvariables)));
    }
}

static SCM
scm_m_letrec (SCM expr, SCM env)
{
  SCM bindings;

  const SCM cdr_expr = CDR (expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 2, s_missing_expression, expr);

  bindings = CAR (cdr_expr);
  if (scm_is_null (bindings))
    return memoize_sequence (CDDR (expr), env);
  else
    {
      SCM rvariables;
      SCM inits;
      SCM v, i;
      SCM undefs = SCM_EOL;
      SCM vals = SCM_EOL;
      SCM sets = SCM_EOL;
      SCM new_env;
      int offset;
      int n = transform_bindings (bindings, expr, &rvariables, &inits);
      offset = n;
      new_env = memoize_env_extend (env, rvariables);
      for (v = scm_reverse (rvariables), i = inits; scm_is_pair (v);
           v = CDR (v), i = CDR (i), n--)
        {
          undefs = scm_cons (MAKMEMO_QUOTE (SCM_UNDEFINED), undefs);
          vals = scm_cons (memoize (CAR (i), new_env), vals);
          sets = scm_cons (MAKMEMO_LEX_SET ((n-1) + offset,
                                            MAKMEMO_LEX_REF (n-1)),
                           sets);
        }
      return MAKMEMO_LET
        (undefs,
         MAKMEMO_BEGIN (scm_list_2 (MAKMEMO_LET (scm_reverse (vals),
                                                 MAKMEMO_BEGIN (sets)),
                                    memoize_sequence (CDDR (expr),
                                                      new_env))));
    }
}

static SCM
scm_m_letstar (SCM expr, SCM env SCM_UNUSED)
{
  SCM bindings;

  const SCM cdr_expr = CDR (expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 2, s_missing_expression, expr);

  bindings = CAR (cdr_expr);
  if (scm_is_null (bindings))
    return memoize_sequence (CDDR (expr), env);
  else
    {
      SCM rvariables;
      SCM variables;
      SCM inits;
      SCM ret, loc;
      transform_bindings (bindings, expr, &rvariables, &inits);
      variables = scm_reverse (rvariables);
      ret = scm_cons (SCM_UNDEFINED, SCM_UNSPECIFIED);
      loc = ret;
      for (; scm_is_pair (variables);
           variables = CDR (variables), inits = CDR (inits))
        { SCM x = MAKMEMO_LET (scm_list_1 (memoize (CAR (inits), env)),
                               MAKMEMO_QUOTE (SCM_UNSPECIFIED));
          SCM_SETCDR (loc, x);
          loc = scm_last_pair (SCM_MEMOIZED_ARGS (x));
          env = scm_cons (CAR (variables), env);
        }
      SCM_SETCDR (loc, memoize_sequence (CDDR (expr), env));
      return CDR (ret);
    }
}

static SCM
scm_m_or (SCM expr, SCM env SCM_UNUSED)
{
  SCM tail = CDR (expr);
  SCM ret, loc;
  const long length = scm_ilength (tail);

  ASSERT_SYNTAX (length >= 0, s_bad_expression, expr);

  ret = scm_cons (SCM_UNDEFINED, SCM_UNSPECIFIED);
  loc = ret;
  for (; scm_is_pair (tail); tail = CDR (tail))
    {
      SCM tmp = scm_gensym (scm_from_locale_string ("cond "));
      SCM x = MAKMEMO_IF (MAKMEMO_LEX_REF (0),
                          MAKMEMO_LEX_REF (0),
                          MAKMEMO_QUOTE (SCM_UNSPECIFIED));
      SCM new_env = scm_cons (tmp, env);
      SCM_SETCDR (loc, MAKMEMO_LET (scm_list_1 (memoize (CAR (tail),
                                                         env)),
                                    x));
      env = new_env;
      loc = scm_last_pair (SCM_MEMOIZED_ARGS (x));
    }
  SCM_SETCDR (loc, MAKMEMO_QUOTE (SCM_BOOL_F));
  return CDR (ret);
}

static SCM
scm_m_quote (SCM expr, SCM env SCM_UNUSED)
{
  SCM quotee;

  const SCM cdr_expr = CDR (expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) == 1, s_expression, expr);
  quotee = CAR (cdr_expr);
  return MAKMEMO_QUOTE (quotee);
}

static SCM
scm_m_set_x (SCM expr, SCM env)
{
  SCM variable;
  SCM vmem;

  const SCM cdr_expr = CDR (expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) >= 0, s_bad_expression, expr);
  ASSERT_SYNTAX (scm_ilength (cdr_expr) == 2, s_expression, expr);
  variable = CAR (cdr_expr);
  vmem = memoize (variable, env);
  
  switch (SCM_MEMOIZED_TAG (vmem))
    {
    case SCM_M_LEXICAL_REF:
      return MAKMEMO_LEX_SET (SCM_I_INUM (SCM_MEMOIZED_ARGS (vmem)),
                              memoize (CADDR (expr), env));
    case SCM_M_TOPLEVEL_REF:
      return MAKMEMO_TOP_SET (variable,
                              memoize (CADDR (expr), env));
    case SCM_M_MODULE_REF:
      return MAKMEMO_MOD_SET (memoize (CADDR (expr), env),
                              CAR (SCM_MEMOIZED_ARGS (vmem)),
                              CADR (SCM_MEMOIZED_ARGS (vmem)),
                              CDDR (SCM_MEMOIZED_ARGS (vmem)));
    default:
      syntax_error (s_bad_variable, variable, expr);
    }
}




SCM_DEFINE (scm_memoize_expression, "memoize-expression", 1, 0, 0, 
            (SCM exp),
	    "Memoize the expression @var{exp}.")
#define FUNC_NAME s_scm_memoize_expression
{
  return memoize (exp, scm_current_module ());
}
#undef FUNC_NAME




#define SCM_MAKE_MEMOIZER(STR, MEMOIZER, N)                             \
  (scm_cell (scm_tc16_memoizer,                                         \
             (scm_t_bits)(scm_c_make_gsubr (STR, N, 0, 0, MEMOIZER))))
#define SCM_DEFINE_MEMOIZER(STR, MEMOIZER, N)                           \
SCM_SNARF_INIT(scm_c_define (STR, SCM_MAKE_MEMOIZER (STR, MEMOIZER, N)))

static SCM m_apply (SCM proc, SCM args);
static SCM m_call_cc (SCM proc);
static SCM m_call_values (SCM prod, SCM cons);
static SCM m_dynamic_wind (SCM pre, SCM exp, SCM post);
static SCM m_prompt (SCM tag, SCM exp, SCM handler);

SCM_DEFINE_MEMOIZER ("@apply", m_apply, 2);
SCM_DEFINE_MEMOIZER ("@call-with-current-continuation", m_call_cc, 1);
SCM_DEFINE_MEMOIZER ("@call-with-values", m_call_values, 2);
SCM_DEFINE_MEMOIZER ("@dynamic-wind", m_dynamic_wind, 3);
SCM_DEFINE_MEMOIZER ("@prompt", m_prompt, 3);




static SCM m_apply (SCM proc, SCM args)
#define FUNC_NAME "@apply"
{
  SCM_VALIDATE_MEMOIZED (1, proc);
  SCM_VALIDATE_MEMOIZED (2, args);
  return MAKMEMO_APPLY (proc, args);
}
#undef FUNC_NAME

static SCM m_call_cc (SCM proc)
#define FUNC_NAME "@call-with-current-continuation"
{
  SCM_VALIDATE_MEMOIZED (1, proc);
  return MAKMEMO_CONT (proc);
}
#undef FUNC_NAME

static SCM m_call_values (SCM prod, SCM cons)
#define FUNC_NAME "@call-with-values"
{
  SCM_VALIDATE_MEMOIZED (1, prod);
  SCM_VALIDATE_MEMOIZED (2, cons);
  return MAKMEMO_CALL_WITH_VALUES (prod, cons);
}
#undef FUNC_NAME

static SCM m_dynamic_wind (SCM in, SCM expr, SCM out)
#define FUNC_NAME "memoize-dynwind"
{
  SCM_VALIDATE_MEMOIZED (1, in);
  SCM_VALIDATE_MEMOIZED (2, expr);
  SCM_VALIDATE_MEMOIZED (3, out);
  return MAKMEMO_DYNWIND (in, expr, out);
}
#undef FUNC_NAME

static SCM m_prompt (SCM tag, SCM exp, SCM handler)
#define FUNC_NAME "@prompt"
{
  SCM_VALIDATE_MEMOIZED (1, tag);
  SCM_VALIDATE_MEMOIZED (2, exp);
  SCM_VALIDATE_MEMOIZED (3, handler);
  return MAKMEMO_PROMPT (tag, exp, handler);
}
#undef FUNC_NAME

SCM_DEFINE (scm_memoizer_p, "memoizer?", 1, 0, 0,
            (SCM x), "")
{
  return scm_from_bool (SCM_MEMOIZER_P (x));
}

SCM_DEFINE (scm_memoizer, "memoizer", 1, 0, 0,
            (SCM memoizer), "")
{
  SCM_ASSERT (SCM_MEMOIZER_P (memoizer), memoizer, 1, "memoizer?");
  return SCM_MEMOIZER (memoizer);
}




SCM_SYMBOL (sym_placeholder, "_");

static SCM unmemoize (SCM expr);

static SCM
unmemoize_exprs (SCM exprs)
{
  SCM ret, tail;
  if (scm_is_null (exprs))
    return SCM_EOL;
  ret = scm_list_1 (unmemoize (CAR (exprs)));
  tail = ret;
  for (exprs = CDR (exprs); !scm_is_null (exprs); exprs = CDR (exprs))
    {
      SCM_SETCDR (tail, scm_list_1 (unmemoize (CAR (exprs))));
      tail = CDR (tail);
    }
  return ret;
}

static SCM
unmemoize_bindings (SCM inits)
{
  SCM ret, tail;
  if (scm_is_null (inits))
    return SCM_EOL;
  ret = scm_list_1 (scm_list_2 (sym_placeholder, unmemoize (CAR (inits))));
  tail = ret;
  for (inits = CDR (inits); !scm_is_null (inits); inits = CDR (inits))
    {
      SCM_SETCDR (tail, scm_list_1 (scm_list_2 (sym_placeholder,
                                                unmemoize (CAR (inits)))));
      tail = CDR (tail);
    }
  return ret;
}

static SCM
unmemoize_lexical (SCM n)
{
  char buf[16];
  buf[15] = 0;
  snprintf (buf, 15, "<%u>", scm_to_uint32 (n));
  return scm_from_locale_symbol (buf);
}

static SCM
unmemoize (const SCM expr)
{
  SCM args;
  
  if (!SCM_MEMOIZED_P (expr))
    abort ();

  args = SCM_MEMOIZED_ARGS (expr);
  switch (SCM_MEMOIZED_TAG (expr))
    {
    case SCM_M_APPLY:
      return scm_cons (scm_sym_atapply, unmemoize_exprs (args));
    case SCM_M_BEGIN:
      return scm_cons (scm_sym_begin, unmemoize_exprs (args));
    case SCM_M_CALL:
      return scm_cons (unmemoize (CAR (args)), unmemoize_exprs (CDDR (args)));
    case SCM_M_CONT:
      return scm_list_2 (scm_sym_atcall_cc, unmemoize (args));
    case SCM_M_CALL_WITH_VALUES:
      return scm_list_3 (scm_sym_at_call_with_values,
                         unmemoize (CAR (args)), unmemoize (CDR (args)));
    case SCM_M_DEFINE:
      return scm_list_3 (scm_sym_define, CAR (args), unmemoize (CDR (args)));
    case SCM_M_DYNWIND:
      return scm_list_4 (scm_sym_at_dynamic_wind,
                         unmemoize (CAR (args)),
                         unmemoize (CADR (args)),
                         unmemoize (CDDR (args)));
    case SCM_M_WITH_FLUIDS:
      {
        SCM binds = SCM_EOL, fluids, vals;
        for (fluids = CAR (args), vals = CADR (args); scm_is_pair (fluids);
             fluids = CDR (fluids), vals = CDR (vals))
          binds = scm_cons (scm_list_2 (unmemoize (CAR (fluids)),
                                        unmemoize (CAR (vals))),
                            binds);
        return scm_list_3 (scm_sym_with_fluids,
                           scm_reverse_x (binds, SCM_UNDEFINED),
                           unmemoize (CDDR (args)));
      }
    case SCM_M_IF:
      return scm_list_4 (scm_sym_if, unmemoize (scm_car (args)),
                         unmemoize (scm_cadr (args)), unmemoize (scm_cddr (args)));
    case SCM_M_LAMBDA:
      if (scm_is_null (CDDR (args)))
        return scm_list_3 (scm_sym_lambda,
                           scm_make_list (CADR (args), sym_placeholder),
                           unmemoize (CAR (args)));
      else if (scm_is_null (CDDDR (args)))
        {
          SCM formals = scm_make_list (CADR (args), sym_placeholder);
          return scm_list_3 (scm_sym_lambda,
                             scm_is_true (CADDR (args))
                             ? scm_cons_star (sym_placeholder, formals)
                             : formals,
                             unmemoize (CAR (args)));
        }
      else
        {
          SCM body = CAR (args), spec = CDR (args), alt, tail;
          
          alt = CADDR (CDDDR (spec));
          if (scm_is_true (alt))
            tail = CDR (unmemoize (alt));
          else
            tail = SCM_EOL;
          
          return scm_cons
            (sym_case_lambda_star,
             scm_cons (scm_list_2 (scm_list_5 (CAR (spec),
                                               CADR (spec),
                                               CADDR (spec),
                                               CADDDR (spec),
                                               unmemoize_exprs (CADR (CDDDR (spec)))),
                                   unmemoize (body)),
                       tail));
        }
    case SCM_M_LET:
      return scm_list_3 (scm_sym_let,
                         unmemoize_bindings (CAR (args)),
                         unmemoize (CDR (args)));
    case SCM_M_QUOTE:
      return scm_list_2 (scm_sym_quote, args);
    case SCM_M_LEXICAL_REF:
      return unmemoize_lexical (args);
    case SCM_M_LEXICAL_SET:
      return scm_list_3 (scm_sym_set_x, unmemoize_lexical (CAR (args)),
                         unmemoize (CDR (args)));
    case SCM_M_TOPLEVEL_REF:
      return args;
    case SCM_M_TOPLEVEL_SET:
      return scm_list_3 (scm_sym_set_x, CAR (args), unmemoize (CDR (args)));
    case SCM_M_MODULE_REF:
      return SCM_VARIABLEP (args) ? args
        : scm_list_3 (scm_is_true (CDDR (args)) ? scm_sym_at : scm_sym_atat,
                      scm_i_finite_list_copy (CAR (args)),
                      CADR (args));
    case SCM_M_MODULE_SET:
      return scm_list_3 (scm_sym_set_x,
                         SCM_VARIABLEP (CDR (args)) ? CDR (args)
                         : scm_list_3 (scm_is_true (CDDDR (args))
                                       ? scm_sym_at : scm_sym_atat,
                                       scm_i_finite_list_copy (CADR (args)),
                                       CADDR (args)),
                         unmemoize (CAR (args)));
    case SCM_M_PROMPT:
      return scm_list_4 (scm_sym_at_prompt,
                         unmemoize (CAR (args)),
                         unmemoize (CADR (args)),
                         unmemoize (CDDR (args)));
    default:
      abort ();
    }
}




SCM_DEFINE (scm_memoized_p, "memoized?", 1, 0, 0, 
            (SCM obj),
	    "Return @code{#t} if @var{obj} is memoized.")
#define FUNC_NAME s_scm_memoized_p
{
  return scm_from_bool (SCM_MEMOIZED_P (obj));
}
#undef FUNC_NAME

SCM_DEFINE (scm_unmemoize_expression, "unmemoize-expression", 1, 0, 0, 
            (SCM m),
	    "Unmemoize the memoized expression @var{m}.")
#define FUNC_NAME s_scm_unmemoize_expression
{
  SCM_VALIDATE_MEMOIZED (1, m);
  return unmemoize (m);
}
#undef FUNC_NAME

SCM_DEFINE (scm_memoized_expression_typecode, "memoized-expression-typecode", 1, 0, 0, 
            (SCM m),
	    "Return the typecode from the memoized expression @var{m}.")
#define FUNC_NAME s_scm_memoized_expression_typecode
{
  SCM_VALIDATE_MEMOIZED (1, m);

  /* The tag is a 16-bit integer so it fits in an inum.  */
  return SCM_I_MAKINUM (SCM_MEMOIZED_TAG (m));
}
#undef FUNC_NAME

SCM_DEFINE (scm_memoized_expression_data, "memoized-expression-data", 1, 0, 0, 
            (SCM m),
	    "Return the data from the memoized expression @var{m}.")
#define FUNC_NAME s_scm_memoized_expression_data
{
  SCM_VALIDATE_MEMOIZED (1, m);
  return SCM_MEMOIZED_ARGS (m);
}
#undef FUNC_NAME

SCM_DEFINE (scm_memoized_typecode, "memoized-typecode", 1, 0, 0, 
            (SCM sym),
	    "Return the memoized typecode corresponding to the symbol @var{sym}.")
#define FUNC_NAME s_scm_memoized_typecode
{
  int i;

  SCM_VALIDATE_SYMBOL (1, sym);

  for (i = 0; i < sizeof(memoized_tags)/sizeof(const char*); i++)
    if (strcmp (scm_i_symbol_chars (sym), memoized_tags[i]) == 0)
      return scm_from_int32 (i);

  return SCM_BOOL_F;
}
#undef FUNC_NAME

SCM_SYMBOL (scm_unbound_variable_key, "unbound-variable");
static void error_unbound_variable (SCM symbol) SCM_NORETURN;
static void error_unbound_variable (SCM symbol)
{
  scm_error (scm_unbound_variable_key, NULL, "Unbound variable: ~S",
	     scm_list_1 (symbol), SCM_BOOL_F);
}

SCM_DEFINE (scm_memoize_variable_access_x, "memoize-variable-access!", 2, 0, 0, 
            (SCM m, SCM mod),
	    "Look up and cache the variable that @var{m} will access, returning the variable.")
#define FUNC_NAME s_scm_memoize_variable_access_x
{
  SCM mx;
  SCM_VALIDATE_MEMOIZED (1, m);
  mx = SCM_MEMOIZED_ARGS (m);
  switch (SCM_MEMOIZED_TAG (m))
    {
    case SCM_M_TOPLEVEL_REF:
      if (SCM_VARIABLEP (mx))
        return mx;
      else
        {
          SCM var = scm_module_variable (mod, mx);
          if (scm_is_false (var) || scm_is_false (scm_variable_bound_p (var)))
            error_unbound_variable (mx);
          SCM_SET_SMOB_OBJECT (m, var);
          return var;
        }

    case SCM_M_TOPLEVEL_SET:
      {
        SCM var = CAR (mx);
        if (SCM_VARIABLEP (var))
          return var;
        else
          {
            var = scm_module_variable (mod, var);
            if (scm_is_false (var))
              error_unbound_variable (CAR (mx));
            SCM_SETCAR (mx, var);
            return var;
          }
      }

    case SCM_M_MODULE_REF:
      if (SCM_VARIABLEP (mx))
        return mx;
      else
        {
          SCM var;
          mod = scm_resolve_module (CAR (mx));
          if (scm_is_true (CDDR (mx)))
            mod = scm_module_public_interface (mod);
          var = scm_module_lookup (mod, CADR (mx));
          if (scm_is_false (scm_variable_bound_p (var)))
            error_unbound_variable (CADR (mx));
          SCM_SET_SMOB_OBJECT (m, var);
          return var;
        }

    case SCM_M_MODULE_SET:
      /* FIXME: not quite threadsafe */
      if (SCM_VARIABLEP (CDR (mx)))
        return CDR (mx);
      else
        {
          SCM var;
          mod = scm_resolve_module (CADR (mx));
          if (scm_is_true (CDDDR (mx)))
            mod = scm_module_public_interface (mod);
          var = scm_module_lookup (mod, CADDR (mx));
          SCM_SETCDR (mx, var);
          return var;
        }

    default:
      scm_wrong_type_arg (FUNC_NAME, 1, m);
      return SCM_BOOL_F;
    }
}
#undef FUNC_NAME




void
scm_init_memoize ()
{
  scm_tc16_memoized = scm_make_smob_type ("%memoized", 0);
  scm_set_smob_mark (scm_tc16_memoized, scm_markcdr);
  scm_set_smob_print (scm_tc16_memoized, scm_print_memoized);

  scm_tc16_memoizer = scm_make_smob_type ("memoizer", 0);

#include "libguile/memoize.x"

  scm_c_define ("macroexpand",
                scm_variable_ref (scm_c_lookup ("memoize-expression")));
}

/*
  Local Variables:
  c-file-style: "gnu"
  End:
*/
