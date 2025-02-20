/*-------------------------------------------------------------------------*
 * GNU Prolog                                                              *
 *                                                                         *
 * Part  : Prolog to WAM compiler                                          *
 * File  : read_file.pl                                                    *
 * Descr.: source file reading                                             *
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


/*-------------------------------------------------------------------------*
 * Data structures:                                                        *
 *                                                                         *
 * the stack of opened files (for nested includes):                        *
 *    global variable open_file_stack = [of(PlFile,Stream,ParIncLine),...] *
 *    PlFile    : the file                                                 *
 *    Stream    : input stream                                             *
 *    ParIncLine: line no of :- include in its parent (next of element)    *
 *    if foo:16 includes bar the stack = [of(bar,S2,16),of(foo,S1,0)]      *
 *                                                                         *
 * the context (where occurs an error):                                    *
 *    global variable where = OpenFileStack+(L1-L2)                        *
 *    L1: first line of the current clause (resp. directive).              *
 *    L2: last  line of the current clause (resp. directive).              *
 *                                                                         *
 * read_predicate(Pred,N,LSrcCl):                                          *
 *    the structure of the compiler is a repeat/fail loop on 1 predicate   *
 *    calling read_predicate(Pred,N,LSrcCl) to obtain next predicate.      *
 *    Pred  : predicate name (an atom).                                    *
 *    N     : arity (an integer >=0).                                      *
 *    LSrcCl: [SrcCl,...], list of source clauses, with                    *
 *     SrcCl: Where+Cl where Cl is the raw source clause read.             *
 *                                                                         *
 * Buffers for special predicate management (with assert/retract):         *
 *                                                                         *
 * buff_aux_pred(Pred,N,LSrcCl):                                           *
 *    records the clauses of an auxiliary predicate.                       *
 *    Asserted by Pass 1 (syntactic sugar removing) when splitting ;/2,etc.*
 *    Retracted at the very next invocation of read_predicate/3 to         *
 *    ensure that aux. predicates always follow their "father" predicate.  *
 *                                                                         *
 * buff_discontig_clause(Pred,N,SrcCl):                                    *
 *    records a clause of a discontiguous predicate (:- discontiguous).    *
 *    Each clause of a discontiguous predicate is asserted when it is read *
 *    When the end of file is reached all clauses of a discontiguous pred  *
 *    are grouped to return a list of source clauses LSrcCl.               *
 *    Thus discontiguous predicates are always compiled after other        *
 *    predicates.                                                          *
 *                                                                         *
 * buff_dyn_interf_clause(Pred,N,SrcCl):                                   *
 *    records the interface clause of a dynamic predicate (:- dynamic).    *
 *    This clause is of the form Head:- call(Head) and only ensures that   *
 *    an external invocation to this predicate will not need to know that  *
 *    it is dynamic (and should be called by call/1).                      *
 *    Asserted as soon as a :- dynamic directive is encountered.           *
 *    Retracted only when the end of file is reached. All other clauses of *
 *    a dynamic predicate give rise to a system executable directive to    *
 *    assert(z) it.                                                        *
 *                                                                         *
 * empty_dyn_pred(Pred,N,Where):                                           *
 *    asserted when the declaration of a dynamic predicate is encoutered   *
 *    (:- dynamic). Retracted when a clause of a dynamic predicate is read.*
 *    At the end, only contains dynamic predicate with no clauses.         *
 *    Used then to define the interface clause.                            *
 *                                                                         *
 * ensure_linked(Pred,N):                                                  *
 *    asserted for each Pred/N occuring in a :- ensure_linked directive.   *
 *                                                                         *
 * module_export(Pred,N,Module):                                           *
 *    asserted for each imported Pred/N from Module.                       *
 *    asserted for each exported Pred/N from Module.                       *
 *                                                                         *
 * meta_pred(Pred,N,MetaDecl):                                             *
 *    asserted for each meta_predicate declaration.                        *
 *                                                                         *
 * Buffers for executable directive management (with assert/retract):      *
 *                                                                         *
 * buff_exe_system(SrcDirec)                                               *
 *     SrcDirec = Where+Body (i.e. source directive).                      *
 *     records a system directive.                                         *
 *     Asserted for dynamic clauses (to assertz it), and to execute, at    *
 *     run-time, op/3 set_prolog_flag/2, char_conversion/2.                *
 *     Retracted only when the end of file has been reached to provide a   *
 *     predicate '$exe_system':- Body.                                     *
 *                                                                         *
 * buff_exe_user(SrcDirec)                                                 *
 *     records user defined directives (:- initialization).                *
 *     Asserted when a :- initialization declaration is encountered.       *
 *     Retracted just after all '$buff_exe_system' to ensure that any user *
 *     directive has the needed environment.                               *
 *                                                                         *
 * Buffers for special clause management (with assert/retract):            *
 *                                                                         *
 * buff_raw_clause(Cl,Where):                                              *
 *    to handle term_expansion/2 which can return a list of (raw) clauses. *
 *    The first one is handled directly, for the others we                 *
 *    assert/retract(buff_raw_clause(Cl,Where)).                           *
 *    Read at the very next invocation of get_next_clause/3.               *
 *                                                                         *
 * buff_src_clause(Pred,N,SrcCl):                                          *
 *    the reader needs a lookahead clause (to group clauses by predicates).*
 *    For such a clause we assert/retract(buff_src_clause(Pred,N,SrcCl)).  *
 *    Read at the next invocation of get_next_clause/3.                    *
 *-------------------------------------------------------------------------*/

:- op(200, fx, ?).

