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

/**@file   graph_node.c
 * @brief  includes graph node methods for Steiner problems
 * @author Daniel Rehfeldt
 *
 * Graph node methods for Steiner problems
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

/*lint -esym(766,stdlib.h) -esym(766,malloc.h)         */
/*lint -esym(766,string.h) */

#include "graph.h"
#include "portab.h"



/* is the vertex a leaf (for NWPTSPG) */
SCIP_Bool graph_knotIsNWLeaf(
   const GRAPH*          g,                  /**< the graph */
   int                   vertex              /**< the vertex  */
)
{
   int e;
   assert(g != NULL && g->stp_type == STP_NWPTSPG);

   for( e = g->outbeg[vertex]; e != EAT_LAST; e = g->oeat[e] )
      if( g->cost[e] < FARAWAY )
         break;

   return (e == EAT_LAST );
}


/** is node in range? */
SCIP_Bool graph_knot_isInRange(
   const GRAPH*          g,                  /**< the graph */
   int                   k                   /**< the node */
   )
{
   assert(g);

   return (0 <= k && k < g->knots);
}


/** add a vertex */
void graph_knot_add(
   GRAPH*                p,                  /**< the graph */
   int                   term                /**< terminal property */
   )
{
   assert(p        != NULL);
   assert(p->ksize >  p->knots);
   assert(term     <  p->layers);

   p->term  [p->knots] = term;
   p->mark  [p->knots] = TRUE;
   p->grad  [p->knots] = 0;
   p->inpbeg[p->knots] = EAT_LAST;
   p->outbeg[p->knots] = EAT_LAST;

   if( Is_term(term) )
      p->terms++;

   p->knots++;
}

/** change terminal property of a vertex */
void graph_knot_chg(
   GRAPH*                p,                  /**< the graph */
   int                   node,               /**< node to be changed */
   int                   term                /**< terminal property */
   )
{
   assert(p      != NULL);
   assert(node   >= 0);
   assert(node   < p->knots);
   assert(term == STP_TERM || term == STP_TERM_NONE || term == STP_TERM_NONLEAF || term == STP_TERM_PSEUDO);

   if( term != p->term[node] )
   {
      if( Is_term(p->term[node]) )
         p->terms--;

      p->term[node] = term;

      if( Is_term(p->term[node]) )
         p->terms++;
   }
}


/** delete node */
void graph_knot_del(
   SCIP*                 scip,               /**< SCIP data structure */
   GRAPH*                g,                  /**< the graph */
   int                   k,                  /**< the node */
   SCIP_Bool             freeancestors       /**< free edge ancestors? */
   )
{
   assert(scip && g);
   assert(k >= 0 && k < g->knots);

   while( g->outbeg[k] != EAT_LAST )
      graph_edge_del(scip, g, g->outbeg[k], freeancestors);

   assert(g->grad[k] == 0);
   assert(g->outbeg[k] == EAT_LAST);
   assert(g->inpbeg[k] == EAT_LAST);
}


/** pseudo deletes non-terminal of degree 2 */
SCIP_RETCODE graph_knot_replaceDeg2(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   vertex,             /**< the vertex */
   GRAPH*                g,                  /**< the graph */
   int*                  solnode,            /**< marks whether an node is part of a given solution (CONNECT), or is NULL */
   SCIP_Bool*            edgeEliminated      /**< edge eliminated? (due to conflict) */
   )
{
   SINGLETONANS ancestors1;
   SINGLETONANS ancestors2;
   const int e1 = g->outbeg[vertex];
   const int e2 = g->oeat[e1];
   const int i1 = g->head[e1];
   const int i2 = g->head[e2];
   int newedge;
   SCIP_Bool conflict;

   assert(scip && g);
   assert(vertex >= 0 && vertex < g->knots);
   assert(!Is_term(g->term[vertex]));
   assert(g->grad[vertex] == 2);
   assert(e1 >= 0 && e2 >= 0);
   assert(SCIPisEQ(scip, g->cost[e1], g->cost[flipedge(e1)]));
   assert(SCIPisEQ(scip, g->cost[e2], g->cost[flipedge(e2)]));
   assert(graph_valid_pseudoAncestors(scip, g));

   *edgeEliminated = FALSE;

   SCIP_CALL( graph_singletonAncestors_init(scip, g, e1, &(ancestors1)) );
   SCIP_CALL( graph_singletonAncestors_init(scip, g, e2, &(ancestors2)) );

   SCIP_CALL( graph_edge_reinsert(scip, g, e1, i2, i1, g->cost[e1] + g->cost[e2],
         -1, &ancestors2, &ancestors1, &newedge, &conflict) );

   graph_singletonAncestors_freeMembers(scip, &(ancestors1));
   graph_singletonAncestors_freeMembers(scip, &(ancestors2));

   graph_knot_del(scip, g, vertex, TRUE);

   if( conflict )
   {
      SCIPdebugMessage("conflict in graph_knot_replaceDeg2 \n");
      assert(newedge >= 0);
      graph_edge_del(scip, g, newedge, TRUE);
      *edgeEliminated = TRUE;
   }

   return SCIP_OKAY;
}



