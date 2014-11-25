// /* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*   Type....: Functions                                                     */
/*   File....: grphbase.c                                                    */
/*   Name....: Basic Graph Routines                                          */
/*   Author..: Thorsten Koch                                                 */
/*   Copyright by Author, All rights reserved                                */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*lint -esym(750,GRPHBASE_C) -esym(766,stdlib.h) -esym(766,malloc.h)         */
/*lint -esym(766,string.h)                                                   */

/* Ein Graph wird initialisiert, und dannach veraendert sich die Anzahl
 * seiner Knoten 'g->knots' und Kanten 'g->edges' nicht mehr kleiner.
 * Allerdings kann der Grad eines Knotens auf 0 zurueckgehen und eine
 * Kante als EAT_FREE gekennzeichnet werden. Wird dann 'graph_pack()'
 * aufgerufen, werden diese Knoten und Kanten nicht uebernommen.
 */
#define GRPHBASE_C
#include "scip/misc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "portab.h"
#include "misc_stp.h"
#include "grph.h"

GRAPH* graph_init(
   int ksize,
   int esize,
   int layers,
   int flags)
{
   GRAPH* p;
   int    i;

   assert(ksize > 0);
   assert(ksize < INT_MAX);
   assert(esize >= 0);
   assert(esize < INT_MAX);
   assert(layers > 0);
   assert(layers < SHRT_MAX);

   p = malloc(sizeof(*p));

   assert(p != NULL);

   p->fixedges = NULL;
   p->ancestors = NULL;

   p->norgmodelknots = 0;
   p->norgmodeledges = 0;
   p->ksize  = ksize;
   p->orgknots = 0;
   p->orgedges = 0;
   p->knots  = 0;
   p->terms  = 0;
   p->stp_type = UNKNOWN;
   p->flags  = flags;
   p->layers = layers;
   p->hoplimit = UNKNOWN;
   p->locals = malloc((size_t)layers * sizeof(int));
   p->source = malloc((size_t)layers * sizeof(int));

   p->term   = malloc((size_t)ksize * sizeof(int));
   p->mark   = malloc((size_t)ksize * sizeof(int));
   p->grad   = malloc((size_t)ksize * sizeof(int));
   p->inpbeg = malloc((size_t)ksize * sizeof(int));
   p->outbeg = malloc((size_t)ksize * sizeof(int));

   p->esize = esize;
   p->edges = 0;

   p->cost  = malloc((size_t)esize * sizeof(double));

   p->tail  = malloc((size_t)esize * sizeof(int));

   p->head  = malloc((size_t)esize * sizeof(int));

   p->orgtail  = NULL;

   p->orghead  = NULL;

   p->ieat  = malloc((size_t)esize * sizeof(int));
   p->oeat  = malloc((size_t)esize * sizeof(int));

   p->xpos  = malloc((size_t)ksize * sizeof(int));
   p->ypos  = malloc((size_t)ksize * sizeof(int));

   //p->ctrctknot = malloc((size_t)ksize * sizeof(int));
   // p->ctrctedge = malloc((size_t)esize * sizeof(int));

   p->maxdeg = NULL;
   p->grid_coordinates = NULL;
   p->grid_ncoords = NULL;

   p->mincut_dist = NULL;
   p->mincut_head = NULL;
   p->mincut_numb = NULL;
   p->mincut_prev = NULL;
   p->mincut_next = NULL;
   p->mincut_temp = NULL;
   p->mincut_e = NULL;
   p->mincut_x = NULL;
   p->mincut_r = NULL;

   p->path_heap = NULL;
   p->path_state = NULL;

   assert(p->xpos   != NULL);
   assert(p->ypos   != NULL);
   assert(p->locals != NULL);
   assert(p->source != NULL);
   assert(p->term   != NULL);
   assert(p->mark   != NULL);
   assert(p->grad   != NULL);
   assert(p->inpbeg != NULL);
   assert(p->outbeg != NULL);
   assert(p->cost   != NULL);
   assert(p->tail   != NULL);
   assert(p->head   != NULL);
   assert(p->ieat   != NULL);
   assert(p->oeat   != NULL);

   for(i = 0; i < p->layers; i++)
   {
      p->source[i] = -1;
      p->locals[i] =  0;
   }
   return(p);
}

/* initialize data structures required to keep track of reductions */
void graph_init_history(
   GRAPH* graph,
   int** orgtail,
   int** orghead,
   IDX*** ancestors
   )
{
   int e;
   int nedges;
   assert(graph != NULL);

   nedges = graph->edges;

   (*orgtail)  = malloc((size_t)nedges * sizeof(int));
   (*orghead)  = malloc((size_t)nedges * sizeof(int));

   for( e = 0; e < nedges; e++ )
   {
      (*orgtail)[e] = graph->tail[e];
      (*orghead)[e] = graph->head[e];
   }

   *ancestors = malloc((size_t)(nedges) * sizeof(IDX*));
   for( e = 0; e < nedges; e++ )
   {
      (*ancestors)[e] = malloc((size_t)sizeof(IDX));
      (*ancestors)[e]->index = e;
      (*ancestors)[e]->parent = NULL;
   }
}
void graph_resize(
   GRAPH* p,
   int    ksize,
   int    esize,
   int    layers)
{
   int i;
   assert(p      != NULL);
   assert((ksize  < 0) || (ksize  >= p->knots));
   assert((esize  < 0) || (esize  >= p->edges));
   assert((layers < 0) || (layers >= p->layers));

   if ((layers > 0) && (layers != p->layers))
   {
      p->locals = realloc(p->locals, (size_t)layers * sizeof(int));
      p->source = realloc(p->source, (size_t)layers * sizeof(int));

      for(i = p->layers; i < layers; i++)
      {
         p->source[i] = -1;
         p->locals[i] =  0;
      }
      p->layers = layers;
   }
   if ((ksize > 0) && (ksize != p->ksize))
   {
      p->ksize  = ksize;
      p->term   = realloc(p->term,   (size_t)ksize * sizeof(int));
      p->mark   = realloc(p->mark,   (size_t)ksize * sizeof(int));
      p->grad   = realloc(p->grad,   (size_t)ksize * sizeof(int));
      p->inpbeg = realloc(p->inpbeg, (size_t)ksize * sizeof(int));
      p->outbeg = realloc(p->outbeg, (size_t)ksize * sizeof(int));
      p->xpos   = realloc(p->xpos,   (size_t)ksize * sizeof(int));
      p->ypos   = realloc(p->ypos,   (size_t)ksize * sizeof(int));
      // p->ctrctknot = realloc(p->ctrctknot, (size_t)ksize * sizeof(int));
   }
   if ((esize > 0) && (esize != p->esize))
   {
      p->esize = esize;
      p->cost  = realloc(p->cost, (size_t)esize * sizeof(double));
      p->tail  = realloc(p->tail, (size_t)esize * sizeof(int));
      p->head  = realloc(p->head, (size_t)esize * sizeof(int));
      /*p->orgtail  = realloc(p->tail, (size_t)esize * sizeof(int));
        p->orghead  = realloc(p->head, (size_t)esize * sizeof(int));
      */
      p->ieat  = realloc(p->ieat, (size_t)esize * sizeof(int));
      p->oeat  = realloc(p->oeat, (size_t)esize * sizeof(int));
      // p->ctrctedge = realloc(p->ctrctedge, (size_t)esize * sizeof(int));
   }
   if( p->stp_type == STP_GRID )
   {
      p->grid_ncoords = realloc(p->grid_ncoords, (size_t)p->grid_dim * sizeof(int));
      assert(p->grid_ncoords != NULL);
   }
   assert(p->locals != NULL);
   assert(p->source != NULL);
   assert(p->term   != NULL);
   assert(p->mark   != NULL);
   assert(p->grad   != NULL);
   assert(p->inpbeg != NULL);
   assert(p->outbeg != NULL);
   assert(p->cost   != NULL);
   assert(p->tail   != NULL);
   assert(p->head   != NULL);
   assert(p->ieat   != NULL);
   assert(p->oeat   != NULL);
   assert(p->xpos   != NULL);
   assert(p->ypos   != NULL);
   //  assert(p->ctrctknot != NULL);
   //  assert(p->ctrctedge != NULL);
}


/** used by graph_grid_create */
static
int getNodeNumber(
   int  grid_dim,
   int  shiftcoord,
   int* ncoords,
   int* currcoord
   )
{
   int number = 0;
   int tmp;
   int i;
   int j;
   for( i = 0; i < grid_dim; i++ )
   {
      tmp = 1;
      for( j = i + 1; j < grid_dim; j++ )
      {
         tmp = tmp * ncoords[j];
      }
      if( shiftcoord == i )
         number += (currcoord[i] + 1) * tmp;
      else
         number += currcoord[i] * tmp;
   }
   number++;
   return number;
}