read_file_init :-
	pp_start,
	retractall(buff_raw_clause(_, _)),
	retractall(buff_src_clause(_, _, _)),
	retractall(buff_aux_pred(_, _, _)),
	retractall(buff_discontig_clause(_, _, _)),
	retractall(buff_dyn_interf_clause(_, _, _)),
	retractall(buff_exe_system(_)),
	retractall(buff_exe_user(_)),
	retractall(empty_dyn_pred(_, _, _)),
	retractall(ensure_linked(_, _)),
	retractall(pred_info(_, _, _)),
	retractall(module_export(_, _, _)),
	retractall(meta_pred(_, _, _)),
	g_assign(module, user),
	g_assign(module_already_seen, f),
	g_assign(default_kind, user),
	g_assign(open_file_stack, []),
	g_assign(where, 0),
	g_assign(syn_error_nb, 0),
	g_assign(in_lines, 0),
	g_assign(in_bytes, 0),
	g_assign(compiler_mode, default),
	set_pred_flag(dyn, term_expansion, 2).




read_file_init(PlFile) :-
	g_assign(reading_dyn_pred, f),
	g_assign(eof_reached, f),
	open_new_prolog_file(PlFile, 0). % 0 means parent = command-line




read_file_term(Bytes, Lines) :-
	g_read(in_bytes, Bytes),
	g_read(in_lines, Lines).




read_file_error_nb(SynErrNb) :-
	g_read(syn_error_nb, SynErrNb).




open_new_prolog_file(PlFile, ParIncLine) :-
	g_read(open_file_stack, OpenFileStack),
	open_new_prolog_file1(PlFile, OpenFileStack, PlFile1, Stream), !,
	g_assign(open_file_stack, [of(PlFile1, Stream, ParIncLine)|OpenFileStack]),
	(   peek_char(Stream, '#'), % ignore #! starting line (for shebang support)
	    repeat,
	    get_char(Stream, X),
	    (X = '\n' ; X = end_of_file)
	;
	    true
	).


open_new_prolog_file1(user, _, user, Stream) :-
	current_input(Stream).
	
open_new_prolog_file1(PlFile, _, PlFile, Stream) :-
%	format('~n*** Trying to open ~a~n', [PlFile]),
	catch(open(PlFile, read, Stream), error(existence_error(source_sink, _), _), fail).

open_new_prolog_file1(PlFile, OpenFileStack, PlFile1, Stream) :-
%	format('file stack: ~w~n', [OpenFileStack]),
	is_relative_file_name(PlFile),
	try_other_directory(OpenFileStack, PlFile, PlFile1, Stream).

open_new_prolog_file1(PlFile, _, _, _) :-
	throw(error(existence_error(source_sink, PlFile), open/3)).


	/* If an included file is not found try to look in "parents" (includers) path.
	 * If found return the new name (to have correct error msg and file_name in .wam)
	 */

try_other_directory([of(PlFile1, _, _)|_], PlFile, PlFile2, Stream) :-
	decompose_file_name(PlFile1, Directory, _, _),
	Directory \== '',
	atom_concat(Directory, PlFile, PlFile2),
%	format('   fail.~n+++ Trying to open ~a~n', [PlFile2]),
	catch(open(PlFile2, read, Stream), _, fail).

try_other_directory([_|OpenFileStack], PlFile, PlFile1, Stream) :-
	try_other_directory(OpenFileStack, PlFile, PlFile1, Stream).



	


close_last_prolog_file :-
	g_read(open_file_stack, [of(_, Stream, _)|OpenFileStack]),
	g_assign(open_file_stack, OpenFileStack),
	g_read(in_bytes, Bytes1),
	g_read(in_lines, Lines1),
	character_count(Stream, Bytes2),
	line_count(Stream, Lines2),
	Bytes is Bytes1 + Bytes2,
	Lines is Lines1 + Lines2,
	g_assign(in_bytes, Bytes),
	g_assign(in_lines, Lines),
	close(Stream).




          % Read of a predicate

read_predicate(Pred, N, LSrcCl) :-
	repeat,
	read_predicate1(Pred, N, LSrcCl),                % standard predicate
% !,
	(   g_read(reading_dyn_pred, f),
	    g_read(native_code, t) ->
	    read_predicate_next(Pred, N, LSrcCl)
	;   true
	).




read_predicate_next(Pred, N, LSrcCl) :-
	(test_pred_flag(dyn, Pred, N) ; test_pred_flag(multi, Pred, N)), !,
	LSrcCl = [Where + _|_],
	add_dyn_interf_clause(Pred, N, Where),
	create_exe_clauses_for_dyn_pred(LSrcCl, Pred, N),
	fail.                              % backtrack to repeat of main loop

read_predicate_next(Pred, N, LSrcCl) :-
	test_pred_flag(pub, Pred, N), !,
	create_exe_clauses_for_pub_pred(LSrcCl).

read_predicate_next(_, _, _).




read_predicate1(Pred, N, LSrcCl) :-
	retract(buff_aux_pred(Pred, N, LSrcCl)), !. % aux. pred (cf syn_sugar)

read_predicate1(Pred, N, LSrcCl) :-
	g_read(eof_reached, f), !,
	repeat,
	get_next_clause(Pred, N, SrcCl),
	SrcCl = _ + Cl,
	retractall(empty_dyn_pred(Pred, N, _)),
	(   test_pred_flag(discontig, Pred, N) ->
	    assertz(buff_discontig_clause(Pred, N, SrcCl)),
	    define_predicate(Pred, N),
	    fail                               % backtrack to read_predicate1
	;   true
	),
	(   test_pred_flag(def, Pred, N) ->
	    warn('discontiguous predicate ~q - clause ignored', [Pred / N]),
	    fail                               % backtrack to read_predicate1
	;   true
	), !,
	Cl \== end_of_file,                    % if end_of_file is read, fail
                                            % and backtrack to read_predicate
	define_predicate(Pred, N),
	group_clauses_by_pred(Pred, N, SrcCl, LSrcCl).

