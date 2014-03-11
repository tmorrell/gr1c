/* rg_main.c -- main entry point for execution of rg.
 *
 *
 * SCL; 2012-2014.
 */


#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "common.h"
#include "logging.h"
#include "ptree.h"
#include "automaton.h"
#include "solve.h"
#include "patching.h"
#include "gr1c_util.h"
extern int yyparse( void );


/**************************
 **** Global variables ****/

extern ptree_t *evar_list;
extern ptree_t *svar_list;
extern ptree_t *env_init;
extern ptree_t *sys_init;
ptree_t *env_trans = NULL;  /* Built from components in env_trans_array. */
ptree_t *sys_trans = NULL;
extern ptree_t **env_goals;
extern ptree_t **sys_goals;
extern int num_egoals;
extern int num_sgoals;

extern ptree_t **env_trans_array;
extern ptree_t **sys_trans_array;
extern int et_array_len;
extern int st_array_len;

/**************************/


/* Output formats */
#define OUTPUT_FORMAT_TEXT 0
#define OUTPUT_FORMAT_TULIP 1
#define OUTPUT_FORMAT_DOT 2
#define OUTPUT_FORMAT_AUT 3

/* Runtime modes */
#define RG_MODE_SYNTAX 0
/* #define RG_MODE_REALIZABLE 1 */
#define RG_MODE_SYNTHESIS 2


extern anode_t *synthesize_reachgame_BDD( DdManager *manager,
										  int num_env, int num_sys,
										  DdNode *Entry, DdNode *Exit,
										  DdNode *etrans, DdNode *strans,
										  DdNode **egoals, DdNode *N_BDD,
										  unsigned char verbose );