/** used by graph_obstgrid_create */
static
void compEdgesObst(
   int   coord,
   int   grid_dim,
   int   nobstacles,
   int*  ncoords,
   int*  currcoord,
   int*  edgecosts,
   int*  gridedgecount,
   int** coords,
   int** gridedges,
   int** obst_coords,
   char* inobstacle
   )
{
   char inobst;
   int i;
   int j;
   int z;
   int x;
   int y;
   int node;
   i = 0;
   while( i < ncoords[coord] ) //for( i = 0; i < ncoords[coord]; i++ )
   {
      currcoord[coord] = i;
      if( coord < grid_dim - 1 )
         compEdgesObst(coord + 1, grid_dim, nobstacles, ncoords, currcoord, edgecosts, gridedgecount, coords, gridedges, obst_coords, inobstacle);
      else
      {
	 x = coords[0][currcoord[0]];
	 y = coords[1][currcoord[1]];
	 // printf("curr cord (%d,%d)  \n ", x, y);
	 inobst = FALSE;
	 node = getNodeNumber(grid_dim, -1, ncoords, currcoord);
	 for( z = 0; z < nobstacles; z++ )
         {

            /*printf("curr x1, y1 (%d,%d)  \n ", obst_coords[0][z], obst_coords[1][z]);
              printf("curr x2, y2(%d,%d)  \n ", obst_coords[2][z], obst_coords[3][z]);
              printf("  \n ");*/
            assert(obst_coords[0][z] < obst_coords[2][z]);
            assert(obst_coords[1][z] < obst_coords[3][z]);
            if( x > obst_coords[0][z] && x < obst_coords[2][z] &&
               y > obst_coords[1][z] && y < obst_coords[3][z] )
            {
               inobst = TRUE;
               //printf("obst node found %d, %d inobst:%d \n", x, y, node);
               inobstacle[node-1] = TRUE;
               break;
            }
         }
         for( j = 0; j < grid_dim; j++ )
         {
            if( currcoord[j] + 1 < ncoords[j] )
            {
	       if( inobst == FALSE )
	       {
                  gridedges[0][*gridedgecount] = node;
                  gridedges[1][*gridedgecount] = getNodeNumber(grid_dim, j, ncoords, currcoord);
                  edgecosts[*gridedgecount] = coords[j][currcoord[j] + 1] - coords[j][currcoord[j]];
		  (*gridedgecount)++;
                  //  printf("edgeXXX %d_%d %d \n ", coords[j][currcoord[j] + 1],  coords[j][currcoord[j]], *gridedgecount );
	       }


               /*   printf("edge %d_%d \n ", getNodeNumber(-1), getNodeNumber(j) );*/
            }
         }
      }
      i++;
   }
}

/** used by graph_grid_create */
static
void compEdges(
   int   coord,
   int   grid_dim,
   int*  ncoords,
   int*  currcoord,
   int*  edgecosts,
   int*  gridedgecount,
   int** coords,
   int** gridedges
   )
{
   int j;
   int i = 0;
   while( i < ncoords[coord] )
   {
      currcoord[coord] = i;
      if( coord < grid_dim - 1 )
         compEdges(coord + 1, grid_dim, ncoords, currcoord, edgecosts, gridedgecount, coords, gridedges);
      else
      {
         for( j = 0; j < grid_dim; j++ )
         {
            if( currcoord[j] + 1 < ncoords[j] )
            {
               gridedges[0][*gridedgecount] = getNodeNumber(grid_dim, -1, ncoords, currcoord);
               gridedges[1][*gridedgecount] = getNodeNumber(grid_dim, j, ncoords, currcoord);
               edgecosts[*gridedgecount] = coords[j][currcoord[j] + 1] - coords[j][currcoord[j]];
               /*     printf("edgeXXX %d_%d %d \n ", coords[j][currcoord[j] + 1],  coords[j][currcoord[j]], gridedgecount );*/
               (*gridedgecount)++;
               /*   printf("edge %d_%d \n ", getNodeNumber(-1), getNodeNumber(j) );*/
            }
         }
      }
      i++;
   }
}

/** creates a graph out of a given grid */
GRAPH* graph_obstgrid_create(
   int** coords,
   int** obst_coords,
   int nterms,
   int grid_dim,
   int nobstacles,
   int scale_order
   )
{
   GRAPH* graph;
   double cost;
   int    i;
   int    j;
   int    k;
   int    tmp;
   int    shift;
   int    nnodes = 0;
   int    nedges;
   double  scale_factor;
   int    gridedgecount;
   int*   ncoords;
   int*   currcoord;
   int*   edgecosts;
   int**  termcoords;
   int**  gridedges;
   char*  inobstacle;
   assert(coords != NULL);
   assert(grid_dim > 1);
   assert(nterms > 0);
   assert(grid_dim == 2);
   scale_factor = pow(10.0, (double) scale_order);

   printf("nobstacles : %d \n", nobstacles);
   /* initalize the terminal-coordinates array */
   termcoords = (int**) malloc(grid_dim * sizeof(int*));
   for( i = 0; i < grid_dim; i++ )
   {
      termcoords[i] = (int*) malloc(nterms * sizeof(int));
      for( j = 0; j < nterms; j++ )
	 termcoords[i][j] = coords[i][j];
   }
   ncoords = (int*) malloc(grid_dim * sizeof(int));
   currcoord = (int*) malloc(grid_dim * sizeof(int));

   /* sort the coordinates and delete multiples */
   for( i = 0; i < grid_dim; i++ )
   {
      ncoords[i] = 1;
      SCIPsortInt(coords[i], nterms);
      shift = 0;
      for( j = 0; j < nterms - 1; j++ )
      {
         if( coords[i][j] == coords[i][j + 1] )
         {
            shift++;
         }
         else
         {
            /* printf("%d_%d %d=%d \n", j + 1 - shift, j + 1, coords[i][j+ 1 - shift],coords[i][j + 1] ); */
            coords[i][j + 1 - shift] = coords[i][j + 1];
            ncoords[i]++;
         }
      }
      /* for( j = 0; j < nterms + 1; j++ )
         {
         printf(" %d ", coords[i][j]);
         }
         printf( "\n");*/
   }

   nnodes = 1;
   for( i = 0; i < grid_dim; i++ )
   {
      nnodes = nnodes * ncoords[i];
   }
   tmp = 0;
   for( i = 0; i < grid_dim; i++ )
   {
      tmp = tmp + nnodes / ncoords[i];
   }

   nedges = grid_dim * nnodes - tmp;
   gridedges = (int**) malloc(2 * sizeof(int*));
   edgecosts = (int*) malloc(nedges * sizeof(int));
   gridedges[0] = (int*) malloc(nedges * sizeof(int));
   gridedges[1] = (int*) malloc(nedges * sizeof(int));
   inobstacle = (char*) malloc(nnodes * sizeof(char));
   gridedgecount = 0;
   for( i = 0; i < nnodes; i++ )
      inobstacle[i] = FALSE;
   compEdgesObst(0, grid_dim, nobstacles, ncoords, currcoord, edgecosts, &gridedgecount, coords, gridedges, obst_coords, inobstacle);
   nedges = gridedgecount;
   /* initialize empty graph with allocated slots for nodes and edges */
   graph = graph_init(nnodes, 2 * nedges, 1, 0);

   graph->grid_ncoords = (int*) malloc(grid_dim * sizeof(int));
   for( i = 0; i < grid_dim; i++ )
      graph->grid_ncoords[i] = ncoords[i];

   graph->grid_dim = grid_dim;
   graph->grid_coordinates = coords;

   /* add nodes */
   for( i = 0; i < nnodes; i++ )
      graph_knot_add(graph, -1, -1, -1);
   /*for( i = 0; i < nnodes; i++ )
     printf("deg: %d\n", graph->grad[i] );*/

   /* add edges */
   for( i = 0; i < nedges; i++ )
   {
      /* (re) scale edge costs */
      if( inobstacle[gridedges[1][i] - 1] == FALSE )
      {
         cost = ((double) edgecosts[i]) / scale_factor;
         graph_edge_add(graph, gridedges[0][i] - 1, gridedges[1][i] - 1, cost, cost);
         // printf( "add edge %d->%d cost: %f\n", gridedges[0][i] - 1, gridedges[1][i] - 1, cost);
      }
   }
   printf( "add edge \n");
   /* add terminals */
   for( i = 0; i < nterms; i++ )
   {
      for( j = 0; j < grid_dim; j++ )
      {
	 for( k = 0; k <= ncoords[j]; k++ )
	 {
	    if( k == ncoords[j] )
	    {
	       printf( "COUNTING ERROR IN graph_grid_create \n");
	       assert(0);
	       return NULL;
	    }
	    if( coords[j][k] == termcoords[j][i] )
	    {
               currcoord[j] = k;
               break;
	    }
	 }
      }
      /* the position of the (future) terminal */
      k = getNodeNumber(grid_dim, -1, ncoords, currcoord) - 1;
      tmp = -1;

      if( i == 0 )
      {
	 graph->source[0] = k;
	 printf("root: (%d", termcoords[0][i]);
	 for( j = 1; j < grid_dim; j++ )
            printf(", %d", termcoords[j][i]);
	 printf(")\n");
      }

      /* make a terminal out of the node */
      graph_knot_chg(graph, k, 0, -1, -1);
   }

   graph = graph_pack(graph);
   graph->stp_type = STP_OBSTACLES_GRID;

   for( i = 0; i < grid_dim; i++ )
   {
      free(termcoords[i]);
      if( i < 2 )
	 free(gridedges[i]);
   }
   free(inobstacle);
   free(termcoords);
   free(edgecosts);
   free(gridedges);
   free(ncoords);
   free(currcoord);
   return graph;

}