read_predicate1(Pred, N, [SrcCl|LSrcCl]) :-
	retract(buff_discontig_clause(Pred, N, SrcCl)), !,  % discontiguous pred
	collect_discontig_clauses(Pred, N, LSrcCl).

read_predicate1(Pred, N, [SrcCl]) :-
	g_assign(reading_dyn_pred, t),
	retract(buff_dyn_interf_clause(Pred, N, SrcCl)), !.     % dyn predicate

read_predicate1(Pred, N, [SrcCl]) :-
	g_assign(reading_dyn_pred, t),
	retract(empty_dyn_pred(Pred, N, Where)),        % empty dyn predicate
	define_predicate(Pred, N),
	(   g_read(native_code, t) ->
	    create_dyn_interf_clause(Pred, N, Where, SrcCl)
	;   SrcCl = Where + '$$empty$$predicate$$clause$$'
	), !.

read_predicate1(Pred, N, LSrcCl) :-
	g_assign(reading_dyn_pred, f),
	retract(buff_exe_system(Where + Body)),       % system exe directives
	Pred = '$exe_system',
	N = 0,
	LSrcCl = [Where + (Pred :- Body)], !.

read_predicate1(Pred, N, LSrcCl) :-
	retract(buff_exe_user(Where + Body)),           % user exe directives
	Pred = '$exe_user',
	N = 0,
	LSrcCl = [Where + (Pred :- Body)], !.

read_predicate1(Pred, N, LSrcCl) :-
	Pred = end_of_file,
	N = 0,
	LSrcCl = [], !.                                         % end of file





group_clauses_by_pred(Pred, N, SrcCl, [SrcCl|LSrcCl1]) :-
	get_next_clause(Pred1, N1, SrcCl1),
	(   Pred = Pred1,
	    N = N1 ->
	    group_clauses_by_pred(Pred1, N1, SrcCl1, LSrcCl1)
	;   LSrcCl1 = [],
	    (   Pred1 = end_of_file,
	        N1 = 0 ->
	        true
	    ;   asserta(buff_src_clause(Pred1, N1, SrcCl1))
	    )
	).




add_dyn_interf_clause(Pred, N, _) :-
	clause(buff_dyn_interf_clause(Pred, N, _), true), !.  % already asserted

add_dyn_interf_clause(Pred, N, Where) :-
	create_dyn_interf_clause(Pred, N, Where, SrcCl),
	assertz(buff_dyn_interf_clause(Pred, N, SrcCl)).




create_dyn_interf_clause(Pred, N, Where, SrcCl) :-
	length(LArgs, N),
	Head =.. [Pred|LArgs],
	SrcCl = Where + (Head :- call(Head)).



/* Version with findall */
collect_discontig_clauses(Pred, N, LSrcCl) :-
	findall(SrcCl, retract(buff_discontig_clause(Pred, N, SrcCl)), LSrcCl).

/* Version with retract recursive (do not omit the cut). Can be worse than 
 * with findall depending on how LDUV is implemented (see dynam_supp.c)
 */
/*
collect_discontig_clauses(Pred, N, [SrcCl|LSrcCl]) :-
	retract(buff_discontig_clause(Pred, N, SrcCl)), !,
	collect_discontig_clauses(Pred, N, LSrcCl).

collect_discontig_clauses(_, _, []).
*/



create_exe_clauses_for_dyn_pred([], _, _).

create_exe_clauses_for_dyn_pred([SrcCl|LSrcCl], Pred, N) :-
	SrcCl = Where + Cl,
	get_file_name(Where, PlFile),
	add_wrapper_to_dyn_clause(Pred, N, Where + Cl, AuxName),
	record_initialization(system, ('$call_c'('Pl_Emit_BC_Execute_Wrapper'(Pred, N, '&', AuxName, N), [by_value]), '$add_clause_term'(Cl, PlFile)), Where),
	create_exe_clauses_for_dyn_pred(LSrcCl, Pred, N).




create_exe_clauses_for_pub_pred([]).

create_exe_clauses_for_pub_pred([Where + Cl|LSrcCl]) :-
	get_file_name(Where, PlFile),
	record_initialization(system, '$add_clause_term'(Cl, PlFile), Where),
	create_exe_clauses_for_pub_pred(LSrcCl).




get_file_name([of(PlFile, _, _)|_] + _, PlFile).




get_next_clause(Pred, N, SrcCl) :-
	retract(buff_raw_clause(Cl, Where)), !,
	g_assign(where, Where),
	get_next_clause2(Cl, Where, [], Pred, N, SrcCl).


get_next_clause(Pred, N, SrcCl) :-
	retract(buff_src_clause(Pred, N, SrcCl)), !,
	SrcCl = Where + _,
	g_assign(where, Where).

get_next_clause(Pred, N, SrcCl) :-
	g_read(open_file_stack, OpenFileStack),
	OpenFileStack = [of(_, Stream, _)|_],
	'$catch'(read_term(Stream, Cl, [singletons(SingNames)]), error(syntax_error(Err), _), after_syn_error, any, 0, false),
	(   var(Err) ->
	    last_read_start_line_column(L1, _),
	    '$catch'('$expand_term1'(Cl, Cl1, TermExpans), error(Err, _), expand_error(Err, Cl, Cl1), any, 0, false),
	    stream_line_column(Stream, Line, Col),
	    (   Col = 1 ->
	        L2 is Line - 1
	    ;   L2 = Line
	    ),
	    Where = OpenFileStack + (L1 - L2),
	    g_assign(where, Where),
	    get_next_clause1(Cl1, Where, SingNames, Pred, N, SrcCl, TermExpans)
	;   get_next_clause(Pred, N, SrcCl)
	), !.


