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
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   reduce.h
 * @brief  includes various reduction methods for Steiner tree problems
 * @author Daniel Rehfeldt
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef APPLICATIONS_STP_SRC_REDUCE_H_
#define APPLICATIONS_STP_SRC_REDUCE_H_


#include "scip/scip.h"
#include "graph.h"

/** minimum spanning tree on the terminal distance graph */
typedef struct special_distance_minimum_spanning_tree SDMST;


/** reduced cost result data */
typedef struct reduce_costs_data
{
   const SCIP_Real* const redEdgeCost;           /**< reduced costs */
   const SCIP_Real* const rootToNodeDist;        /**< shortest path distances from root  */
   const PATH* const nodeTo3TermsPaths;          /**< paths to three nearest terminals */
   const int* const nodeTo3TermsBases;           /**< three nearest terminals */
   const SCIP_Real cutoff;                       /**< reduced cost cutoff value or -1.0 if not used */
   const int redCostRoot;                        /**< graph root for reduced cost calculation */
} REDCOST;


/** reduced cost reduction parameters */
typedef struct reduce_costs_parameters
{
   int                   prevrounds;         /**< number of reduction rounds that have been performed already */
   SCIP_Bool             useRec;             /**< use recombination heuristic? */
   SCIP_Bool             useExtRed;          /**< use extended tests? */
   SCIP_Bool             nodereplacing;      /**< should node replacement (by edges) be performed? */
   /* PC/MW only values: */
   SCIP_Bool             pcmw_solbasedda;    /**< rerun Da based on best primal solution */
   SCIP_Bool             pcmw_useMultRoots;  /**< vary root for DA? (if possible) */
   SCIP_Bool             pcmw_markroots;     /**< should terminals proven to be part of an opt. sol. be marked as such? */
   SCIP_Bool             pcmw_fastDa;        /**< run dual ascent heuristic in fast mode? */
} RPDA;


/* reduce.c
 */
extern SCIP_RETCODE level0(SCIP*, GRAPH*);
extern SCIP_RETCODE level0infeas(SCIP*, GRAPH*, SCIP_Bool*);
extern SCIP_RETCODE level0RpcRmw(SCIP*, GRAPH*, SCIP_Real*);
extern SCIP_RETCODE level0RpcRmwInfeas(SCIP*, GRAPH*, SCIP_Real*, SCIP_Bool*);
extern SCIP_RETCODE reduceStp(SCIP*, GRAPH*, SCIP_Real*, int, SCIP_Bool, SCIP_Bool, SCIP_Bool);
extern SCIP_RETCODE reducePc(SCIP*, const int*, GRAPH*, SCIP_Real*, int, SCIP_Bool, SCIP_Bool, SCIP_Bool);
extern SCIP_RETCODE redLoopStp(SCIP*, GRAPH*, PATH*, PATH*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*, int*, int*, STP_Bool*, SCIP_Real*, SCIP_Real, SCIP_Bool, SCIP_Bool, SCIP_Bool, int, SCIP_Bool, SCIP_Bool);
extern SCIP_RETCODE redLoopPc(SCIP*, const int*, GRAPH*, PATH*, PATH*,  GNODE**, SCIP_Real*, int*, int*, int*, int*, int*, int*, int*, STP_Bool*, SCIP_Real*, SCIP_Bool, SCIP_Bool, SCIP_Bool, int, SCIP_Bool, SCIP_Bool);
extern SCIP_RETCODE redLoopMw(SCIP*, GRAPH*, PATH*, PATH*,  GNODE**, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*, int*, int*, STP_Bool*, SCIP_Real*, STP_Bool, STP_Bool, STP_Bool, int, SCIP_Bool);
extern SCIP_RETCODE reduce(SCIP*, GRAPH*, SCIP_Real*, int, int, SCIP_Bool);

/* reduce_alt.c
 */