/** creates a graph out of a given grid */
GRAPH* graph_grid_create(
   int** coords,
   int nterms,
   int grid_dim,
   int scale_order
   )
{
   GRAPH* graph;
   double cost;
   int    i;
   int    j;
   int    k;
   int    tmp;
   int    shift;
   int    nnodes = 0;
   int    nedges;
   double  scale_factor;
   int    gridedgecount;
   int*   ncoords;
   int*   currcoord;
   int*   edgecosts;
   int**  termcoords;
   int**  gridedges;
   assert(coords != NULL);
   assert(grid_dim > 1);
   assert(nterms > 0);

   scale_factor = pow(10.0, (double) scale_order);

   /* initalize the terminal-coordinates array */
   termcoords = (int**) malloc(grid_dim * sizeof(int*));
   for( i = 0; i < grid_dim; i++ )
   {
      termcoords[i] = (int*) malloc(nterms * sizeof(int));
      for( j = 0; j < nterms; j++ )
	 termcoords[i][j] = coords[i][j];
   }
   ncoords = (int*) malloc(grid_dim * sizeof(int));
   currcoord = (int*) malloc(grid_dim * sizeof(int));

   /* sort the coordinates and delete multiples */
   for( i = 0; i < grid_dim; i++ )
   {
      ncoords[i] = 1;
      SCIPsortInt(coords[i], nterms);
      shift = 0;
      for( j = 0; j < nterms - 1; j++ )
      {
         if( coords[i][j] == coords[i][j + 1] )
         {
            shift++;
         }
         else
         {
            /* printf("%d_%d %d=%d \n", j + 1 - shift, j + 1, coords[i][j+ 1 - shift],coords[i][j + 1] ); */
            coords[i][j + 1 - shift] = coords[i][j + 1];
            ncoords[i]++;
         }
      }
      /* for( j = 0; j < nterms + 1; j++ )
         {
         printf(" %d ", coords[i][j]);
         }
         printf( "\n");*/
   }

   nnodes = 1;
   for( i = 0; i < grid_dim; i++ )
   {
      nnodes = nnodes * ncoords[i];
   }
   tmp = 0;
   for( i = 0; i < grid_dim; i++ )
   {
      tmp = tmp + nnodes / ncoords[i];
   }

   nedges = grid_dim * nnodes - tmp;
   gridedges = (int**) malloc(2 * sizeof(int*));
   edgecosts = (int*) malloc(nedges * sizeof(int));
   gridedges[0] = (int*) malloc(nedges * sizeof(int));
   gridedges[1] = (int*) malloc(nedges * sizeof(int));
   gridedgecount = 0;

   compEdges(0, grid_dim, ncoords, currcoord, edgecosts, &gridedgecount, coords, gridedges);

   /* initialize empty graph with allocated slots for nodes and edges */
   graph = graph_init(nnodes, 2 * nedges, 1, 0);

   graph->grid_ncoords = (int*) malloc(grid_dim * sizeof(int));
   for( i = 0; i < grid_dim; i++ )
      graph->grid_ncoords[i] = ncoords[i];

   graph->grid_dim = grid_dim;
   graph->grid_coordinates = coords;

   /* add nodes */
   for( i = 0; i < nnodes; i++ )
      graph_knot_add(graph, -1, -1, -1);

   /* add edges */
   for( i = 0; i < nedges; i++ )
   {
      /* (re) scale edge costs */
      cost = (double) edgecosts[i] / scale_factor;
      graph_edge_add(graph, gridedges[0][i] - 1, gridedges[1][i] - 1, cost, cost);
   }

   /* add terminals */
   for( i = 0; i < nterms; i++ )
   {
      for( j = 0; j < grid_dim; j++ )
      {
	 for( k = 0; k <= ncoords[j]; k++ )
	 {
	    if( k == ncoords[j] )
	    {
	       printf( "COUNTING ERROR IN graph_grid_create \n");
	       assert(0);
	       return NULL;
	    }
	    if( coords[j][k] == termcoords[j][i] )
	    {
               currcoord[j] = k;
               break;
	    }
	 }
      }
      /* the position of the (future) terminal */
      k = getNodeNumber(grid_dim, -1, ncoords, currcoord) - 1;
      tmp = -1;

      if( 0 && i == 0 )
      {
	 graph->source[0] = k;
	 printf("root: (%d", termcoords[0][i]);
	 for( j = 1; j < grid_dim; j++ )
            printf(", %d", termcoords[j][i]);
	 printf(")\n");
      }

      /* make a terminal out of the node */
      graph_knot_chg(graph, k, 0, -1, -1);
   }

   graph->stp_type = STP_GRID;

   for( i = 0; i < grid_dim; i++ )
   {
      free(termcoords[i]);
      if( i < 2 )
	 free(gridedges[i]);
   }

   free(termcoords);
   free(edgecosts);
   free(gridedges);
   free(ncoords);
   free(currcoord);
   return graph;
}


/** computes coordinates of node 'node' */
void graph_grid_coordinates(
   int**  coords,
   int**  nodecoords,  /* coordinates of the node (to be computed) */
   int*   ncoords,
   int    node,
   int    grid_dim
   )
{
   int i;
   int j;
   int tmp;
   int coord;
   assert(grid_dim > 1);
   assert(node >= 0);
   assert(coords != NULL);
   assert(ncoords != NULL);
   if( *nodecoords == NULL )
      *nodecoords = (int*) malloc(grid_dim * sizeof(int));

   for( i = 0; i < grid_dim; i++ )
   {
      tmp = 1;
      for( j = i; j < grid_dim; j++ )
         tmp = tmp * ncoords[j];

      coord = node % tmp;
      tmp = tmp / ncoords[i];
      coord = coord / tmp;
      (*nodecoords)[i] = coords[i][coord];
   }
}

/** alters the graph in such a way that each optimal STP solution to the
 * new graph corresponds to an optimal Prize Collecting solution to the original graph
 */
void
graph_prize_transform(
   GRAPH* graph,
   double* prize
   )
{
   int k;
   int root;
   int node;
   int nnodes;
   int nterms;
   double tmpsum = 0.0;
   assert(graph != NULL);
   assert(graph->edges == graph->esize);
   root = graph->source[0];
   nnodes = graph->knots;
   nterms = graph->terms;
   assert(prize != NULL);
   assert(nnodes == graph->ksize);
   graph->norgmodeledges = graph->edges;
   graph->norgmodelknots = nnodes;

   /* for each terminal, except for the root, one node and three edges (i.e. six arcs) are to be added */
   graph_resize(graph, (graph->ksize + graph->terms + 1), (graph->esize + graph->terms * 6) , -1);

   for( k = 0; k < nterms; ++k )
   {
      /* create a new node */
      graph_knot_add(graph, -1, -1, -1);
   }


   /* new root */
   root = graph->knots;
   graph_knot_add(graph, 0, -1, -1);
   nterms = 0;
   graph->source[0] = root;
   for( k = 0; k < nnodes; ++k )
   {
      /* is the kth node a terminal other than the root? */
      if( Is_term(graph->term[k]) )
      {
         /* the copied node */
         node = nnodes + nterms;
         nterms++;
         /* switch the terminal property */
         graph->term[k] = -1;
         graph->term[node] = 0;
         tmpsum += prize[k];
         //printf("prize[%d] : %f \n\n", k, prize[k]);
         /* add one edge going from the root to the 'copied' terminal and one going from the former terminal to its copy */
	 graph_edge_add(graph, root, k, 0, FARAWAY);
	 graph_edge_add(graph, root, node, prize[k], FARAWAY);
         graph_edge_add(graph, k, node, 0, FARAWAY);
      }
   }
   graph->source[0] = root;
   graph->stp_type = STP_PRIZE_COLLECTING;
   assert((nterms + 1) == graph->terms);
   //printf("total TP sum: %f \n\n", tmpsum);
}