get_next_clause1(LstCl, Where, SingNames, Pred, N, SrcCl, TermExpans) :-
	nonvar(TermExpans),
	list(LstCl), !,		% term_expansion/2 can return a list of clauses
	(   LstCl = [] ->
	    get_next_clause(Pred, N, SrcCl)
	;   LstCl = [Cl|LstCl1],	    
	    (   member(Cl1, LstCl1),
	        assertz(buff_raw_clause(Cl1, Where)),
	        fail
	    ;
		get_next_clause2(Cl, Where, SingNames, Pred, N, SrcCl)
	    )
	).
	    
get_next_clause1(Cl, Where, SingNames, Pred, N, SrcCl, _) :-
	get_next_clause2(Cl, Where, SingNames, Pred, N, SrcCl).


		% return the (next) source clause SrcCl from the raw clause Cl

get_next_clause2(end_of_file, _, _, Pred, N, SrcCl) :-
	close_last_prolog_file,
	g_read(open_file_stack, OpenFileStack),
	(   OpenFileStack = [] ->
	    Pred = end_of_file,
	    N = 0,
	    SrcCl = _ + end_of_file,
	    g_assign(eof_reached, t),
	    pp_stop
	;
	    get_next_clause(Pred, N, SrcCl)
	).

get_next_clause2(T, _, _, Pred, N, SrcCl) :-
	pp_handle_term(T), !,	% if succeeds, read next clause (used to skip a clause)
	get_next_clause(Pred, N, SrcCl).

get_next_clause2((:- D), Where, SingNames, Pred, N, SrcCl) :- % other directives than pp
	display_singletons(SingNames),
	(   g_read(foreign_only, f)
	;   functor(D, foreign, _)
	),
	(   handle_directive(D, Where)
	;   error('invalid directive ~q', [D])
	), !,
	get_next_clause(Pred, N, SrcCl).

get_next_clause2(Cl, Where, SingNames, Pred, N, Where + Cl) :-
	(   Cl = (Head :- _) ->
	    true
	;   Cl = Head
	),
	check_callable(Head, head),
	check_head_is_module_free(Head),
	functor(Head, Pred, N),
	check_module_clash(Pred, N),
	check_predicate(Pred, N),
	display_singletons(SingNames),
	g_read(foreign_only, f),	    % fail if --foreign-only
	embed_clause(Pred, N, Cl), !.    % fail if embed only (no compiled)

		% ignore clause with --foreign-only or if embed only
get_next_clause2(_, _, _, Pred, N, SrcCl) :-
	get_next_clause(Pred, N, SrcCl).




after_syn_error :-
	g_read(syn_error_nb, SynErrNb),
	SynErrNb1 is SynErrNb + 1,
	g_assign(syn_error_nb, SynErrNb1),
	syntax_error_info(_, Line, Column, Msg),
	g_read(open_file_stack, OpenFileStack),
	g_assign(where, OpenFileStack + (Line - Line)),
	disp_msg('syntax error', Column, '~a', [Msg]).




expand_error(existence_error(procedure,'$expand_term1'/3), Cl, Cl) :-
	!. 	% when bootstrapped with an old version of gprolog not having '$expand_term1'

expand_error(Err, Cl, Cl) :-
	last_read_start_line_column(Line, _),
	g_read(open_file_stack, OpenFileStack),
	g_assign(where, OpenFileStack + (Line - Line)),
	warn('term rewriting (DCG or term_expansion/2) raised exception: ~q', [Err]).




display_singletons(SingNames) :-
	g_read(singl_warn, t), !,
	get_singletons(SingNames, Sing),
	(   Sing = [] ->
	    true
	;   warn('singleton variables ~w', [Sing])
	).

display_singletons(_).




get_singletons([], []).

get_singletons([X = _|SingNames], Sing1) :-
	(   sub_atom(X, 0, 1, _, '_') ->
	    Sing1 = Sing
	;   Sing1 = [X|Sing]
	),
	get_singletons(SingNames, Sing).




	/* +++ begin preprocessor management +++
	 *
	 * Use a stack PPStack = [ pp(Where, Action),... ]
	 *   Where: in_then / in_else
	 *   Action: keep, ignore, ignore_all (ignore in both then and else parts)
	 *
	 * Each read term is passed to pp_handle_term. In case of success, the
	 * the next term will be read (get_next_clause). This is used to handle 
	 * preprocessor directives and to ignore a term in a false branch of 
	 * the preprocessor as :- if(fail). term_is_ignored. :- endif.
	 */

pp_handle_term((:- D)) :-
	check_callable(D, directive),
	pp_handle_directive(D), !.

pp_handle_term(_) :-
	g_read(pp_stack, [pp(_, Action)|_]),
	Action \== keep.	% success = ignore term, failure = continue with term




pp_handle_directive(if(Goal)) :-
	g_read(pp_stack, PPStack),
	(   PPStack = [pp(_, Action)|_], Action \== keep -> % fail is pp_stack is empty
	    g_assign(pp_stack, [pp(in_then, ignore_all)|PPStack])
	;   pp_exec_if_goal(Goal, PPStack, if)
	).

pp_handle_directive(elif(Goal)) :-
	(   g_read(pp_stack, [pp(in_then, Action)|PPStack]) ->
	    (   Action \== ignore ->
		g_assign(pp_stack, [pp(in_then, ignore_all)|PPStack])
	    ;	pp_exec_if_goal(Goal, PPStack, elif)
	    )
	;
	    error('unexpected elif directive', [])
	).

pp_handle_directive(else) :-
	(   g_read(pp_stack, [pp(in_then, Action)|PPStack]) ->
	    (	Action = keep, Action1 = ignore
	    ;	Action = ignore, Action1 = keep
	    ;	Action = ignore_all, Action1 = ignore_all
	    ), !,
	    g_assign(pp_stack, [pp(in_else, Action1)|PPStack])
	;
	    error('unexpected else directive', [])
	).


pp_handle_directive(endif) :-
	(   g_read(pp_stack, [_|PPStack]) ->
	    g_assign(pp_stack, PPStack)
	;   error('unexpected endif directive', [])
	).




