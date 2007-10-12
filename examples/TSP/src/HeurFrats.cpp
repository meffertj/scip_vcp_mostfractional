/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2007 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2007 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: HeurFrats.cpp,v 1.1 2007/10/12 20:27:35 bzfberth Exp $"

/**@file   heur_frats.c
 * @brief  fractional travelling salesman heuristic
 * @author Timo Berthold
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "HeurFrats.h"
#include "ProbDataTSP.h"

using namespace tsp;
using namespace std;


/*
 * Local methods
 */


/*
 * Callback methods of primal heuristic
 */


/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
SCIP_RETCODE HeurFrats::scip_free(
   SCIP*              scip,               /**< SCIP data structure */
   SCIP_HEUR*         heur                /**< the primal heuristic itself */
   )
{
   return SCIP_OKAY;
}
   
/** initialization method of primal heuristic (called after problem was transformed) */
SCIP_RETCODE HeurFrats::scip_init(
   SCIP*              scip,               /**< SCIP data structure */
   SCIP_HEUR*         heur                /**< the primal heuristic itself */
   )
{
   ProbDataTSP*   probdata;  

   /* create heuristic data */
   SCIP_CALL( SCIPcreateSol(scip, &sol, heur) );
   
   probdata = dynamic_cast<ProbDataTSP*>(SCIPgetObjProbData(scip));
   assert( probdata != NULL );

   graph = probdata->getGraph();
   assert( graph != NULL );

   capture_graph(graph);

   return SCIP_OKAY;
}
   
/** deinitialization method of primal heuristic (called before transformed problem is freed) */
SCIP_RETCODE HeurFrats::scip_exit(
   SCIP*              scip,               /**< SCIP data structure */
   SCIP_HEUR*         heur                /**< the primal heuristic itself */
   )
{
   SCIP_CALL( SCIPfreeSol(scip, &sol) );
   release_graph(&graph);

   return SCIP_OKAY;
}
   
/** solving process initialization method of primal heuristic (called when branch and bound process is about to begin)
 *
 *  This method is called when the presolving was finished and the branch and bound process is about to begin.
 *  The primal heuristic may use this call to initialize its branch and bound specific data.
 *
 */
SCIP_RETCODE HeurFrats::scip_initsol(
   SCIP*              scip,               /**< SCIP data structure */
   SCIP_HEUR*         heur                /**< the primal heuristic itself */
   )
{
   return SCIP_OKAY;
}
   
/** solving process deinitialization method of primal heuristic (called before branch and bound process data is freed)
 *
 *  This method is called before the branch and bound process is freed.
 *  The primal heuristic should use this call to clean up its branch and bound data.
 */
SCIP_RETCODE HeurFrats::scip_exitsol(
   SCIP*              scip,               /**< SCIP data structure */
   SCIP_HEUR*         heur                /**< the primal heuristic itself */
   )
{
   return SCIP_OKAY;
}
   
/** execution method of primal heuristic */
SCIP_RETCODE HeurFrats::scip_exec(
   SCIP*              scip,               /**< SCIP data structure */
   SCIP_HEUR*         heur,               /**< the primal heuristic itself */
   SCIP_RESULT*       result              /**< pointer to store the result of the heuristic call */
   )
{  /*lint --e{715}*/

   SCIP_SOL* newsol;

   GRAPHNODE* currnode;   
   SCIP_Bool* visited;   
   int nnodes;
   int i;
   SCIP_Bool success;

   assert( result != NULL );
   assert( SCIPhasCurrentNodeLP(scip) );

   *result = SCIP_DIDNOTRUN;

   /* only call heuristic, if an optimal LP solution is at hand */
   if( SCIPgetLPSolstat(scip) != SCIP_LPSOLSTAT_OPTIMAL )
      return SCIP_OKAY;

   /* get the working solution from heuristic's local data */
   assert( sol != NULL );

   /* copy the current LP solution to the working solution */
   SCIP_CALL( SCIPlinkLPSol(scip, sol) );

   *result = SCIP_DIDNOTFIND;

   currnode = &graph->nodes[0];   
   nnodes = graph->nnodes;
   success = TRUE;
   
   SCIP_CALL( SCIPcreateSol (scip, &newsol, heur) );      
   SCIP_CALL( SCIPallocBufferArray(scip, &visited, nnodes) ); 
   BMSclearMemoryArray(visited, nnodes);
   
   assert( currnode->id == 0 );
   visited[0] = TRUE;

   for( i = 0; i < nnodes; i++ )
   {
      GRAPHEDGE* edge;
      SCIP_Real bestval; 
      GRAPHEDGE* bestedge;

      bestedge = NULL;
      bestval = -1;

      edge = currnode->first_edge; 
      
      if( i != nnodes-1 )
      {
         while( edge != NULL )
         {
            if( SCIPgetSolVal(scip, sol, edge->var) > bestval && !visited[edge->adjac->id] 
               && SCIPvarGetUbGlobal(edge->var) == 1.0 )
            {
               bestval = SCIPgetSolVal(scip, sol, edge->var);
               bestedge = edge;
            }
            edge = edge->next;
         }
      }
      else
      {
         GRAPHNODE* finalnode;
         finalnode = &graph->nodes[0]; 
         while( edge != NULL )
         {
            if( edge->adjac == finalnode )
            {
               if( SCIPvarGetUbGlobal(edge->var) == 1.0 )
               {
                  bestval =  SCIPgetSolVal(scip, sol, edge->var);
                  bestedge = edge;
               }
               break;
            }
            edge = edge->next;
         }
      }

      if( bestval == -1 )
      {
         success = FALSE;
         break;
      }    
      assert( bestval >= 0 );
      assert( bestedge != NULL );
      assert( bestval ==  SCIPgetSolVal(scip, sol, bestedge->var) );

      SCIP_CALL( SCIPsetSolVal(scip, newsol, bestedge->var, 1.0) );
      currnode = bestedge->adjac;
      assert( currnode != NULL );
      assert( 0 <= currnode->id && currnode->id <= nnodes-1 );
      if( i != nnodes-1 )
         assert( !visited[currnode->id] );
      visited[currnode->id] = TRUE;
   }
   if( success )
   {
      for( i = 0; i < nnodes; i++ )
         assert( visited[graph->nodes[i].id] );
      
      success = FALSE;
      SCIP_CALL( SCIPtrySol(scip, newsol, FALSE, FALSE, FALSE, &success) );
      if( success )
         *result = SCIP_FOUNDSOL;  
   }
   SCIP_CALL( SCIPfreeSol(scip, &newsol) );      
   SCIPfreeBufferArray(scip, &visited);
   
   return SCIP_OKAY;
}
