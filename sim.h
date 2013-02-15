/** \file sim.h
 * \brief Simulation support
 *
 *
 * SCL; 2012, 2013.
 */


#ifndef SIM_H
#define SIM_H

#include "common.h"
#include "automaton.h"


/* Environment strategy is random walk (so permissibility depends on
   the specification, and is only with probability 1). */
anode_t *sim_rhc( DdManager *manager, DdNode *W, DdNode *etrans, DdNode *strans, DdNode **sgoals, char *metric_vars, int horizon, bool *init_state, int num_it, unsigned char verbose );


#endif