extern void    reduce_ans(SCIP*, GRAPH*, int*, int*);
extern void    reduce_ansAdv(SCIP*, GRAPH*, int*, int*, SCIP_Bool);
extern void    reduce_ansAdv2(SCIP*, GRAPH*, int*, int*);
extern void    reduce_nnp(SCIP*, GRAPH*, int*, int*);
extern SCIP_RETCODE    reduce_sdsp(SCIP*, GRAPH*, PATH*, PATH*, int*, int*, int*, int*, int*, int*, int, int*);
extern SCIP_RETCODE    reduce_sdStar(SCIP*, int, const int*, GRAPH*, SCIP_Real*, int*, int*, STP_Bool*, DHEAP*, int*);
extern SCIP_RETCODE    reduce_sdStarPc(SCIP*, int, const int*, GRAPH*, SCIP_Real*, int*, int*, STP_Bool*, DHEAP*, int*);
extern SCIP_RETCODE    reduce_sdWalk(SCIP*, int, const int*, GRAPH*, int*, SCIP_Real*, int*, int*, int*, STP_Bool*, int*);
extern SCIP_RETCODE    reduce_sdWalk_csr(SCIP*, int, const int*, GRAPH*, int*, SCIP_Real*, int*, STP_Bool*, DHEAP*, int*);
extern SCIP_RETCODE    reduce_sdWalkTriangle(SCIP*, int, const int*, GRAPH*, int*, SCIP_Real*, int*, STP_Bool*, DHEAP*, int*);
extern SCIP_RETCODE    reduce_sdWalkExt(SCIP*, int, const int*, GRAPH*, SCIP_Real*, int*, int*, int*, STP_Bool*, int*);
extern SCIP_RETCODE    reduce_sdWalkExt2(SCIP*, int, const int*, GRAPH*, int*,  SCIP_Real*, int*, int*, int*, STP_Bool*, int*);
extern SCIP_RETCODE    reduce_sdspSap(SCIP*, GRAPH*, PATH*, PATH*, int*, int*, int*, int*, int*, int*, int);
extern SCIP_RETCODE    reduce_sd(SCIP*, GRAPH*, PATH*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*, int*, int*, SCIP_Bool, int*);
extern SCIP_RETCODE    reduce_sdPc(SCIP*, GRAPH*, PATH*, int*, int*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_getSd(SCIP*, GRAPH*, PATH*, PATH*, SCIP_Real*, SCIP_Real, int*, int*, int*, int*, int*, int, int, int, SCIP_Bool, SCIP_Bool);
extern SCIP_RETCODE    reduce_getSdPcMw(SCIP*, const GRAPH*, PATH*, PATH*, SCIP_Real*, SCIP_Real, int*, int*, int*, int*, int*, int*, int*, int, int, int);
extern SCIP_RETCODE    reduce_nts(SCIP*, GRAPH*, PATH*, PATH*, int*, int*, int*, int*, int*, int*, int);
extern SCIP_RETCODE    reduce_bd34(SCIP*, GRAPH*, PATH*, PATH*, int*, int*, int*, int*, int*, int*, int, SCIP_Real*);
extern SCIP_RETCODE    reduce_bd34WithSd(SCIP*, GRAPH*, GRAPH*, PATH*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_nv(SCIP*, GRAPH*, PATH*, double*, int*, int*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_nvAdv(SCIP*, const int*, GRAPH*, PATH*, SCIP_Real*, double*, int*, int*, int*, int*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_sl(SCIP*, const int*, GRAPH*, PATH*, double*, int*, int*, int*, int*, STP_Bool*, int*, int*);
extern SCIP_RETCODE    reduce_ledge(SCIP*, GRAPH*, PATH*, int*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_cnsAdv(SCIP*, GRAPH*, int*, int*);
extern SCIP_RETCODE    reduce_npv(SCIP*, GRAPH*, PATH*, PATH*, int*, int*, int*, int*, int*, int*, int);
extern SCIP_RETCODE    reduce_chain2(SCIP*, GRAPH*, PATH*, PATH*, int*, int*, int*, int*, int*, int*, int);


/* reduce_bnd.c
 */
extern SCIP_RETCODE    reduce_bound(SCIP*, GRAPH*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_boundMw(SCIP*, GRAPH*, PATH*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_boundPrune(SCIP*, GRAPH*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, const int*, const int*, int*, int);
extern SCIP_RETCODE    reduce_boundHop(SCIP*, GRAPH*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_boundHopR(SCIP*, GRAPH*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*);
extern SCIP_RETCODE    reduce_boundHopRc(SCIP*, GRAPH*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, SCIP_Real, int*, int*, int*, int*, int*, SCIP_Bool);


/* reduce_da.c
 */
extern SCIP_RETCODE    reduce_da(SCIP*, GRAPH*, const RPDA*, PATH*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*, STP_Bool*, int*, SCIP_RANDNUMGEN*);
extern SCIP_RETCODE    reduce_daSlackPrune(SCIP*, SCIP_VAR**, GRAPH*, PATH*, GNODE**, SCIP_Real*, SCIP_Real*, SCIP_Real*, SCIP_Real*, int*, int*, int*, int*, int*, int*, STP_Bool*, STP_Bool*, int*, int, SCIP_Bool);
extern SCIP_RETCODE    reduce_daPcMw(SCIP*, GRAPH*, const RPDA*, PATH*, GNODE**, SCIP_Real*, int*, int*, int*, STP_Bool*, int*, SCIP_RANDNUMGEN*, SCIP_Real);


/* reduce_ext.c
 */
extern SCIP_RETCODE    reduce_deleteConflictEdges(SCIP*, GRAPH*);
extern SCIP_RETCODE    reduce_extendedCheck3Tree(SCIP*, const GRAPH*, int, const SCIP_Real*, const SCIP_Real*, const PATH*, const int*, SCIP_Real, const int*, int, int*, SCIP_Real*, SCIP_Bool*, unsigned int*, int*, SCIP_Bool*);
extern int             reduce_extendedEdge(SCIP*, GRAPH*, const PATH*, const SCIP_Real*, const SCIP_Real*, const int*, SCIP_Real, int, int*, STP_Bool*, SCIP_Bool);


/* reduce_simple.c
 */
extern SCIP_RETCODE    reduce_simple(SCIP*, GRAPH*, SCIP_Real*, int*, int*, int*);
extern SCIP_RETCODE    reduce_simple_hc(SCIP*, GRAPH*, SCIP_Real*, int*);
extern SCIP_RETCODE    reduce_simple_mw(SCIP*, GRAPH*, int*, SCIP_Real*, int*);
extern SCIP_RETCODE    reduce_simple_pc(SCIP*, const int*, GRAPH*, SCIP_Real*, int*, int*, int*);
extern SCIP_RETCODE    reduce_simple_sap(SCIP*, GRAPH*, SCIP_Real*, int*);
extern SCIP_RETCODE    reduce_deleteMultiedges(SCIP*, GRAPH*);
extern SCIP_RETCODE    reduce_contract0Edges(SCIP*, GRAPH*, SCIP_Bool);
extern SCIP_RETCODE    reduce_aritculations(SCIP*, GRAPH*, SCIP_Real*, int*);
extern SCIP_RETCODE    reduce_fixedConflicts(SCIP*, const int*, GRAPH*, int*);
extern SCIP_RETCODE    reduce_rpt(SCIP*, GRAPH*, SCIP_Real*, int*);
extern void            reduce_identifyNonLeafTerms(SCIP*, GRAPH*);
extern void            reduce_removeDeg0NonLeafTerms(SCIP*, GRAPH*, SCIP_Real*);

/* reduce_util.c
 */
extern SCIP_RETCODE    reduce_sdmstInit(SCIP*, const GRAPH*, SDMST**);
extern SCIP_Real       reduce_sdmstGetSd(const SDMST*, int, int);
extern SCIP_Real       reduce_sdmstGetSdAbort(const SDMST*, int, int, SCIP_Real);
extern const SCIP_Bool* reduce_sdmstUsedEdges(const SDMST*);
extern void            reduce_sdmstFree(SCIP*, SDMST*);

#endif /* APPLICATIONS_STP_SRC_REDUCE_H_ */