/** contracts node s into node t */
SCIP_RETCODE graph_knot_contract(
   SCIP*                 scip,               /**< SCIP data structure */
   GRAPH*                p,                  /**< graph data structure */
   int*                  solnode,            /**< node array to mark whether an node is part of a given solution (CONNECT),
                                                or NULL */
   int                   t,                  /**< tail node to be contracted (surviving node) */
   int                   s                   /**< head node to be contracted */
   )
{
   SCIP_Real* incost = NULL;
   SCIP_Real* outcost = NULL;
   SINGLETONANS* ancestors = NULL;
   int* mark = NULL;
   int* edge = NULL;
   int* knot = NULL;
   int slc = 0;
   int sgrad;

   assert(p && scip);
   assert(t >= 0 && t < p->knots);
   assert(s >= 0 && s < p->knots);
   assert(p->grad[s] > 0 && p->grad[t] > 0);
   assert(p->layers == 1);

   /* save solution */
   if( solnode != NULL )
      if( solnode[s] == CONNECT )
         solnode[t] = CONNECT;

   /* change terminal property */
   if( Is_term(p->term[s]) )
   {
      graph_knot_chg(p, t, p->term[s]);
      graph_knot_chg(p, s, -1);
   }

   /* retain root */
   if( p->source == s )
      p->source = t;

   sgrad = p->grad[s];
   if( sgrad >= 2 )
   {
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &incost, sgrad - 1) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &outcost, sgrad - 1) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &mark, sgrad - 1) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &edge, sgrad - 1) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &knot, sgrad - 1) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &ancestors, sgrad - 1) );
   }

   /* store edges to be moved/removed */
   for( int es = p->outbeg[s]; es != EAT_LAST; es = p->oeat[es] )
   {
      assert(p->tail[es] == s);

      if( p->head[es] != t )
      {
         assert(ancestors && mark && incost && outcost && edge && knot);

         SCIP_CALL( graph_singletonAncestors_init(scip, p, es, &(ancestors[slc])) );
         mark[slc] = FALSE;
         edge[slc] = es;
         knot[slc] = p->head[es];
         outcost[slc] = p->cost[es];
         incost[slc] = p->cost[Edge_anti(es)];
         slc++;

         assert(slc < sgrad);
      }
   }

   assert(slc == sgrad - 1);

   /* traverse edges */
   for( int i = 0; i < slc; i++ )
   {
      int et;
      const int ihead = knot[i];

      assert(knot != NULL && outcost != NULL && incost != NULL && mark != NULL);

      /* search for an edge out of t with same head as current edge */

      if( p->grad[ihead] >= p->grad[t] )
      {
         for( et = p->outbeg[t]; et >= 0; et = p->oeat[et] )
            if( p->head[et] == ihead )
               break;
      }
      else
      {
         for( et = p->inpbeg[ihead]; et >= 0; et = p->ieat[et] )
            if( p->tail[et] == t )
               break;
      }

      /* does such an edge not exist? */
      if( et == EAT_LAST )
      {
         mark[i] = TRUE;
      }
      else
      {
         SCIP_Bool copyPseudoancestors = FALSE;
         assert(et != EAT_LAST);

         /* This is for nodes with edges to s and t.
          * Need to adjust the out and in costs of the edge
          */

         if( graph_typeIsUndirected(p) && SCIPisGT(scip, p->cost[et], outcost[i]) && SCIPisGT(scip, p->cost[Edge_anti(et)], incost[i]) )
            copyPseudoancestors = TRUE;

         if( copyPseudoancestors )
            graph_edge_delPseudoAncestors(scip, et, p);

         if( SCIPisGT(scip, p->cost[et], outcost[i]) )
         {
            SCIPintListNodeFree(scip, &((p->ancestors)[et]));
            SCIP_CALL( SCIPintListNodeAppendCopy(scip, &((p->ancestors)[et]), ancestors[i].ancestors, NULL) );

            assert(graph_edge_nPseudoAncestors(p, et) == 0);

            p->cost[et] = outcost[i];
         }
         if( SCIPisGT(scip, p->cost[Edge_anti(et)], incost[i]) )
         {
            const int anti = Edge_anti(et);

            SCIPintListNodeFree(scip, &(p->ancestors[anti]));
            SCIP_CALL( SCIPintListNodeAppendCopy(scip, &((p->ancestors)[anti]), ancestors[i].revancestors, NULL) );

            assert(graph_edge_nPseudoAncestors(p, anti) == 0);

            p->cost[anti] = incost[i];
         }

         if( copyPseudoancestors )
         {
            SCIP_Bool conflict;
            SCIP_CALL( graph_pseudoAncestors_appendCopySingToEdge(scip, et, &(ancestors[i]), FALSE, p, &conflict) );
            assert(!conflict);
         }
      }
   }

   /* insert edges */
   for( int i = 0; i < slc; i++ )
   {
      assert(mark && ancestors && knot && outcost && incost);

      if( mark[i] )
      {
         int es = p->outbeg[s];
         const int head = knot[i];
         const int tail = t;
         SCIP_Bool conflict;

         assert(es != EAT_LAST);

         graph_edge_del(scip, p, es, TRUE);

         assert(p->ancestors[es] == NULL);
         SCIP_CALL( SCIPintListNodeAppendCopy(scip, &(p->ancestors[es]), ancestors[i].ancestors, NULL) );

         SCIP_CALL( graph_pseudoAncestors_appendCopySingToEdge(scip, es, &(ancestors[i]), FALSE, p, &conflict) );
         assert(!conflict);

         p->grad[head]++;
         p->grad[tail]++;

         p->cost[es]     = outcost[i];
         p->tail[es]     = tail;
         p->head[es]     = head;
         p->ieat[es]     = p->inpbeg[head];
         p->oeat[es]     = p->outbeg[tail];
         p->inpbeg[head] = es;
         p->outbeg[tail] = es;

         es = Edge_anti(es);

         assert(p->ancestors[es] == NULL);
         SCIP_CALL( SCIPintListNodeAppendCopy(scip, &(p->ancestors[es]), ancestors[i].revancestors, NULL) );

         p->cost[es]     = incost[i];
         p->tail[es]     = head;
         p->head[es]     = tail;
         p->ieat[es]     = p->inpbeg[tail];
         p->oeat[es]     = p->outbeg[head];
         p->inpbeg[tail] = es;
         p->outbeg[head] = es;
      }
   }

   /* delete remaining edges */
   graph_knot_del(scip, p, s, TRUE);

   if( sgrad >= 2 )
   {
      assert(ancestors && knot && outcost && edge && mark && incost);

      for( int i = 0; i < slc; i++ )
         graph_singletonAncestors_freeMembers(scip, &(ancestors[i]));

      SCIPfreeBlockMemoryArray(scip, &ancestors, sgrad - 1);
      SCIPfreeBlockMemoryArray(scip, &knot, sgrad - 1);
      SCIPfreeBlockMemoryArray(scip, &edge, sgrad - 1);
      SCIPfreeBlockMemoryArray(scip, &mark, sgrad - 1);
      SCIPfreeBlockMemoryArray(scip, &outcost, sgrad - 1);
      SCIPfreeBlockMemoryArray(scip, &incost, sgrad - 1);
   }

   return SCIP_OKAY;
}