pp_exec_if_goal(Goal, PPStack, What) :-
	(   '$catch'(Goal, Err, (warn('~a directive raised exception: ~q', [What, Err]), fail),
		     What, 1, false) ->
	    g_assign(pp_stack, [pp(in_then, keep)|PPStack])
	;   g_assign(pp_stack, [pp(in_then, ignore)|PPStack])
	).




pp_start :-
	g_assign(pp_stack, []).




pp_stop :-
	g_read(pp_stack, []), !.

pp_stop :-
	error('endif directive expected', []).


	/* +++ end preprocessor management +++ */


:- discontiguous(handle_directive/3).

% in pp_handle_directive the directive has been checked: it is a callable

handle_directive(D, Where) :-
	D =.. [DName|DLst],	% to handle include(a/1, b/2, c/3) and include([a/1, b/2, c/3]) as lists
	handle_directive(DName, DLst, Where), !.

handle_directive(D, _) :-
	warn('invalid directive is ignored: ~q', [D]).




handle_directive(public, DLst, _) :-
	!,
	check_pi_list(DLst, f),
	set_flag_for_preds(DLst, pub).

handle_directive(dynamic, DLst, Where) :-
	!,
	check_pi_list(DLst, f),
	set_flag_for_preds(DLst, dyn),
	set_flag_for_preds(DLst, pub),
	add_empty_dyn(DLst, Where).

handle_directive(multifile, DLst, Where) :-
	!,
	check_pi_list(DLst, f),
	set_flag_for_preds(DLst, multi),
	add_empty_dyn(DLst, Where).

handle_directive(discontiguous, DLst, _) :-
	!,
	check_pi_list(DLst, f),
	set_flag_for_preds(DLst, discontig).

handle_directive(compiler_mode, [CompMode], _) :-
	!,
	(   memberchk(CompMode, [default, embed, compile]),
	    g_assign(compiler_mode, CompMode)
	;   memberchk(CompMode, [embed_compile, both, embed+compile, compile+embed]),
	    g_assign(compiler_mode, embed_compile)
	), !.

handle_directive(built_in, DLst, _) :-
	!,
	check_pi_list(DLst, t),
	(   DLst = [] ->
	    g_assign(default_kind, built_in)
	;   set_flag_for_preds(DLst, bpl)
	).

handle_directive(built_in_fd, DLst, _) :-
	!,
	check_pi_list(DLst, t),
	(   DLst = [] ->
	    g_assign(default_kind, built_in_fd)
	;   set_flag_for_preds(DLst, bfd)
	).

handle_directive(ensure_linked, DLst, _) :-
	!,
	check_pi_list(DLst, f),
	(   g_read(native_code, f) ->
	    warn('ensure_linked directive ignored in byte-code compilation mode', [])
	;   add_ensure_linked(DLst)
	).

handle_directive(ensure_loaded, DLst, _) :-
	!,
	check_pi_list(DLst, f),
	warn('ensure_loaded directive not supported - directive ignored', []).

handle_directive(encoding, _, _) :-
	!,
	warn('encoding directive not supported - directive ignored', []).

handle_directive(include, [PlFile], Where) :-
	!,
	Where = _ + (L1 - _),
	prolog_file_name(PlFile, PlFile1),
	open_new_prolog_file(PlFile1, L1).

handle_directive(op, [X, Y, Z], Where) :-
	!,
	handle_init_directive(op(X, Y, Z), system, Where).

handle_directive(char_conversion, [X, Y], Where) :-
	!,
	handle_init_directive(char_conversion(X, Y), system, Where).

handle_directive(set_prolog_flag, [X, Y], Where) :-
	!,
	handle_init_directive(set_prolog_flag(X, Y), system, Where),
	(   X = singleton_warning -> % singl_warn and singleton_warning flag can be decorelated (use only flag ?)
	    (   current_prolog_flag(singleton_warning, off) ->
		g_assign(singl_warn, f)
	    ;   g_assign(singl_warn, t)
	    )
	;   true
	),
	(   X = suspicious_warning -> % idem for susp_warn and suspicious_warning flag
	    (   current_prolog_flag(suspicious_warning, off) ->
		g_assign(susp_warn, f)
	    ;   g_assign(susp_warn, t)
	    )
	;   true
	).

handle_directive(initialization, [Goal], Where) :-
	!,
	handle_init_directive(Goal, user, Where).

handle_directive(module, [Module, DLst], _) :-
	!,
	check_pi_list(DLst, f),
	check_module_name(Module, f),
	(   g_read(module_already_seen, f) ->
	    g_assign(module_already_seen, t),
	    g_assign(module, Module),
	    add_module_export_info(DLst, Module)
	;
	    error('directive module/2 already declared', [])
	).

handle_directive(use_module, [Module, DLst], _) :-
	!,
	check_module_name(Module, f),
	add_module_export_info(DLst, Module).

handle_directive(meta_predicate, [MetaDecl], Where) :-
	!,
	(   callable(MetaDecl) ->
	    functor(MetaDecl, Pred, N),
	    set_flag_for_preds(Pred/N, meta),
	    assertz(meta_pred(Pred, N, MetaDecl)),
	    set_flag_for_preds('$prop_meta_pred'/3, discontig),
	    set_flag_for_preds('$prop_meta_pred'/3, multi),
	    assertz(buff_discontig_clause('$prop_meta_pred', 3, Where+'$prop_meta_pred'(Pred, N, MetaDecl)))
	;
	    error('invalide directive meta_predicate/1 ~q', [MetaDecl])
	).

handle_directive(foreign, [Template], Where) :-
	!,
	handle_directive(foreign, [Template, []], Where).

handle_directive(foreign, _, _) :-
	g_read(call_c, f), !,
	warn('foreign directive ignored (not allowed in this mode)', []).