void
graph_rootprize_transform(
   GRAPH* graph,
   double* prize
   )
{
   int k;
   int root;
   int node;
   int nnodes;
   int nterms;
   double tmpsum = 0.0;
   assert(graph != NULL);
   assert(graph->edges == graph->esize);
   root = graph->source[0];
   nnodes = graph->knots;
   nterms = graph->terms;
   assert(prize != NULL);
   assert(nnodes == graph->ksize);
   assert(root >= 0);
   graph->norgmodeledges = graph->edges;
   graph->norgmodelknots = nnodes;
   /* for each terminal, except for the root, one node and three edges (i.e. six arcs) are to be added */
   graph_resize(graph, (graph->ksize + graph->terms), (graph->esize + graph->terms * 4) , -1);

   for( k = 0; k < nterms - 1; ++k )
   {
      /* create a new node */
      graph_knot_add(graph, -1, -1, -1);
   }
   /* new root */


   nterms = 0;

   for( k = 0; k < nnodes; ++k )
   {
      /* is the kth node a terminal other than the root? */
      if( Is_term(graph->term[k]) && k != root )
      {

         /* the copied node */
         node = nnodes + nterms;
         nterms++;
         /* switch the terminal property */
         graph->term[k] = -1;
         graph->term[node] = 0;
         tmpsum += prize[k];
         /* add one edge going from the root to the 'copied' terminal and one going from the former terminal to its copy */
         graph_edge_add(graph, root, node, prize[k], FARAWAY);
         graph_edge_add(graph, k, node, 0, FARAWAY);
      }
   }
   /* one for the root */
   nterms++;
   assert((nterms) == graph->terms);
   printf("total TPR sum: %f \n\n", tmpsum);
   graph->stp_type = STP_ROOTED_PRIZE_COLLECTING;
}

/** alters the graph in such a way that each optimal STP solution to the
 * new graph corresponds to an optimal Maximal NODE WEIGHT solution to the original graph
 */
void
graph_maxweight_transform(
   GRAPH* graph,
   double* maxweights
   )
{
   double* prize;
   int e;
   int i;
   int nnodes;
   int nterms = 0;

   assert(maxweights != NULL);
   assert(graph != NULL);
   assert(graph->terms == 0);
   nnodes = graph->knots;

   /* count number of terminals, modify incoming edges for non-terminals */
   for( i = 0; i < nnodes; i++ )
   {
      if( LT(maxweights[i], 0.0) )
      {
         for( e = graph->inpbeg[i]; e != EAT_LAST; e = graph->ieat[e] )
         {
            graph->cost[e] -= maxweights[i];
	    //printf("edgecost2:  %d-%d  %.12f  \n", graph->tail[e], graph->head[e], graph->cost[e]);
         }
      }
      else
      {
	 //printf("term:  %d \n", i);
	 graph_knot_chg(graph, i, 0, -1, -1);
	 nterms++;
      }
   }
   prize = malloc((size_t)nnodes * sizeof(double));
   nterms = 0;
   for( i = 0; i < nnodes; i++ )
   {
      if( Is_term(graph->term[i]) )
      {
         assert(!LT(maxweights[i], 0.0));
         prize[i] = maxweights[i];
	 //printf("termcost/prize2:  %.12f \n", maxweights[i]);
	 nterms++;
      }
      else
      {
	 assert(LT(maxweights[i], 0.0));
	 prize[i] = 0.0;
      }
   }
   assert(nterms == graph->terms);
   graph_prize_transform(graph, prize);
#if 0
   int root = graph->source[0];
   for( e = 0; e < graph->edges; e++ )
      if( Is_term(graph->term[graph->head[e]]) && Is_term(graph->term[graph->tail[e]])  && (graph->head[e]!= root && graph->tail[e] != root) )
         printf("found a new 0 edge: %d, cost: %f \n", e, graph->cost[e] );
#endif
   free(prize);
   graph->stp_type = STP_MAX_NODE_WEIGHT;
}


#if 0

/** transform the graph to enabls solving of group Steiner problems */
double graph_group_convert(
   GRAPH* g)
{
   double  max_cost = 0.0;
   double  fixed    = 0.0;
   int     real_knots;
   int     pseudo;
   int     i;

   assert(g != NULL);

   if (g->groups < 2)
      return 0.0;

   assert(graph_valid(g));

   graph_resize(g, g->knots + g->groups, g->edges + 2 * g->terms,
      NO_CHANGE, NO_CHANGE);

   assert(g != NULL);

   real_knots = g->knots;

   for(i = 0; i < g->edges; i++)
      if (g->cost[i] > max_cost)
         max_cost = g->cost[i];

   /* So hoch muss man wohl sein, um bei den Reduktionen keinen Probleme
    * zu bekommen.
    */
   max_cost += 1.0;

   for(i = 0; i < g->groups; i++)
      graph_knot_add(g, 0, 0, 0, 0);

   for(i = 0; i < real_knots; i++)
   {
      if (!Is_term(g->term[i]))
         continue;

      assert(g->group[i] >= 0);

      pseudo = real_knots + g->group[i];

      graph_edge_add(g, i, pseudo, max_cost, max_cost);
      graph_knot_chg(g, i, -1, -1, NO_CHANGE, NO_CHANGE);

      /* Das ist noch nicht richtig toll.
       */
      graph_knot_chg(g, pseudo, NO_CHANGE, NO_CHANGE,
         g->xpos[i] + 1, g->ypos[i] + 1);
   }
   g->source[0] = real_knots;

   fixed = max_cost * g->groups;

   printf("Group converter: Added %d knots, cost: %g\n",
      g->groups, fixed);

   return -fixed;
}



void graph_group_compsdcost(
   const GRAPH* g,
   double*      cost)
{
   PATH*  path;
   double max_dist;
   int    i;
   int    k;
   int    j;

   assert(g    != NULL);
   assert(cost != NULL);

   if (g->groups < 2)
      return;

   path = malloc(g->knots * sizeof(*path));

   assert(path != NULL);

   for(i = 0; i < g->knots; i++)
      g->mark[i] = TRUE;

   for(k = 0; k < g->edges; k++)
      cost[k] = g->cost[k];

   for(i = 0; i < g->knots; i++)
   {
      /* Wir gehen die Gruppen anhand der Pseudoterminal durch.
       */
      if (!Is_term(g->term[i]))
         continue;

      for(k = g->outbeg[i]; k != EAT_LAST; k = g->oeat[k])
      {
         graph_path_exec(g, FSP_MODE, g->head[k], g->cost, path);

         max_dist = 0.0;

         for(j = g->outbeg[i]; j != EAT_LAST; j = g->oeat[j])
            if (path[g->head[j]].dist > max_dist)
               max_dist = path[g->head[j]].dist;

         printf("Term %d max_dist: %g\n", g->head[k], max_dist);

         /* Entweder ist sonst niemand in der Gruppe, dann ist
          * die Distanz 0 oder es sind noch welche da, dann ist da
          * auch eine positive Distanz.
          */
         assert(((g->grad[i] == 1) && EQ(max_dist, 0.0)) || GT(max_dist, 0.0));
         assert(g->head[k] == g->tail[Edge_anti(k)]);

         cost[k]            = max_dist + 1.0;
         cost[Edge_anti(k)] = max_dist + 1.0;
      }
   }
   free(path);
}


#endif
void graph_free(
   GRAPH* p,
   char   final
   )
{
   assert(p != NULL);

   free(p->locals);
   free(p->source);
   free(p->term);
   free(p->mark);
   free(p->grad);
   free(p->inpbeg);
   free(p->outbeg);
   free(p->cost);
   if( final )
   {
      free(p->tail);
      free(p->head);
      if( p->orgtail != NULL )
      {
	 assert(p->orghead != NULL);
         free(p->orgtail);
         free(p->orghead);
      }
      //TODO free list
   }
   free(p->ieat);
   free(p->oeat);
   free(p->xpos);
   free(p->ypos);
   //  free(p->ctrctknot);
   // free(p->ctrctedge);
   if( p->stp_type == STP_DEG_CONS )
      free(p->maxdeg);
   else if(p->stp_type == STP_GRID )
   {
      int i;
      for( i = 0; i < p->grid_dim; i++ )
	 free(p->grid_coordinates[i]);
      free(p->grid_coordinates);
      free(p->grid_ncoords);
   }
   free(p);
}