/** contract an edge, given index and by its endpoints, which is to be fixed */
SCIP_RETCODE graph_knot_contractFixed(
   SCIP*                 scip,               /**< SCIP data structure */
   GRAPH*                g,                  /**< graph data structure */
   int*                  solnode,            /**< node array to mark whether an node is part of a given solution (CONNECT),
                                                or NULL */
   int                   edge,               /**< the edge */
   int                   t,                  /**< tail node to be contracted (surviving node) */
   int                   s                   /**< head node to be contracted */
   )
{
   SCIP_CALL( graph_fixed_addEdge(scip, edge, g) );
   SCIP_CALL( graph_knot_contract(scip, g, solnode, t, s) );

   return SCIP_OKAY;
}


/** contract endpoint of lower degree into endpoint of higher degree */
SCIP_RETCODE graph_knot_contractLowdeg2High(
   SCIP*                 scip,               /**< SCIP data structure */
   GRAPH*                g,                  /**< graph data structure */
   int*                  solnode,            /**< node array to mark whether an node is part of a given solution (CONNECT),
                                                or NULL */
   int                   t,                  /**< tail node to be contracted */
   int                   s                   /**< head node to be contracted */
   )
{
   assert(g != NULL);

   if( g->grad[t] >= g->grad[s] )
      SCIP_CALL( graph_knot_contract(scip, g, solnode, t, s) );
   else
      SCIP_CALL( graph_knot_contract(scip, g, solnode, s, t) );

   return SCIP_OKAY;
}