handle_directive(foreign, [Template, Options], Where) :-
	!,
	callable(Template),
	list(Options),
	functor(Template, Pred, N),
	(   test_pred_flag(pub, Pred, N) ->
	    error('foreign predicate ~q should not be public/dynamic', [Pred/N])
	;   true),
	define_predicate(Pred, N),
	g_assign(foreign_fct_name, Pred),
	g_assign(foreign_return, boolean),
	g_assign(foreign_bip, Pred/N),
	g_assign(foreign_choice_size, -1),
	foreign_get_options(Options),
	foreign_check_types(0, N, Template, LType),
	g_read(foreign_fct_name, FctName),
	g_read(foreign_return, Return),
	g_read(foreign_bip, BipPred),
	g_read(foreign_choice_size, ChcSize),
	mk_no_internal_transf(args(FctName, Return, BipPred, ChcSize, LType), Args),
	functor(Head, Pred, N),
	SrcCl = Where + (Head :- '$foreign_call_c'(Args)),
	assertz(buff_discontig_clause(Pred, N, SrcCl)),
	add_ensure_linked('$force_foreign_link' / 0).
                     % to force the link of foreign.o and then foreign_supp.o




foreign_get_options([]).

foreign_get_options([X|Options]) :-
	foreign_get_options1(X), !,
	foreign_get_options(Options).


foreign_get_options1(fct_name(FctName)) :-
	atom(FctName),
	g_assign(foreign_fct_name, FctName).

foreign_get_options1(return(Return)) :-
	atom(Return),
	(   Return = none
	;   Return = boolean
	;   Return = jump
	),
	g_assign(foreign_return, Return).

foreign_get_options1(bip_name(X)) :-
	nonvar(X),
	X = none,
	g_assign(foreign_bip, ''/ -1).

foreign_get_options1(bip_name(BipName, BipArity)) :-
	atom(BipName),
	integer(BipArity),
	g_assign(foreign_bip, BipName/BipArity).

foreign_get_options1(bip_name(BipName/BipArity)) :-
	atom(BipName),
	integer(BipArity),
	g_assign(foreign_bip, BipName/BipArity).

foreign_get_options1(choice_size(ChcSize)) :-
	integer(ChcSize),
	g_assign(foreign_choice_size, ChcSize).




foreign_check_types(N, N, _, []) :-
	!.

foreign_check_types(I, N, Template, [(Mode, A)|LArgType]) :-
	I1 is I + 1,
	arg(I1, Template, Arg),
	nonvar(Arg),
	(   Arg = + A,
	    Mode = in
	;   Arg = - A,
	    Mode = out
	;   Arg = ? A,
	    Mode = in_out
	;   Arg = term,
	    A = term,
	    Mode = in
	),                                                     % term = +term
	nonvar(A),
	foreign_check_arg(A),
	foreign_check_types(I1, N, Template, LArgType).

foreign_check_arg(integer).
foreign_check_arg(positive).
foreign_check_arg(float).
foreign_check_arg(number).
foreign_check_arg(atom).
foreign_check_arg(boolean).
foreign_check_arg(char).
foreign_check_arg(in_char).
foreign_check_arg(code).
foreign_check_arg(in_code).
foreign_check_arg(byte).
foreign_check_arg(in_byte).
foreign_check_arg(string).
foreign_check_arg(chars).
foreign_check_arg(codes).
foreign_check_arg(term).




embed_clause(Pred, N, Cl) :-
	g_read(compiler_mode, CompMode),
	(   CompMode = default, Pred = term_expansion, N = 2
	;   CompMode = embed
	;   CompMode = embed_compile
	), !,
	(   test_pred_flag(embed, Pred, N) ->
	    true
	;   set_pred_flag(embed, Pred, N),
	    functor(Head, Pred, N),
	    retractall(Head)	% reinit when pl2wam executed under top-level
	),
	assertz(Cl),
	CompMode \== embed.	% fail if embed only (do not compile)

embed_clause(_, _, _).




handle_init_directive(Goal, SystemUser, Where) :-
	embed_directive(Goal, SystemUser), !, % fail if embed only
	record_initialization(SystemUser, Goal, Where).

handle_init_directive(_, _, _).
	


embed_directive(Goal, SystemUser) :-
	g_read(compiler_mode, CompMode),
	(   CompMode = default, SystemUser = system
	;   CompMode = embed
	;   CompMode = embed_compile
	), !,
	exec_directive(Goal),
	(   current_prolog_flag(singleton_warning, off) -> % in case of :- set_prolog_flag
	    g_assign(singl_warn, f)
	;   g_assign(singl_warn, t)
	),
	CompMode \== embed.	% fail if embed only (do not compile)

embed_directive(_, _).




exec_directive(Goal) :-
	check_callable(Goal, 'directive goal'),
	'$catch'(Goal, Err, exec_directive_exception(Goal, Err), any, 0, false), !.

exec_directive(Goal) :-
	warn('directive goal failed (~q)', [Goal]).


exec_directive_exception(Goal, Err) :-
	warn('directive goal failed (~q): raised exception ~q', [Goal, Err]).




record_initialization(system, Body, Where) :-
	assertz(buff_exe_system(Where + Body)).

record_initialization(user, Body, Where) :-
	assertz(buff_exe_user(Where + Body)).



	
add_empty_dyn([], _) :-
	!.

add_empty_dyn([P1|P2], Where) :-
	!,
	add_empty_dyn(P1, Where),
	add_empty_dyn(P2, Where).

add_empty_dyn((P1, P2), Where) :-
	!,
	add_empty_dyn(P1, Where),
	add_empty_dyn(P2, Where).

add_empty_dyn(Pred / N, Where) :-
	(   clause(empty_dyn_pred(Pred, N, _), _) ->
	    true
	;   assertz(empty_dyn_pred(Pred, N, Where))
	).




