/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2019 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   scip_dcmp.h
 * @ingroup TODO
 * @brief  public methods for decompositions
 * @author Gregor Hendel
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef SCIP_SCIP_DECOMP_H_
#define SCIP_SCIP_DECOMP_H_

#include "scip/def.h"
#include "scip/type_cons.h"
#include "scip/type_dcmp.h"
#include "scip/type_retcode.h"
#include "scip/type_scip.h"

#ifdef __cplusplus
extern "C" {
#endif

/** creates a decomposition */
SCIP_EXPORT
SCIP_RETCODE SCIPcreateDecomp(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP**         decomp,             /**< pointer to store the decomposition data structure */
   int                   nblocks,            /**< the number of blocks (without the linking block) */
   SCIP_Bool             original,           /**< is this a decomposition in the original (TRUE) or transformed space? */
   SCIP_Bool             benderslabels       /**< should the variables be labeled for the application of Benders' decomposition */
   );

/** frees a decomposition */
SCIP_EXPORT
void SCIPfreeDecomp(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP**         decomp              /**< pointer to free the decomposition data structure */
   );

/** adds decomposition to SCIP */
SCIP_EXPORT
SCIP_RETCODE SCIPaddDecomp(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP*          decomp              /**< decomposition to add */
   );

/** gets available user decompositions for either the original or transformed problem */
SCIP_EXPORT
void SCIPgetDecomps(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP***        decomps,            /**< pointer to store decompositions array */
   int*                  ndecomps,           /**< pointer to store number of decompositions */
   SCIP_Bool             original            /**< should the decompositions for the original problem be returned? */
   );

/** returns TRUE if a constraint contains only linking variables in a decomposition */
SCIP_EXPORT
SCIP_RETCODE SCIPhasConsOnlyLinkVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP*          decomp,             /**< decomposition data structure */
   SCIP_CONS*            cons,               /**< the constraint */
   SCIP_Bool*            hasonlylinkvars     /**< will be set to TRUE if this constraint has only linking variables */
   );

/** computes constraint labels from variable labels
 *
 *  Existing labels for the constraints are simply overridden
 *
 *  The computed labels depend on the flag SCIPdecompUseBendersLabels() of the decomposition.
 *
 *  If the flag is set to FALSE, the labeling assigns
 *
 *  - label i, if only variables labeled i are present in the constraint (and optionally linking variables)
 *  - SCIP_DECOMP_LINKCONS, if there are either only variables labeled with SCIP_DECOMP_LINKVAR present, or
 *    if there are variables with more than one block label.
 *
 *  If the flag is set to TRUE, the assignment is the same, unless variables from 2 named blocks occur in the same
 *  constraint, which is an invalid labeling for the Benders case.
 */
SCIP_EXPORT
SCIP_RETCODE SCIPcomputeDecompConsLabels(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP*          decomp,             /**< decomposition data structure */
   SCIP_CONS**           conss,              /**< array of constraints */
   int                   nconss              /**< number of constraints */
   );

/** creates a decomposition of the variables from a labeling of the constraints
 *
 *  NOTE: by default, the variable labeling is based on a Dantzig-Wolfe decomposition. This means that constraints in named
 *  blocks have have precedence over linking constraints. If a variable exists in constraints from
 *  two or more named blocks, then this variable is marked as a linking variable.
 *  If a variable occurs in exactly one named block i>=0, it is assigned label i.
 *  Variables which are only in linking constraints are unlabeled. However, SCIPdecompGetVarsLabels() will
 *  label them as linking variables.
 *
 *  If the variables should be labeled for the application of Benders' decomposition, the decomposition must be
 *  flagged explicitly via SCIPdecompSetUseBendersLabels().
 *  With this setting, the presence in linking constraints takes precedence over the presence in named blocks.
 *  Now, a variable is considered linking if it is present in at least one linking constraint and an arbitrary
 *  number of constraints from named blocks.
 */
SCIP_EXPORT
SCIP_RETCODE SCIPcomputeDecompVarsLabels(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP*          decomp,             /**< decomposition data structure */
   SCIP_CONS**           conss,              /**< array of constraints */
   int                   nconss              /**< number of constraints */
   );

/** assigns linking constraints to blocks
 *
 * Each linking constraint is assigned to the most frequent block among its variables.
 * Variables of other blocks are relabeled as linking variables.
 *
 * @note: In contrast to SCIPcomputeDecompConsLabels(), this method potentially relabels variables.
 */
SCIP_EXPORT
SCIP_RETCODE SCIPassignDecompLinkConss(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP*          decomp,             /**< decomposition data structure */
   SCIP_CONS**           conss,              /**< array of linking constraints that should be reassigned */
   int                   nconss              /**< number of constraints */
   );

/** computes decomposition statistics and store them in the decomposition object */
SCIP_EXPORT
SCIP_RETCODE SCIPcomputeDecompStats(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECOMP*          decomp              /**< decomposition data structure */
   );

#ifdef __cplusplus
}
#endif

#endif