int main( int argc, char **argv )
{
	FILE *fp;
	FILE *stdin_backup = NULL;
	byte run_option = RG_MODE_SYNTHESIS;
	bool help_flag = False;
	bool ptdump_flag = False;
	bool logging_flag = False;
	unsigned char init_flags = ALL_INIT;
	byte format_option = OUTPUT_FORMAT_TULIP;
	unsigned char verbose = 0;
	int input_index = -1;
	int output_file_index = -1;  /* For command-line flag "-o". */
	char dumpfilename[64];

	int i, j, var_index;
	ptree_t *tmppt;  /* General purpose temporary ptree pointer */
	DdNode **vars, **pvars;
	bool env_nogoal_flag = False;
	ptree_t *var_separator;

	DdManager *manager;
	anode_t *strategy = NULL;
	int num_env, num_sys;
	int original_num_env, original_num_sys;
	ptree_t *nonbool_var_list = NULL;

	DdNode *Entry, *Exit;
	DdNode *sinit, *einit, *etrans, *strans, **egoals;

	/* Look for flags in command-line arguments. */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (argv[i][1] == 'h') {
				help_flag = True;
			} else if (argv[i][1] == 'V') {
				printf( "rg (part of gr1c) " GR1C_VERSION "\n\n"
						GR1C_COPYRIGHT "\n" );
				return 0;
			} else if (argv[i][1] == 'v') {
				verbose = 1;
				j = 2;
				/* Only support up to "level 2" of verbosity */
				while (argv[i][j] == 'v' && j <= 2) {
					verbose++;
					j++;
				}
			} else if (argv[i][1] == 'l') {
				logging_flag = True;
			} else if (argv[i][1] == 's') {
				run_option = RG_MODE_SYNTAX;
			} else if (argv[i][1] == 'p') {
				ptdump_flag = True;
			} /*else if (argv[i][1] == 'r') {
				run_option = RG_MODE_REALIZABLE;
				} */
			else if (argv[i][1] == 't') {
				if (i == argc-1) {
					fprintf( stderr, "Invalid flag given. Try \"-h\".\n" );
					return 1;
				}
				if (!strncmp( argv[i+1], "txt", strlen( "txt" ) )) {
					format_option = OUTPUT_FORMAT_TEXT;
				} else if (!strncmp( argv[i+1], "tulip", strlen( "tulip" ) )) {
					format_option = OUTPUT_FORMAT_TULIP;
				} else if (!strncmp( argv[i+1], "dot", strlen( "dot" ) )) {
					format_option = OUTPUT_FORMAT_DOT;
				} else if (!strncmp( argv[i+1], "aut", strlen( "aut" ) )) {
					format_option = OUTPUT_FORMAT_AUT;
				} else {
					fprintf( stderr,
							 "Unrecognized output format. Try \"-h\".\n" );
					return 1;
				}
				i++;
			} else if (argv[i][1] == 'n') {
				if (i == argc-1) {
					fprintf( stderr, "Invalid flag given. Try \"-h\".\n" );
					return 1;
				}
				for (j = 0; j < strlen( argv[i+1] ); j++)
					argv[i+1][j] = tolower( argv[i+1][j] );
				if (!strncmp( argv[i+1], "all_init",
									 strlen( "all_init" ) )) {
					init_flags = ALL_INIT;
				} else {
					fprintf( stderr,
							 "Unrecognized init flags. Try \"-h\".\n" );
					return 1;
				}
				i++;
			} else if (argv[i][1] == 'o') {
				if (i == argc-1) {
					fprintf( stderr, "Invalid flag given. Try \"-h\".\n" );
					return 1;
				}
				output_file_index = i+1;
				i++;
			} else {
				fprintf( stderr, "Invalid flag given. Try \"-h\".\n" );
				return 1;
			}
		} else if (input_index < 0) {
			/* Use first non-flag argument as filename whence to read
			   specification. */
			input_index = i;
		}
	}

	if (help_flag) {
		/* Split among printf() calls to conform with ISO C90 string length */
		printf( "Usage: %s [-hVvls] [-t TYPE] [-o FILE] [FILE]\n\n"
				"  -h        this help message\n"
				"  -V        print version and exit\n"
				"  -v        be verbose\n"
				"  -l        enable logging\n"
				"  -t TYPE   strategy output format; default is \"tulip\";\n"
				"            supported formats: txt, dot, aut, tulip\n", argv[0] );
		printf( "  -n INIT     initial condition interpretation; (not case sensitive)\n"
				"              one of\n"
				"                  ALL_INIT (default)\n"
				"  -s        only check specification syntax (return -1 on error)\n"
/*				"  -r        only check realizability; do not synthesize strategy\n"
				"            (return 0 if realizable, -1 if not)\n" */
				"  -o FILE   output strategy to FILE, rather than stdout (default)\n" );
		return 1;
	}

	if (logging_flag) {
		openlogfile( "rg" );
		verbose = 1;
	} else {
		setlogstream( stdout );
		setlogopt( LOGOPT_NOTIME );
	}

	/* If filename for specification given at command-line, then use
	   it.  Else, read from stdin. */
	if (input_index > 0) {
		fp = fopen( argv[input_index], "r" );
		if (fp == NULL) {
			perror( "rg, fopen" );
			return -1;
		}
		stdin_backup = stdin;
		stdin = fp;
	}

	/* Parse the specification. */
	if (verbose)
		logprint( "Parsing input..." );
	if (yyparse())
		return -1;
	if (verbose)
		logprint( "Done." );
	if (stdin_backup != NULL) {
		stdin = stdin_backup;
	}

	if (num_sgoals > 1) {
		fprintf( stderr,
				 "Syntax error: reachability game specification has more"
				 " than 1 system goal.\n" );
		return -1;
	}

	if (check_gr1c_form( evar_list, svar_list, env_init, sys_init,
						 env_trans_array, et_array_len,
						 sys_trans_array, st_array_len,
						 env_goals, num_egoals, sys_goals, num_sgoals,
						 init_flags ) < 0)
		return -1;

	if (run_option == RG_MODE_SYNTAX)
		return 0;

	/* Close input file, if opened. */
	if (input_index > 0)
		fclose( fp );

	/* Omission implies empty. */
	if (et_array_len == 0) {
		et_array_len = 1;
		env_trans_array = malloc( sizeof(ptree_t *) );
		if (env_trans_array == NULL) {
			perror( "gr1c, malloc" );
			return -1;
		}
		*env_trans_array = init_ptree( PT_CONSTANT, NULL, 1 );
	}
	if (st_array_len == 0) {
		st_array_len = 1;
		sys_trans_array = malloc( sizeof(ptree_t *) );
		if (sys_trans_array == NULL) {
			perror( "gr1c, malloc" );
			return -1;
		}
		*sys_trans_array = init_ptree( PT_CONSTANT, NULL, 1 );
	}

	/* Number of variables, before expansion of those that are nonboolean */
	original_num_env = tree_size( evar_list );
	original_num_sys = tree_size( svar_list );


	if (ptdump_flag) {
		tree_dot_dump( env_init, "env_init_ptree.dot" );
		tree_dot_dump( sys_init, "sys_init_ptree.dot" );

		for (i = 0; i < et_array_len; i++) {
			snprintf( dumpfilename, sizeof(dumpfilename),
					  "env_trans%05d_ptree.dot", i );
			tree_dot_dump( *(env_trans_array+i), dumpfilename );
		}
		for (i = 0; i < st_array_len; i++) {
			snprintf( dumpfilename, sizeof(dumpfilename),
					  "sys_trans%05d_ptree.dot", i );
			tree_dot_dump( *(sys_trans_array+i), dumpfilename );
		}

		if (num_egoals > 0) {
			for (i = 0; i < num_egoals; i++) {
				snprintf( dumpfilename, sizeof(dumpfilename),
						 "env_goal%05d_ptree.dot", i );
				tree_dot_dump( *(env_goals+i), dumpfilename );
			}
		}
		if (num_sgoals > 0)
			tree_dot_dump( *sys_goals, "sys_goal_ptree.dot" );

		var_index = 0;
		printf( "Environment variables (indices; domains): " );
		if (evar_list == NULL) {
			printf( "(none)" );
		} else {
			tmppt = evar_list;
			while (tmppt) {
				if (tmppt->value == -1) {  /* Boolean */
					if (tmppt->left == NULL) {
						printf( "%s (%d; bool)", tmppt->name, var_index );
					} else {
						printf( "%s (%d; bool), ", tmppt->name, var_index);
					}
				} else {
					if (tmppt->left == NULL) {
						printf( "%s (%d; {0..%d})",
								tmppt->name, var_index, tmppt->value );
					} else {
						printf( "%s (%d; {0..%d}), ",
								tmppt->name, var_index, tmppt->value );
					}
				}
				tmppt = tmppt->left;
				var_index++;
			}
		}
		printf( "\n\n" );

		printf( "System variables (indices; domains): " );
		if (svar_list == NULL) {
			printf( "(none)" );
		} else {
			tmppt = svar_list;
			while (tmppt) {
				if (tmppt->value == -1) {  /* Boolean */
					if (tmppt->left == NULL) {
						printf( "%s (%d; bool)", tmppt->name, var_index );
					} else {
						printf( "%s (%d; bool), ", tmppt->name, var_index );
					}
				} else {
					if (tmppt->left == NULL) {
						printf( "%s (%d; {0..%d})",
								tmppt->name, var_index, tmppt->value );
					} else {
						printf( "%s (%d; {0..%d}), ",
								tmppt->name, var_index, tmppt->value );
					}
				}
				tmppt = tmppt->left;
				var_index++;
			}
		}
		printf( "\n\n" );

		printf( "ENV INIT:  " );
		print_formula( env_init, stdout );
		printf( "\n" );

		printf( "SYS INIT:  " );
		print_formula( sys_init, stdout );
		printf( "\n" );

		printf( "ENV TRANS:  [] " );
		print_formula( env_trans, stdout );
		printf( "\n" );

		printf( "SYS TRANS:  [] " );
		print_formula( sys_trans, stdout );
		printf( "\n" );

		printf( "ENV GOALS:  " );
		if (num_egoals == 0) {
			printf( "(none)" );
		} else {
			printf( "[]<> " );
			print_formula( *env_goals, stdout );
			for (i = 1; i < num_egoals; i++) {
				printf( " & []<> " );
				print_formula( *(env_goals+i), stdout );
			}
		}
		printf( "\n" );

		printf( "SYS GOAL:  " );
		if (num_sgoals == 0) {
			printf( "(none)" );
		} else {
			printf( "<> " );
			print_formula( *sys_goals, stdout );
		}
		printf( "\n" );
	}

	if (expand_nonbool_GR1( evar_list, svar_list, &env_init, &sys_init,
							&env_trans_array, &et_array_len,
							&sys_trans_array, &st_array_len,
							&env_goals, num_egoals, &sys_goals, num_sgoals,
							init_flags, verbose ) < 0)
		return -1;
	nonbool_var_list = expand_nonbool_variables( &evar_list, &svar_list,
												 verbose );

	/* Merge component safety (transition) formulas */
	if (et_array_len > 1) {
		env_trans = merge_ptrees( env_trans_array, et_array_len, PT_AND );
	} else if (et_array_len == 1) {
		env_trans = *env_trans_array;
	} else {  /* No restrictions on transitions. */
		fprintf( stderr,
				 "Syntax error: GR(1) specification is missing environment"
				 " transition rules.\n" );
		return -1;
	}
	if (st_array_len > 1) {
		sys_trans = merge_ptrees( sys_trans_array, st_array_len, PT_AND );
	} else if (st_array_len == 1) {
		sys_trans = *sys_trans_array;
	} else {  /* No restrictions on transitions. */
		fprintf( stderr,
				 "Syntax error: GR(1) specification is missing system"
				 " transition rules.\n" );
		return -1;
	}

	if (verbose > 1)
		/* Dump the spec to show results of conversion (if any). */
		print_GR1_spec( evar_list, svar_list, env_init, sys_init,
						env_trans_array, et_array_len,
						sys_trans_array, st_array_len,
						env_goals, num_egoals, sys_goals, num_sgoals, NULL );


	num_env = tree_size( evar_list );
	num_sys = tree_size( svar_list );

	manager = Cudd_Init( 2*(num_env+num_sys),
						 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0 );
	Cudd_SetMaxCacheHard( manager, (unsigned int)-1 );
	Cudd_AutodynEnable( manager, CUDD_REORDER_SAME );

	if (verbose > 1) {
		logprint_startline();
		logprint_raw( "rg invoked with init_flags: " );
		LOGPRINT_INIT_FLAGS( init_flags );
		logprint_endline();
	}
	if (verbose)
		logprint( "Synthesizing a reachability game strategy..." );

	/* Set environment goal to True (i.e., any state) if none was
	   given. This simplifies the implementation below. */
	if (num_egoals == 0) {
		env_nogoal_flag = True;
		num_egoals = 1;
		env_goals = malloc( sizeof(ptree_t *) );
		*env_goals = init_ptree( PT_CONSTANT, NULL, 1 );
	}

	/* Chain together environment and system variable lists for
	   working with BDD library. */
	if (evar_list == NULL) {
		var_separator = NULL;
		evar_list = svar_list;  /* that this is the deterministic case
								   is indicated by var_separator = NULL. */
	} else {
		var_separator = get_list_item( evar_list, -1 );
		if (var_separator == NULL) {
			fprintf( stderr,
					 "Error: get_list_item failed on environment variables"
					 " list.\n" );
			return -2;
		}
		var_separator->left = svar_list;
	}

	/* Generate BDDs for the various parse trees from the problem spec. */
	if (env_init != NULL) {
		einit = ptree_BDD( env_init, evar_list, manager );
	} else {
		einit = Cudd_ReadOne( manager );
		Cudd_Ref( einit );
	}
	if (sys_init != NULL) {
		sinit = ptree_BDD( sys_init, evar_list, manager );
	} else {
		sinit = Cudd_ReadOne( manager );
		Cudd_Ref( sinit );
	}
	if (verbose)
		logprint( "Building environment transition BDD..." );
	etrans = ptree_BDD( env_trans, evar_list, manager );
	if (verbose) {
		logprint( "Done." );
		logprint( "Building system transition BDD..." );
	}
	strans = ptree_BDD( sys_trans, evar_list, manager );
	if (verbose)
		logprint( "Done." );
	if (num_egoals > 0) {
		egoals = malloc( num_egoals*sizeof(DdNode *) );
		for (i = 0; i < num_egoals; i++)
			*(egoals+i) = ptree_BDD( *(env_goals+i), evar_list, manager );
	} else {
		egoals = NULL;
	}

	Entry = Cudd_bddAnd( manager, einit, sinit );
	Cudd_Ref( Entry );
	if (num_sgoals > 0) {
		Exit = ptree_BDD( *sys_goals, evar_list, manager );
	} else {
		Exit = Cudd_Not( Cudd_ReadOne( manager ) );  /* No exit */
		Cudd_Ref( Exit );
	}

	if (var_separator == NULL) {
		evar_list = NULL;
	} else {
		var_separator->left = NULL;
	}

	/* Define a map in the manager to easily swap variables with their
	   primed selves. */
	vars = malloc( (num_env+num_sys)*sizeof(DdNode *) );
	pvars = malloc( (num_env+num_sys)*sizeof(DdNode *) );
	for (i = 0; i < num_env+num_sys; i++) {
		*(vars+i) = Cudd_bddIthVar( manager, i );
		*(pvars+i) = Cudd_bddIthVar( manager, i+num_env+num_sys );
	}
	if (!Cudd_SetVarMap( manager, vars, pvars, num_env+num_sys )) {
		fprintf( stderr,
				 "Error: failed to define variable map in CUDD manager.\n" );
		return -2;
	}
	free( vars );
	free( pvars );
	
	strategy = synthesize_reachgame_BDD( manager, num_env, num_sys,
										 Entry, Exit, etrans, strans,
										 egoals, Cudd_ReadOne( manager ),
										 verbose );

	if (strategy == NULL) {
		fprintf( stderr, "Synthesis failed.\n" );
		return -1;
	} else {

		/* De-expand nonboolean variables */
		tmppt = nonbool_var_list;
		while (tmppt) {
			aut_compact_nonbool( strategy, evar_list, svar_list,
								 tmppt->name, tmppt->value );
			tmppt = tmppt->left;
		}

		num_env = tree_size( evar_list );
		num_sys = tree_size( svar_list );

		/* Open output file if specified; else point to stdout. */
		if (output_file_index >= 0) {
			fp = fopen( argv[output_file_index], "w" );
			if (fp == NULL) {
				perror( "rg, fopen" );
				return -1;
			}
		} else {
			fp = stdout;
		}

		if (format_option == OUTPUT_FORMAT_TEXT) {
			list_aut_dump( strategy, num_env+num_sys, fp );
		} else if (format_option == OUTPUT_FORMAT_DOT) {
			if (nonbool_var_list != NULL) {
				dot_aut_dump( strategy, evar_list, svar_list,
							  DOT_AUT_ATTRIB, fp );
			} else {
				dot_aut_dump( strategy, evar_list, svar_list,
							  DOT_AUT_BINARY | DOT_AUT_ATTRIB, fp );
			}
		} else if (format_option == OUTPUT_FORMAT_AUT) {
			aut_aut_dump( strategy, num_env+num_sys, fp );
		} else { /* OUTPUT_FORMAT_TULIP */
			tulip_aut_dump( strategy, evar_list, svar_list, fp );
		}

		if (fp != stdout)
			fclose( fp );
	}

	/* Clean-up */
	delete_tree( evar_list );
	delete_tree( svar_list );
	delete_tree( env_init );
	delete_tree( sys_init );
	delete_tree( env_trans );
	delete_tree( sys_trans );
	if (env_nogoal_flag) {
		num_egoals = 0;
		delete_tree( *env_goals );
		free( env_goals );
	} else {
		for (i = 0; i < num_egoals; i++)
			delete_tree( *(env_goals+i) );
		if (num_egoals > 0)
			free( env_goals );
	}
	if (num_sgoals > 0) {
		delete_tree( *sys_goals );
		free( sys_goals );
	}
	Cudd_RecursiveDeref( manager, einit );
	Cudd_RecursiveDeref( manager, sinit );
	Cudd_RecursiveDeref( manager, etrans );
	Cudd_RecursiveDeref( manager, strans );
	Cudd_RecursiveDeref( manager, Entry );
	Cudd_RecursiveDeref( manager, Exit );
	for (i = 0; i < num_egoals; i++)
		Cudd_RecursiveDeref( manager, *(egoals+i) );
	if (strategy)
		delete_aut( strategy );
	if (verbose > 1)
		logprint( "Cudd_CheckZeroRef -> %d", Cudd_CheckZeroRef( manager ) );
	Cudd_Quit(manager);
	if (logging_flag)
		closelogfile();

    /* Return 0 if realizable, -1 if not realizable. */
	if (strategy != NULL) {
		return 0;
	} else {
		return -1;
	}

	return 0;
}