GRAPH* graph_copy(
   const GRAPH* p)
{
   int e;
   GRAPH* g;
   assert(p != NULL);

   g = graph_init(p->ksize, p->esize, p->layers, p->flags);

   assert(g         != NULL);
   assert(g->xpos   != NULL);
   assert(g->ypos   != NULL);
   assert(g->locals != NULL);
   assert(g->source != NULL);
   assert(g->term   != NULL);
   assert(g->mark   != NULL);
   assert(g->grad   != NULL);
   assert(g->inpbeg != NULL);
   assert(g->outbeg != NULL);
   assert(g->cost   != NULL);
   assert(g->tail   != NULL);
   assert(g->head   != NULL);
   assert(g->ieat   != NULL);
   assert(g->oeat   != NULL);
   //   assert(g->ctrctknot != NULL);
   // assert(g->ctrctedge != NULL);

   g->norgmodeledges = p->norgmodeledges;
   g->norgmodelknots = p->norgmodelknots;
   g->knots = p->knots;
   g->terms = p->terms;
   g->edges = p->edges;
   g->orgedges = p->orgedges;
   g->orgknots = p->orgknots;
   g->grid_dim = p->grid_dim;
   g->stp_type = p->stp_type;
   g->hoplimit = p->hoplimit;
   /*
     if( p->fixedges != NULL )
     {
     IDX* curr;
     IDX* tmp = NULL;
     curr = p->fixedges;
     while( curr != NULL )
     {
     memcpy(g->fixedges, curr, sizeof(*curr));
     if( tmp != NULL )
     tmp->parent = g->fixedges;
     tmp = g->fixedges;
     curr = curr->parent;
     }
     }
     else
     {
     g->fixedges = NULL;
     }



     memcpy(g->ancestors,   p->ancestors,   p->edges  * sizeof(*p->ancestors));
     for( e = 0; e < p->orgedges; p++ )
     {
     IDX* curr;
     IDX* tmp = NULL;
     curr = p->ancestors[e];
     while( curr != NULL )
     {
     memcpy(g->ancestors[e], curr, sizeof(*curr));
     if( tmp != NULL )
     tmp->parent = g->ancestors[e];
     tmp = g->ancestors[e];
     curr = curr->parent;
     }
     }
   */


   memcpy(g->locals, p->locals, p->layers * sizeof(*p->locals));
   memcpy(g->source, p->source, p->layers * sizeof(*p->source));
   memcpy(g->term,   p->term,   p->ksize  * sizeof(*p->term));
   memcpy(g->mark,   p->mark,   p->ksize  * sizeof(*p->mark));
   memcpy(g->grad,   p->grad,   p->ksize  * sizeof(*p->grad));
   memcpy(g->inpbeg, p->inpbeg, p->ksize  * sizeof(*p->inpbeg));
   memcpy(g->outbeg, p->outbeg, p->ksize  * sizeof(*p->outbeg));

   memcpy(g->cost,   p->cost,   p->esize  * sizeof(*p->cost));
   memcpy(g->tail,   p->tail,   p->esize  * sizeof(*p->tail));
   memcpy(g->head,   p->head,   p->esize  * sizeof(*p->head));
   // memcpy(g->orgtail,   p->orgtail,   p->esize  * sizeof(*p->orgtail));
   // memcpy(g->orghead,   p->orghead,   p->esize  * sizeof(*p->orghead));
   memcpy(g->ieat,   p->ieat,   p->esize  * sizeof(*p->ieat));
   memcpy(g->oeat,   p->oeat,   p->esize  * sizeof(*p->oeat));

   memcpy(g->xpos,   p->xpos,   p->ksize  * sizeof(*p->xpos));
   memcpy(g->ypos,   p->ypos,   p->ksize  * sizeof(*p->ypos));

   //  memcpy(g->ctrctknot,   p->ctrctknot,   p->ksize  * sizeof(*p->ctrctknot));
   //  memcpy(g->ctrctedge,   p->ctrctedge,   p->esize  * sizeof(*p->ctrctedge));

   if( g->stp_type == STP_DEG_CONS )
   {
      assert(p->maxdeg != NULL);
      g->maxdeg = malloc((size_t)(g->knots) * sizeof(int));
      memcpy(g->maxdeg,   p->maxdeg,   p->knots  * sizeof(*p->maxdeg));
   }
   else if( p->stp_type == STP_GRID )
   {
      int i;
      assert(p->grid_ncoords != NULL);
      assert(p->grid_coordinates != NULL);

      g->grid_coordinates = malloc((size_t)(p->grid_dim) * sizeof(int*));
      memcpy(g->grid_coordinates,   p->grid_coordinates,   p->grid_dim * sizeof(*p->grid_coordinates));
      for( i = 0; i < p->grid_dim; i++)
      {
	 g->grid_coordinates[i] = malloc((size_t)(p->terms) * sizeof(int));
	 memcpy(g->grid_coordinates[i],   p->grid_coordinates[i],   p->terms * sizeof(*(p->grid_coordinates[i])));
      }
      g->grid_ncoords = malloc((size_t)(p->grid_dim) * sizeof(int));
      memcpy(g->grid_ncoords,   p->grid_ncoords,   p->grid_dim * sizeof(*p->grid_ncoords));
   }
   assert(graph_valid(p));

   return g;
}

void graph_flags(
   GRAPH* p,
   int    flags)
{
   assert(p     != NULL);
   assert(flags >= 0);

   p->flags |= flags;
}

void graph_show(
   const GRAPH* p)
{
   int i;

   assert(p != NULL);

   for(i = 0; i < p->knots; i++)
      if (p->grad[i] > 0)
         (void)printf("Knot %d, term=%d, grad=%d, inpbeg=%d, outbeg=%d\n",
            i, p->term[i], p->grad[i], p->inpbeg[i], p->outbeg[i]);

   (void)fputc('\n', stdout);

   for(i = 0; i < p->edges; i++)
      if (p->ieat[i] != EAT_FREE)
         (void)printf("Edge %d, cost=%g, tail=%d, head=%d, ieat=%d, oeat=%d\n",
            i, p->cost[i], p->tail[i], p->head[i], p->ieat[i], p->oeat[i]);

   (void)fputc('\n', stdout);
}

void graph_ident(
   const GRAPH* p)
{
   int i;
   int ident = 0;

   assert(p != NULL);

   for(i = 0; i < p->knots; i++)
      ident += (i + 1) * (p->term[i] * 2 + p->grad[i] * 3
         + p->inpbeg[i] * 5 + p->outbeg[i] * 7);

   for(i = 0; i < p->edges; i++)
      ident += (i + 1) * ((int)p->cost[i] + p->tail[i]
         + p->head[i] + p->ieat[i] + p->oeat[i]);

   (void)printf("Graph Ident = %d\n", ident);
}

/* ARGSUSED */
void graph_knot_add(
   GRAPH* p,
   int    term,
   int    xpos,
   int    ypos)
{
   assert(p        != NULL);
   assert(p->ksize >  p->knots);
   assert(term     <  p->layers);

   p->term  [p->knots] = term;
   p->mark  [p->knots] = TRUE;
   p->grad  [p->knots] = 0;
   p->inpbeg[p->knots] = EAT_LAST;
   p->outbeg[p->knots] = EAT_LAST;
   p->xpos  [p->knots] = xpos;
   p->ypos  [p->knots] = ypos;

   if (Is_term(term))
   {
      p->terms++;
      p->locals[term]++;
   }
   p->knots++;
}

/* ARGSUSED */
void graph_knot_chg(
   GRAPH* p,
   int    knot,
   int    term,
   int    xpos,
   int    ypos)
{
   assert(p      != NULL);
   assert(knot   >= 0);
   assert(knot   < p->knots);
   assert(term   < p->layers);

   if (term != NO_CHANGE)
   {
      if (term != p->term[knot])
      {
         if (Is_term(p->term[knot]))
         {
            p->terms--;
            p->locals[p->term[knot]]--;
         }
         p->term[knot] = term;

         if (Is_term(p->term[knot]))
         {
            p->terms++;
            p->locals[p->term[knot]]++;
         }
      }
   }
   if (xpos >= 0)
      p->xpos[knot] = xpos;

   if (ypos >= 0)
      p->ypos[knot] = ypos;
}