add_ensure_linked([]) :-
	!.

add_ensure_linked([P1|P2]) :-
	!,
	add_ensure_linked(P1),
	add_ensure_linked(P2).

add_ensure_linked((P1, P2)) :-
	!,
	add_ensure_linked(P1),
	add_ensure_linked(P2).

add_ensure_linked(Pred / N) :-
	clause(ensure_linked(Pred, N), true), !.

add_ensure_linked(Pred / N) :-
	assertz(ensure_linked(Pred, N)).




add_module_export_info([], _) :-
	!.

add_module_export_info([P1|P2], Module) :-
	!,
	add_module_export_info(P1, Module),
	add_module_export_info(P2, Module).

add_module_export_info((P1, P2), Module) :-
	!,
	add_module_export_info(P1, Module),
	add_module_export_info(P2, Module).

add_module_export_info(Pred / N, _) :-
	clause(module_export(Pred, N, Module1), true), !,
	error('predicate ~q already exported from module ~q', [Pred/N, Module1]).

add_module_export_info(Pred / N, Module) :-
	assertz(module_export(Pred, N, Module)),
	(   test_pred_flag(def, Pred, N) ->
	    check_module_clash(Pred, N)
	;   true
	).




	% check_pi_list(DLst, EmptyOK)
check_pi_list(DLst, _) :-
	var(DLst), !,
	error('directive argument is a variable', []).

check_pi_list([], f) :-
	!,
	error('directive argument missing', []).

check_pi_list([], _) :-
	!.

check_pi_list([P1|P2], _) :-
	!,
	check_pi_list(P1, t),
	check_pi_list(P2, t).

check_pi_list((P1, P2), _) :-
	!,
	check_pi_list(P1, t),
	check_pi_list(P2, t).

check_pi_list(Pred / N, _) :-
	atom(Pred),
	integer(N),
	N >= 0, !.

check_pi_list(P, _) :-
	error('directive argument is not a valid predicate indicator (~q)', [P]).




check_callable(X, _) :-
	callable(X), !.

check_callable(X, What) :-
	var(X), !,
	error('~a is a variable', [What]).

check_callable(X, What) :-
	error('~a is not a callable (~q)', [What, X]).




	% check_module_name(Module, VarOK)
check_module_name(Module, t) :-	
	var(Module), !.

check_module_name(Module, _) :-
	atom(Module), !.

check_module_name(Module, _) :-
	error('invalid module name (~q) should be an atom', [Module]).

/*
check_module_name(Module, _) :-
	atom(Module),
	\+ atom_property(Module, needs_quotes), !.

check_module_name(Module, _) :-
	error('invalid module name (~q) should only contain lower chars', [Module]).
*/




check_head_is_module_free(Module:Head) :-
	!,
	error('module qualification is not allowed for the head of a clause (~q)',
	      [Module:Head]).

check_head_is_module_free(_).




check_module_clash(Pred, N) :-  % Pred/N is defined in current module check for clash with an import
	clause(module_export(Pred, N, Module), true), !,
	g_read(module, Module1),
	Module \== Module1, !,
	error('clash on ~q - defined in module ~q (here) and imported from ~q',
	      [Pred / N, Module1, Module]).

check_module_clash(_, _).




get_owner_module(Pred, N, Module) :-
	clause(module_export(Pred, N, Module), true), !,
	Module \== system, !.

get_owner_module(_, _, _).



is_exported(Pred, N) :-
	clause(module_export(Pred, N, _), true), !.




get_module_of_cur_pred(Module) :-
	cur_pred(Pred, N),
	(   test_pred_flag(bpl, Pred, N) ->
	    Module = system
	;   test_pred_flag(bfd, Pred, N) ->
	    Module = system
	;   g_read(module, Module)
	).




set_flag_for_preds([], _) :-
	!.

set_flag_for_preds([P1|P2], Flag) :-
	!,
	set_flag_for_preds(P1, Flag),
	set_flag_for_preds(P2, Flag).

set_flag_for_preds((P1, P2), Flag) :-
	!,
	set_flag_for_preds(P1, Flag),
	set_flag_for_preds(P2, Flag).

set_flag_for_preds(Pred / N, Flag) :-
	set_flag_for_preds1(Flag, Pred, N).
	

set_flag_for_preds1(_, Pred, N) :-
	test_pred_flag(def, Pred, N), !,
	warn('directive occurs after definition of ~q - directive ignored',
	     [Pred / N]).

set_flag_for_preds1(-Flag, Pred, N) :-
	!,
	unset_pred_flag(Flag, Pred, N).

set_flag_for_preds1(bpl, Pred, N) :-
	unset_pred_flag(bfd, Pred, N),
	fail.

set_flag_for_preds1(bfd, Pred, N) :-
	unset_pred_flag(bpl, Pred, N),
	fail.

set_flag_for_preds1(bfd, Pred, N) :-
	unset_pred_flag(bpl, Pred, N),
	fail.

set_flag_for_preds1(Flag, Pred, N) :-
	set_pred_flag(Flag, Pred, N).




define_predicate(F, N) :-
	set_pred_flag(def, F, N),
	test_pred_flag(bpl, F, N), !.

define_predicate(F, N) :-
	test_pred_flag(bfd, F, N), !.

define_predicate(F, N) :-
	g_read(default_kind, built_in), !,
	set_pred_flag(bpl, F, N).

define_predicate(F, N) :-
	g_read(default_kind, built_in_fd), !,
	set_pred_flag(bfd, F, N).

define_predicate(_, _).




flag_bit(def, 0).
flag_bit(dyn, 1).
flag_bit(pub, 2).
flag_bit(bpl, 3).
flag_bit(bfd, 4).
flag_bit(discontig, 5).
flag_bit(need_cut_level, 6).
flag_bit(meta, 7).
flag_bit(multi, 8).
flag_bit(embed, 9).