void graph_knot_contract(
   GRAPH* p,
   int    t,
   int    s
   )
{
   typedef struct save_list
   {
      unsigned int mark;
      signed int   edge;
      signed int   knot;
      double       incost;
      double       outcost;
   } SLIST;

   SLIST* slp;
   IDX**   ancestors;
   IDX**   revancestors;
   IDX*   tsancestors = NULL;
   IDX*   stancestors = NULL;
   int    slc = 0;
   int    i;
   int    et;
   int    anti;
   int    es;
   int    cedgeout = UNKNOWN;
   int    head;
   int    tail;

   assert(p          != NULL);
   assert(t          >= 0);
   assert(t          <  p->knots);
   assert(s          >= 0);
   assert(s          <  p->knots);
   assert(s          != t);
   assert(p->grad[s] >  0);
   assert(p->grad[t] >  0);
   assert(p->layers  == 1);
   assert(graph_valid(p));

   /* Feindliche Uebernahme des Terminals.
    */
   if (Is_term(p->term[s]))
      graph_knot_chg(p, t, p->term[s], -1, -1);

   /* Die Quelle darf nicht versiegen !
    */
   if (p->source[0] == s)
      p->source[0] = t;

   slp = malloc((size_t)p->grad[s] * sizeof(SLIST));
   ancestors = malloc((size_t)p->grad[s] * sizeof(IDX*));
   revancestors = malloc((size_t)p->grad[s] * sizeof(IDX*));
   assert(slp != NULL);

   /* Liste mit Kanten des aufzuloesenden Knotens merken
    */
   for( es = p->outbeg[s]; es != EAT_LAST; es = p->oeat[es] )
   {
      assert(p->tail[es] == s);

      ancestors[slc] = NULL;
      SCIPindexListNodeAppendCopy(&(ancestors[slc]), p->ancestors[es]);
      revancestors[slc] = NULL;
      SCIPindexListNodeAppendCopy(&(revancestors[slc]), p->ancestors[Edge_anti(es)]);
      if( p->head[es] != t )
      {
         /*
           assert(EQ(p->cost[es], p->cost[Edge_anti(es)]));
         */
         slp[slc].mark = FALSE;
	 slp[slc].edge = es;
         slp[slc].knot = p->head[es];
         slp[slc].outcost = p->cost[es];
         slp[slc].incost = p->cost[Edge_anti(es)];
         slc++;

         assert(slc < p->grad[s]);
      }
      else
      {
         cedgeout = Edge_anti(es); /* The edge out of t and into s. */
	 SCIPindexListNodeAppendCopy(&stancestors, p->ancestors[es]);
	 SCIPindexListNodeAppendCopy(&tsancestors, p->ancestors[cedgeout]);
      }
   }
   assert(slc == p->grad[s] - 1);

   /* Kantenliste durchgehen
    */
   for(i = 0; i < slc; i++)
   {
      /* Hat t schon eine Kante mit diesem Ziel ?
       */
      for(et = p->outbeg[t]; et != EAT_LAST; et = p->oeat[et])
         if( p->head[et] == slp[i].knot )
            break;

      /* Keine gefunden, Kante aus der Liste muss eingefuegt werden.
       */
      if (et == EAT_LAST)
      {
         slp[i].mark = TRUE;
	 //printf("edge %d->%d not found \n ", p->tail[slp[i].edge], p->head[slp[i].edge]);
      }
      else
      {
         /* Ja ist vorhanden !
          */
         assert(et != EAT_LAST);

         /* Muessen die Kosten korrigiert werden ?
          * This is for nodes with edges to s and t.
          * Need to adjust the out and in costs of the edge
          */
         if( GT(p->cost[et], slp[i].outcost) )
	 {
	    SCIPindexListNodeFree(&((p->ancestors)[et]));
	    assert(p->ancestors[et] == NULL);
	    SCIPindexListNodeAppendCopy(&((p->ancestors)[et]), ancestors[i]);
	    SCIPindexListNodeAppendCopy(&((p->ancestors)[et]), tsancestors);
            p->cost[et] = slp[i].outcost;
	 }

         if( GT(p->cost[Edge_anti(et)], slp[i].incost) )
	 {
	    anti = Edge_anti(et);
	    SCIPindexListNodeFree(&(p->ancestors[anti]));
	    assert(p->ancestors[anti] == NULL);
	    SCIPindexListNodeAppendCopy(&((p->ancestors)[anti]), revancestors[i]);
            SCIPindexListNodeAppendCopy(&((p->ancestors)[anti]), stancestors);
            p->cost[anti] = slp[i].incost;
	 }
      }
   }

   /* Einzufuegenden Kanten einfuegen
    */
   for( i = 0; i < slc; i++ )
   {
      if( slp[i].mark )
      {
         es = p->outbeg[s];

         assert(es != EAT_LAST);
	 assert(ancestors[i] != NULL);
	 assert(revancestors[i] != NULL);

         SCIPindexListNodeFree(&(p->ancestors[es]));
	 SCIPindexListNodeAppendCopy(&(p->ancestors[es]), ancestors[i]);
	 /*
           printf("edge: %d->%d, %d \n", p->tail[slp[i].edge], p->head[slp[i].edge], slp[i].edge);
           printf("XXX1 add edge: %d %d \n", p->tail[slp[i].edge], p->head[slp[i].edge]);
           printf("XXX add edge: %d %d \n", p->tail[cedgeout], p->head[cedgeout]);
	 */
	 SCIPindexListNodeAppendCopy(&(p->ancestors[es]), tsancestors);
	 /*
           IDX* curr;
           curr = p->ancestors[es];
           while( curr != NULL )
           {
           printf("inside %d->%d ancestor: %d->%d \n", p->orgtail[es], p->orghead[es], p->orgtail[curr->index], p->orghead[curr->index] );
           curr = curr->parent;
           }
	 */

         graph_edge_del(p, es);

         head = slp[i].knot;
         tail = t;

         p->grad[head]++;
         p->grad[tail]++;

         p->cost[es]     = slp[i].outcost;
         p->tail[es]     = tail;
         p->head[es]     = head;
         p->ieat[es]     = p->inpbeg[head];
         p->oeat[es]     = p->outbeg[tail];
         p->inpbeg[head] = es;
         p->outbeg[tail] = es;

	 es = Edge_anti(es);
         SCIPindexListNodeFree(&(p->ancestors[es]));

	 SCIPindexListNodeAppendCopy(&(p->ancestors[es]), revancestors[i]);
	 SCIPindexListNodeAppendCopy(&(p->ancestors[es]), stancestors);

         p->cost[es]     = slp[i].incost;
         p->tail[es]     = head;
         p->head[es]     = tail;
         p->ieat[es]     = p->inpbeg[tail];
         p->oeat[es]     = p->outbeg[head];
         p->inpbeg[tail] = es;
         p->outbeg[head] = es;
      }
   }
   /* Alle uebrigen Kanten Loeschen.
    */
   while(p->outbeg[s] != EAT_LAST)
   {
      es = p->outbeg[s];
      SCIPindexListNodeFree(&(p->ancestors[es]));
      p->ancestors[es] = NULL;
      graph_edge_del(p, es);
   }

   //TODO free ancestors?
   /* p->ctrctedge[cedgeout] = p->ctrctknot[s];
      p->ctrctknot[t] = cedgeout;*/
   /* SCIPindexListNodeFree(&stancestors);
      SCIPindexListNodeFree(&tsancestors);*/
   for( i = 0; i < slc; i++ )
   {
      SCIPindexListNodeFree(&(ancestors[i]));
      SCIPindexListNodeFree(&(revancestors[i]));
   }

   free(ancestors);
   free(revancestors);
   free(slp);

   assert(p->grad[s]   == 0);
   assert(p->outbeg[s] == EAT_LAST);
   assert(p->inpbeg[s] == EAT_LAST);
   assert(graph_valid(p));
}


/* ARGSUSED */
void graph_edge_add(
   GRAPH* p,
   int    tail,
   int    head,
   double cost1,
   double cost2)
{
   int    e;

   assert(p      != NULL);
   assert(GE(cost1, 0));
   assert(GE(cost2, 0));
   assert(tail   >= 0);
   assert(tail   <  p->knots);
   assert(head   >= 0);
   assert(head   <  p->knots);

   assert(p->esize >= p->edges + 2);

   e = p->edges;

   p->grad[head]++;
   p->grad[tail]++;
   //  p->ctrctknot[head]   = EAT_LAST;
   //  p->ctrctknot[tail]   = EAT_LAST;

   p->cost[e]           = cost1;
   p->tail[e]           = tail;
   p->head[e]           = head;
   p->ieat[e]           = p->inpbeg[head];
   p->oeat[e]           = p->outbeg[tail];
   p->inpbeg[head]      = e;
   p->outbeg[tail]      = e;
   // p->ctrctedge[e]      = EAT_LAST;

   e++;

   p->cost[e]           = cost2;
   p->tail[e]           = head;
   p->head[e]           = tail;
   p->ieat[e]           = p->inpbeg[tail];
   p->oeat[e]           = p->outbeg[head];
   p->inpbeg[tail]      = e;
   p->outbeg[head]      = e;
   // p->ctrctedge[e]      = EAT_LAST;

   p->edges += 2;
}

inline static void edge_remove(
   GRAPH* p,
   int    e)
{
   int    i;
   int    head;
   int    tail;

   assert(p          != NULL);
   assert(e          >= 0);
   assert(e          <  p->edges);

   head = p->head[e];
   tail = p->tail[e];

   if (p->inpbeg[head] == e)
      p->inpbeg[head] = p->ieat[e];
   else
   {
      for(i = p->inpbeg[head]; p->ieat[i] != e; i = p->ieat[i])
         assert(i >= 0);

      p->ieat[i] = p->ieat[e];
   }
   if (p->outbeg[tail] == e)
      p->outbeg[tail] = p->oeat[e];
   else
   {
      for(i = p->outbeg[tail]; p->oeat[i] != e; i = p->oeat[i])
         assert(i >= 0);

      p->oeat[i] = p->oeat[e];
   }
}

void graph_edge_del(
   GRAPH* p,
   int    e)
{
   assert(p          != NULL);
   assert(e          >= 0);
   assert(e          <  p->edges);

   /* Immer mit der ersten von beiden Kanten anfangen
    */
   e -= e % 2;

   assert(p->head[e] == p->tail[e + 1]);
   assert(p->tail[e] == p->head[e + 1]);

   p->grad[p->head[e]]--;
   p->grad[p->tail[e]]--;

   edge_remove(p, e);

   assert(p->ieat[e] != EAT_FREE);
   assert(p->ieat[e] != EAT_HIDE);
   assert(p->oeat[e] != EAT_FREE);
   assert(p->oeat[e] != EAT_HIDE);

   p->ieat[e] = EAT_FREE;
   p->oeat[e] = EAT_FREE;

   e++;

   edge_remove(p, e);

   assert(p->ieat[e] != EAT_FREE);
   assert(p->ieat[e] != EAT_HIDE);
   assert(p->oeat[e] != EAT_FREE);
   assert(p->oeat[e] != EAT_HIDE);

   p->ieat[e] = EAT_FREE;
   p->oeat[e] = EAT_FREE;
}

void graph_edge_hide(
   GRAPH* p,
   int    e)
{
   assert(p          != NULL);
   assert(e          >= 0);
   assert(e          <  p->edges);

   /* Immer mit der ersten von beiden Anfangen
    */
   e -= e % 2;

   assert(p->head[e] == p->tail[e + 1]);
   assert(p->tail[e] == p->head[e + 1]);

   p->grad[p->head[e]]--;
   p->grad[p->tail[e]]--;

   edge_remove(p, e);

   assert(p->ieat[e] != EAT_FREE);
   assert(p->ieat[e] != EAT_HIDE);
   assert(p->oeat[e] != EAT_FREE);
   assert(p->oeat[e] != EAT_HIDE);

   p->ieat[e] = EAT_HIDE;
   p->oeat[e] = EAT_HIDE;

   e++;

   edge_remove(p, e);

   assert(p->ieat[e] != EAT_FREE);
   assert(p->ieat[e] != EAT_HIDE);
   assert(p->oeat[e] != EAT_FREE);
   assert(p->oeat[e] != EAT_HIDE);

   p->ieat[e] = EAT_HIDE;
   p->oeat[e] = EAT_HIDE;
}

void graph_uncover(
   GRAPH* p)
{
   int head;
   int tail;
   int e;

   assert(p      != NULL);

   for(e = 0; e < p->edges; e++)
   {
      if (p->ieat[e] == EAT_HIDE)
      {
         assert(e % 2 == 0);
         assert(p->oeat[e] == EAT_HIDE);

	 head            = p->head[e];
	 tail            = p->tail[e];

	 p->grad[head]++;
	 p->grad[tail]++;

	 p->ieat[e]      = p->inpbeg[head];
	 p->oeat[e]      = p->outbeg[tail];
	 p->inpbeg[head] = e;
	 p->outbeg[tail] = e;

         e++;

         assert(p->ieat[e] == EAT_HIDE);
         assert(p->oeat[e] == EAT_HIDE);
         assert(p->head[e] == tail);
         assert(p->tail[e] == head);

	 head            = p->head[e];
	 tail            = p->tail[e];
	 p->ieat[e]      = p->inpbeg[head];
	 p->oeat[e]      = p->outbeg[tail];
	 p->inpbeg[head] = e;
	 p->outbeg[tail] = e;
      }
   }
}

GRAPH *graph_pack(
   GRAPH* p)
{
   const char* msg1   = " ok\nKnots: %d  Edges: %d  Terminals: %d\n";

   GRAPH* q;
   int*   new;
   int    knots = 0;
   int    edges = 0;
   int    i;
   int    l;

   assert(p      != NULL);
   assert(graph_valid(p));

   (void)printf("Packing Graph: ");

   new = malloc((size_t)p->knots * sizeof(new[0]));

   assert(new != NULL);

   /* Knoten zaehlen
    */
   for(i = 0; i < p->knots; i++)
   {
      new[i] = knots;

      /* Hat der Knoten noch Kanten ?
       */
      if (p->grad[i] > 0)
         knots++;
      else
         new[i] = -1;
   }

   /* Ist ueberhaupt noch ein Graph vorhanden ?
    */
   if (knots == 0)
   {
      free(new);
      new = NULL;

      printf(" Graph vanished!\n");
      knots = 1;
   }

   /* Kanten zaehlen
    */
   for(i = 0; i < p->edges; i++)
   {
      if (p->oeat[i] != EAT_FREE)
      {
         assert(p->ieat[i] != EAT_FREE);
         edges++;
      }
   }

   q = graph_init(knots, edges, p->layers, p->flags);
   q->norgmodelknots = p->norgmodelknots;
   q->norgmodeledges = p->norgmodeledges;
   q->orgtail = p->orgtail;
   q->orghead = p->orghead;
   q->orgknots = p->knots;
   q->orgedges = p->edges;
   q->stp_type = p->stp_type;
   q->maxdeg = p->maxdeg;
   q->grid_dim = p->grid_dim;
   q->grid_ncoords = p->grid_ncoords;
   q->grid_coordinates = p->grid_coordinates;
   q->fixedges = p->fixedges;
   q->hoplimit = p->hoplimit;
   if( new == NULL )
   {

      q->ancestors = p->ancestors;
      graph_free(p, FALSE);
      graph_knot_add(q, 0, -1, -1);
      q->source[0] = 0;
      return q;
   }

   q->ancestors = malloc((size_t)(edges) * sizeof(IDX*));
   for( i = 0; i < edges; i++ )
      q->ancestors[i] = NULL;

   /* Knoten umladen
    */
   for(i = 0; i < p->knots; i++)
   {
      assert(p->term[i] < p->layers);
#if 0
      if ((i % 100) == 0)
      {
         (void)fputc('k', stdout);
         (void)fflush(stdout);
      }
#endif
      if( p->grad[i] > 0 )
         graph_knot_add(q, p->term[i], p->xpos[i], p->ypos[i]);
   }

   /* Kanten umladen
    */
   for( i = 0; i < p->edges; i += 2 )
   {
#if 0
      if ((i % 1000) == 0)
      {
         (void)fputc('e', stdout);
         (void)fflush(stdout);
      }
#endif
      if (p->ieat[i] == EAT_FREE)
      {
         assert(p->oeat[i]     == EAT_FREE);
         assert(p->ieat[i + 1] == EAT_FREE);
         assert(p->oeat[i + 1] == EAT_FREE);
	 SCIPindexListNodeFree(&(p->ancestors[i]));
         SCIPindexListNodeFree(&(p->ancestors[i + 1]));
         continue;
      }
      assert(p->ieat[i]      != EAT_FREE);
      assert(p->oeat[i]      != EAT_FREE);
      assert(p->ieat[i + 1]  != EAT_FREE);
      assert(p->oeat[i + 1]  != EAT_FREE);
      assert(new[p->tail[i]] >= 0);
      assert(new[p->head[i]] >= 0);

      SCIPindexListNodeAppendCopy(&(q->ancestors[q->edges]), p->ancestors[i]);
      SCIPindexListNodeAppendCopy(&(q->ancestors[q->edges + 1]), p->ancestors[i + 1]);
      graph_edge_add(q, new[p->tail[i]], new[p->head[i]],
         p->cost[i], p->cost[Edge_anti(i)]);

      SCIPindexListNodeFree(&(p->ancestors[i]));
      SCIPindexListNodeFree(&(p->ancestors[i + 1]));
   }

   /* Wurzeln umladen
    */
   for(l = 0; l < q->layers; l++)
   {
      assert(q->term[new[p->source[l]]] == l);
      q->source[l] = new[p->source[l]];
   }

   free(new);

   p->stp_type = UNKNOWN;
   graph_free(p, FALSE);

#if 0
   for(l = 0; l < q->layers; l++)
      q->source[l] = -1;

   for(i = 0; i < q->knots; i++)
      if ((q->term[i] >= 0) && ((q->source[q->term[i]] < 0)
            || (q->grad[i] > q->grad[q->source[q->term[i]]])))
         q->source[q->term[i]] = i;
#endif
   assert(q->source[0] >= 0);

   (void)printf(msg1, q->knots, q->edges, q->terms);

   return(q);
}