/* Version with assert/retract */
set_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	(   retract(pred_info(F, N, InfoMask)), !
	;   InfoMask = 0
	), !,
	InfoMask1 is InfoMask \/ 1 << Bit,
	assertz(pred_info(F, N, InfoMask1)).




unset_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	retract(pred_info(F, N, InfoMask)), !,
	InfoMask1 is InfoMask /\ \ (1 << Bit),
	assertz(pred_info(F, N, InfoMask1)).

unset_pred_flag(_, _, _).




test_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	clause(pred_info(F, N, InfoMask), _), !,
	InfoMask /\ 1 << Bit > 0.




test_not_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	clause(pred_info(F, N, InfoMask), _), !,
	InfoMask /\ 1 << Bit =:= 0.

test_not_pred_flag(_, _, _).	% succeeds if not clause defined for F/N




/* Alternative version with g_assign (same speed) but would need a reset (like retractall)
set_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	f_n_to_key(F, N, Key),
	g_read(Key, InfoMask),
	InfoMask1 is InfoMask \/ 1 << Bit,
	g_assign(Key, InfoMask1).




unset_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	f_n_to_key(F, N, Key),
	g_read(Key, InfoMask),
	InfoMask1 is InfoMask /\ \ (1 << Bit),
	g_assign(Key, InfoMask1).




test_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	f_n_to_key(F, N, Key),
	g_read(Key, InfoMask),
	InfoMask /\ 1 << Bit > 0.



   
test_not_pred_flag(Flag, F, N) :-
	flag_bit(Flag, Bit),
	f_n_to_key(F, N, Key),
	g_read(Key, InfoMask),
	InfoMask /\ 1 << Bit =:= 0.




f_n_to_key(F, N, Key) :-
	format_to_atom(Key, '$~a/~d', [F, N]).
*/



check_predicate(Pred, N) :-
	g_read(redef_error, t),
	control_construct(Pred, N), !,
	error('redefining control construct ~q', [Pred / N]).

check_predicate(Pred, N) :-
	g_read(redef_error, t),
	bip(Pred, N), !,
	error('redefining built-in predicate ~q', [Pred / N]).

check_predicate(Pred, N) :-
	g_read(susp_warn, t),
	suspicious_predicate(Pred, N), !,
	warn('suspicious predicate ~q', [Pred / N]).

check_predicate(Pred, N) :-
	'$aux_name'(Pred), !,
	warn('using system auxiliary predicate ~q', [Pred / N]).

check_predicate(_, _).



bip(F, N) :-
	'$predicate_property1'(F, N, built_in), !.
/* no longer needed built_in_fd ==> built_in
bip(F, N) :-
	'$predicate_property1'(F, N, built_in_fd).
*/


control_construct(',', 2).
control_construct(;, 2).
control_construct(->, 2).
control_construct(!, 0).
control_construct(fail, 0).
control_construct(true, 0).
control_construct(call, 1).
control_construct(catch, 3).
control_construct(throw, 1).

%suspicious_predicate(',', 2).
%suspicious_predicate(;, 2).
%suspicious_predicate(->, 2).
%suspicious_predicate(!, 0).
suspicious_predicate(:, 2).
suspicious_predicate(:-, 1).
suspicious_predicate(:-, 2).
suspicious_predicate(-->, 2).
suspicious_predicate({}, X) :- X < 2.
suspicious_predicate(+, 2).
suspicious_predicate(-, 2).
suspicious_predicate(*, 2).
suspicious_predicate(/, 2).
suspicious_predicate(//, 2).




warn(Msg, LArg) :-
	disp_msg(warning, 0, Msg, LArg).




error(Msg, LArg) :-
	disp_msg('fatal error', 0, Msg, LArg),
	repeat,                                      % close all opened files
	(   close_last_prolog_file ->
	    fail
	;   !
	),
	abandon_exec.




abandon_exec :-
	abort.




disp_msg(MsgType, Column, Msg, LArg) :-
	numbervars(LArg),
	g_read(where, Where),
	(   Where = OpenFileStack + L12,
	    L12 = _ - _ ->
	    disp_file_name(OpenFileStack, _, _),
	    disp_lines(L12),
	    disp_column(Column)
	;   true
	),
	format('~a: ', [MsgType]),
	format(Msg, LArg),
	nl.




disp_file_name([], _, _) :-
	!.

disp_file_name([of(PlFile, _, ParIncLine1)|OpenFileStack], First, ParIncLine) :-
	disp_file_name(OpenFileStack, First, ParIncLine1),
	(   var(ParIncLine) ->
	    format('~a', [PlFile])
	;
	    (	var(First) ->
		First = f,
		Prefix = 'In file included'
	    ;	Prefix = '                '
	    ),
	    format('~a from ~a:~d~n', [Prefix, PlFile, ParIncLine])
	).




disp_lines(L - L) :-
	!,
	format(':~d', [L]).

disp_lines(L1 - L2) :-
	format(':~d-~d', [L1, L2]).



disp_column(Column) :-
	Column > 0, !,
	format(':~d: ', [Column]).

disp_column(_) :-
	write(': ').




          % Exception recovery

exception(error(syntax_error(_), _)) :-
	!,
	syntax_error_info(_, Line, Char, Msg),
	g_read(open_file_stack, OpenFileStack),
	g_assign(where, OpenFileStack + (Line - Line)),
	error('syntax error: ~a (char:~d)', [Msg, Char]).

exception(error(existence_error(source_sink, File), _)) :-
	!,
	error('cannot open file ~a - does not exist', [File]).

exception(error(permission_error(open, source_sink, File), _)) :-
	!,
	error('cannot open file ~a - permission error', [File]).

exception(abandon_exec) :-
	abort.

exception(Err) :-
	error('exception raised: ~q', [Err]).