void graph_trail(
   const GRAPH* p,
   int          i)
{
   int   k;

   assert(p      != NULL);
   assert(i      >= 0);
   assert(i      <  p->knots);

   if (!p->mark[i])
   {
      p->mark[i] = TRUE;

      for(k = p->outbeg[i]; k != EAT_LAST; k = p->oeat[k])
         if (!p->mark[p->head[k]])
            graph_trail(p, p->head[k]);
   }
}

int graph_valid(
   const GRAPH* p)
{
   const char* fehler1  = "*** Graph Validation Error: Head invalid, Knot %d, Edge %d, Tail=%d, Head=%d\n";
   const char* fehler2  = "*** Graph Validation Error: Tail invalid, Knot %d, Edge %d, Tail=%d, Head=%d\n";
   const char* fehler3  = "*** Graph Validation Error: Source invalid, Layer %d, Source %d, Terminal %d\n";
   const char* fehler4  = "*** Graph Validation Error: FREE invalid, Edge %d/%d\n";
   const char* fehler5  = "*** Graph Validation Error: Anti invalid, Edge %d/%d, Tail=%d/%d, Head=%d/%d\n";
   const char* fehler6  = "*** Graph Validation Error: Knot %d with Grad 0 has Edges\n";
   const char* fehler7  = "*** Graph Validation Error: Knot %d not connected\n";
   const char* fehler8  = "*** Graph Validation Error: Wrong locals count, Layer %d, count is %d, should be %d\n";
   const char* fehler9  = "*** Graph Validation Error: Wrong Terminal count, count is %d, should be %d\n";

   int    k;
   int    l;
   int    e;
   int    terms;
   int*   locals;

   assert(p      != NULL);

   terms  = p->terms;
   locals = malloc((size_t)p->layers * sizeof(int));

   assert(locals != NULL);

   for(l = 0; l < p->layers; l++)
      locals[l] = p->locals[l];

   for(k = 0; k < p->knots; k++)
   {
      if (Is_term(p->term[k]))
      {
         locals[p->term[k]]--;
         terms--;
      }
      for(e = p->inpbeg[k]; e != EAT_LAST; e = p->ieat[e])
         if (p->head[e] != k)
            break;

      if (e != EAT_LAST)
         return((void)fprintf(stderr, fehler1, k, e, p->tail[e], p->head[e]), FALSE);

      for(e = p->outbeg[k]; e != EAT_LAST; e = p->oeat[e])
         if (p->tail[e] != k)
            break;

      if (e != EAT_LAST)
         return((void)fprintf(stderr, fehler2, k, e, p->tail[e], p->head[e]), FALSE);
   }
   if (terms != 0)
      return((void)fprintf(stderr, fehler9, p->terms, p->terms - terms), FALSE);

   for(l = 0; l < p->layers; l++)
   {
      if (locals[l] != 0)
         return((void)fprintf(stderr, fehler8,
               l, p->locals[l], p->locals[l] - locals[l]), FALSE);

      if ((p->source[l] < 0)
         || (p->source[l] >= p->knots)
         || (p->term[p->source[l]] != l))
         return((void)fprintf(stderr, fehler3,
               l, p->source[l], p->term[p->source[l]]), FALSE);
   }
   free(locals);

   for(e = 0; e < p->edges; e += 2)
   {
      if ((p->ieat[e    ] == EAT_FREE) && (p->oeat[e    ] == EAT_FREE)
         && (p->ieat[e + 1] == EAT_FREE) && (p->oeat[e + 1] == EAT_FREE))
         continue;

      if ((p->ieat[e] == EAT_FREE) || (p->oeat[e] == EAT_FREE)
         || (p->ieat[e + 1] == EAT_FREE) || (p->oeat[e + 1] == EAT_FREE))
         return((void)fprintf(stderr, fehler4, e, e + 1), FALSE);

      if ((p->head[e] != p->tail[e + 1]) || (p->tail[e] != p->head[e + 1]))
         return((void)fprintf(stderr, fehler5,
               e, e + 1, p->head[e], p->tail[e + 1],
               p->tail[e], p->head[e + 1]), FALSE);

   }
   for(k = 0; k < p->knots; k++)
      p->mark[k] = FALSE;

   graph_trail(p, p->source[0]);

   for(k = 0; k < p->knots; k++)
   {
      if ((p->grad[k] == 0)
         && ((p->inpbeg[k] != EAT_LAST) || (p->outbeg[k] != EAT_LAST)))
         return((void)fprintf(stderr, fehler6, k), FALSE);

      if (!p->mark[k] && (p->grad[k] > 0) && p->stp_type != STP_PRIZE_COLLECTING && p->stp_type != STP_MAX_NODE_WEIGHT)
         return((void)fprintf(stderr, fehler7, k), FALSE);
   }
   return(TRUE);
}
char graph_sol_valid(
   const GRAPH* graph,
   int* result

   )
{
   SCIP_QUEUE* queue;

   char* terminal;
   int* pnode;
   int e;
   int i;
   int root;
   int nnodes;
   int termcount;
   assert(graph != NULL);
   assert(result != NULL);
   nnodes = graph->knots;
   root = graph->source[0];
   assert(root >= 0);

   terminal = malloc((size_t)nnodes * sizeof(char));
   for( i = 0; i < nnodes; i++ )
      terminal[i] = FALSE;
   /* BFS until all terminals are reached */
   SCIP_CALL( SCIPqueueCreate(&queue, nnodes, 2) );

   SCIP_CALL( SCIPqueueInsert(queue, &root) );
   termcount = 1;
   terminal[root] = TRUE;

   while( !SCIPqueueIsEmpty(queue) )
   {
      pnode = (SCIPqueueRemove(queue));
      for( e = graph->outbeg[*pnode]; e != EAT_LAST; e = graph->oeat[e] )
      {

         if( result[e] == CONNECT )
         {
            i = graph->head[e];
            if( Is_term(graph->term[i]) )
            {
               assert(!terminal[i]);
               terminal[i] = TRUE;
               termcount++;
            }
            SCIP_CALL( SCIPqueueInsert(queue, &graph->head[e]) );
         }
      }

   }

   if (termcount != graph->terms)
   {
      for( i = 0; i < nnodes; i++ )
         if( Is_term(graph->term[i]) && !terminal[i] )
            printf("not reached, node: %d\n", i);
      printf("a: %d, b: %d: \n", termcount, graph->terms);
      assert(0);
   }

   free(terminal);
   SCIPqueueFree(&queue);

   return (termcount == graph->terms);
}


char graph_valid2(
   SCIP* scip,
   const GRAPH* graph,
   SCIP_Real* cost

   )
{

   SCIP_QUEUE* queue;

   char* terminal;
   char* reached;
   int* pnode;
   int e;
   int i;
   int root;
   int nnodes;
   int termcount;
   assert(graph != NULL);
   assert(cost != NULL);
   nnodes = graph->knots;
   root = graph->source[0];
   assert(root >= 0);

   terminal = malloc((size_t)nnodes * sizeof(char));
   reached = malloc((size_t)nnodes * sizeof(char));
   for( i = 0; i < nnodes; i++ )
   {
      terminal[i] = FALSE;
      reached[i] = FALSE;
   }
   /* BFS until all terminals are reached */
   SCIP_CALL( SCIPqueueCreate(&queue, nnodes, 2) );

   SCIP_CALL( SCIPqueueInsert(queue, &root) );
   termcount = 1;
   terminal[root] = TRUE;
   reached[root] = TRUE;
   while( !SCIPqueueIsEmpty(queue) )
   {
      pnode = (SCIPqueueRemove(queue));
      for( e = graph->outbeg[*pnode]; e != EAT_LAST; e = graph->oeat[e] )
      {

         if( SCIPisLT(scip, cost[e], 1e+10 - 10 ) && !reached[graph->head[e]] )
         {
            i = graph->head[e];
	    reached[i] = TRUE;
            if( Is_term(graph->term[i]) )
            {
               assert(  !terminal[i]   );
               terminal[i] = TRUE;
               termcount++;

            }
            SCIP_CALL( SCIPqueueInsert(queue, &graph->head[e]) );
         }
      }

   }
   free(terminal);
   free(reached);
   SCIPqueueFree(&queue);
   if (termcount != graph->terms)
   {
      for( i = 0; i < nnodes; i++ )
         if( Is_term(graph->term[i]) && !terminal[i] )
            printf("not reached, node: %d\n", i);
      printf("a: %d, b: %d: \n", termcount, graph->terms);
      assert(0);
   }

   return (termcount == graph->terms);
}
