/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2009 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: cons_soc.c,v 1.3 2009/10/22 12:43:47 bzfviger Exp $"

/**@file   cons_soc.c
 * @ingroup CONSHDLRS 
 * @brief  constraint handler for second order cone constraints
 * @author Stefan Vigerske
 * @author Marc Pfetsch
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/cons_soc.h"
#include "scip/cons_quadratic.h"
#include "scip/cons_linear.h"
#include "scip/heur_nlp.h"
#include "scip/nlpi.h"
#include "scip/intervalarith.h"


/* constraint handler properties */
#define CONSHDLR_NAME          "soc"
#define CONSHDLR_DESC          "constraint handler for second order cone constraints"
#define CONSHDLR_SEPAPRIORITY        10 /**< priority of the constraint handler for separation */
#define CONSHDLR_ENFOPRIORITY       -40 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY      -10 /**< priority of the constraint handler for checking feasibility */
#define CONSHDLR_SEPAFREQ             5 /**< frequency for separating cuts; zero means to separate only in the root node */
#define CONSHDLR_PROPFREQ            20 /**< frequency for propagating domains; zero means only preprocessing propagation */
#define CONSHDLR_EAGERFREQ          100 /**< frequency for using all instead of only the useful constraints in separation,
                                              *   propagation and enforcement, -1 for no eager evaluations, 0 for first only */
#define CONSHDLR_MAXPREROUNDS        -1 /**< maximal number of presolving rounds the constraint handler participates in (-1: no limit) */
#define CONSHDLR_DELAYSEPA        FALSE /**< should separation method be delayed, if other separators found cuts? */
#define CONSHDLR_DELAYPROP        FALSE /**< should propagation method be delayed, if other propagators found reductions? */
#define CONSHDLR_DELAYPRESOL      FALSE /**< should presolving method be delayed, if other presolvers found reductions? */
#define CONSHDLR_NEEDSCONS         TRUE /**< should the constraint handler be skipped, if no constraints are available? */

#define QUADCONSUPGD_PRIORITY         0 /**< priority of the constraint handler for upgrading of quadratic constraints */

/*
 * Data structures
 */

/** Eventdata for variable bound change events. */
struct SCIP_EventData
{
   SCIP_CONSDATA*        consdata;           /**< the constraint data */
   int                   varidx;             /**< the index of a variable on the left hand side which bound change is catched, or -1 for variable on right hand side */
   int                   filterpos;          /**< position of corresponding event in event filter */
};

/** constraint data for soc constraints */
struct SCIP_ConsData
{
   int                   nvars;              /**< number of variables on left hand side (n) */
   SCIP_VAR**            vars;               /**< variables on left hand side (x_i) */
   SCIP_Real*            coefs;              /**< coefficients for variables on left hand side (alpha_i) */
   SCIP_Real*            offsets;            /**< offsets for variables on left hand side (beta_i) */
   SCIP_Real             constant;           /**< constant on left hand side (gamma) */
   
   SCIP_VAR*             rhsvar;             /**< variable on right hand side (x_{n+1}) */
   SCIP_Real             rhscoeff;           /**< coefficient of square term on right hand side (alpha_{n+1}) */
   SCIP_Real             rhsoffset;          /**< offset for variable on right hand side (beta_{n+1}) */
   
   SCIP_Real             lhsval;             /**< value of left hand side in current point */
   SCIP_Real             violation;          /**< violation of constraint in current point */
   
   SCIP_EVENTDATA*       lhsbndchgeventdatas;/**< eventdata for bound change events on left  hand side variables */
   SCIP_EVENTDATA        rhsbndchgeventdata; /**< eventdata for bound change event  on right hand side variable  */
   SCIP_Bool             ispropagated;       /**< does the domains need to be propagated? */
   SCIP_Bool             isapproxadded;      /**< has a linear outer approximation be added? */
};

/** constraint handler data */
struct SCIP_ConshdlrData
{
   SCIP_EVENTHDLR*       eventhdlr;      /**< event handler for bound change events */
   SCIP_HEUR*            nlpheur;        /**< a pointer to the NLP heuristic */
   
   SCIP_Bool             glineur;        /**< is the Glineur outer approx prefered to Ben-Tal Nemirovski? */
   SCIP_Bool             doscaling;      /**< are constraint violations scaled? */
   SCIP_Bool             projectpoint;   /**< is the point in which a cut is generated projected onto the feasible set? */
   int                   nauxvars;       /**< number of auxiliary variables to use when creating a linear outer approx. of a SOC3 constraint */
   int                   branchfreq;     /**< frequency of branching on a node instead of adding weak cuts */
   SCIP_Longint          nextbranchnode; /**< (lower bound on) index of node where to branch next time if adding weak cuts fails */ 
};



/*
 * Local methods
 */

/** catch variable events */
static
SCIP_RETCODE catchVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons                /**< constraint for which to catch bound change events */      
   )
{
   SCIP_CONSDATA* consdata;
   int            i;
   
   assert(scip      != NULL);
   assert(cons      != NULL);
   assert(eventhdlr != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata  != NULL);

   SCIP_CALL( SCIPreallocMemoryArray(scip, &consdata->lhsbndchgeventdatas, consdata->nvars) );
   for( i = 0; i < consdata->nvars; ++i )
   {
      consdata->lhsbndchgeventdatas[i].consdata = consdata;
      consdata->lhsbndchgeventdatas[i].varidx   = i;
      SCIP_CALL( SCIPcatchVarEvent(scip, consdata->vars[i], SCIP_EVENTTYPE_BOUNDTIGHTENED, eventhdlr, &consdata->lhsbndchgeventdatas[i], &consdata->lhsbndchgeventdatas[i].filterpos) );
   }
   consdata->rhsbndchgeventdata.consdata = consdata;
   consdata->rhsbndchgeventdata.varidx   = -1;
   SCIP_CALL( SCIPcatchVarEvent(scip, consdata->rhsvar, SCIP_EVENTTYPE_UBTIGHTENED, eventhdlr, &consdata->rhsbndchgeventdata, &consdata->rhsbndchgeventdata.filterpos) );
   
   consdata->ispropagated = FALSE;

   return SCIP_OKAY;
}

/** drop variable events */
static
SCIP_RETCODE dropVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons                /**< constraint for which to catch bound change events */      
   )
{
   SCIP_CONSDATA* consdata;
   int i;
   
   assert(scip      != NULL);
   assert(eventhdlr != NULL);
   assert(cons      != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata  != NULL);

   for( i = 0; i < consdata->nvars; ++i )
   {
      SCIP_CALL( SCIPdropVarEvent(scip, consdata->vars[i], SCIP_EVENTTYPE_BOUNDTIGHTENED, eventhdlr, &consdata->lhsbndchgeventdatas[i], consdata->lhsbndchgeventdatas[i].filterpos) );
   }
   
   SCIP_CALL( SCIPdropVarEvent(scip, consdata->rhsvar, SCIP_EVENTTYPE_UBTIGHTENED, eventhdlr, &consdata->rhsbndchgeventdata, consdata->rhsbndchgeventdata.filterpos) );

   return SCIP_OKAY;
}

/** processed variable bound tightening event */
static
SCIP_DECL_EVENTEXEC(processVarEvent)
{
   SCIP_CONSDATA* consdata;
   
   assert(scip      != NULL);
   assert(event     != NULL);
   assert(eventdata != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), CONSHDLR_NAME) == 0);

   consdata = eventdata->consdata;
   assert(consdata  != NULL);
   
   consdata->ispropagated = FALSE;
   /* @todo look at bounds on x_i to decide whether propagation makes sense */

   return SCIP_OKAY;
}

#ifdef QUADCONSUPGD_PRIORITY
/** tries to upgrade a quadratic constraint to a SOC constraint
 * @todo more general quadratic constraints then sums of squares might allow an upgrade to a SOC
 */
static
SCIP_DECL_QUADCONSUPGD(upgradeConsQuadratic)
{
   int            nquadvars;
   SCIP_VAR*      var;
   SCIP_VAR**     lhsvars;
   SCIP_Real*     lhscoefs = NULL;
   SCIP_Real*     lhsoffsets = NULL;
   SCIP_Real      lhsconstant = 0.0;
   int            lhscount = 0;
   SCIP_VAR*      rhsvar = NULL; 
   SCIP_Real      rhscoef = 0.0;
   SCIP_Real      rhsoffset = 0.0;
   SCIP_Real      sqrcoef, lincoef;
   int            i, j;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(upgdconslhs != NULL);
   assert(upgdconsrhs != NULL);
   
   *upgdconslhs = NULL;
   *upgdconsrhs = NULL;
   
   SCIPdebugMessage("upgradeConsQuadratic called for constraint <%s>\n", SCIPconsGetName(cons));
   SCIPdebug( SCIP_CALL( SCIPprintCons(scip, cons, NULL) ) );
   
   /* currently do not support linear parts in upgrading of SOC constraints */
   if( SCIPgetNLinearVarsQuadratic(scip, cons) )
      return SCIP_OKAY;

   /* currently do not support bilinear parts in upgrading of SOC constraints */
   if( SCIPgetNBilinTermsQuadratic(scip, cons) )
      return SCIP_OKAY;

   nquadvars = SCIPgetNQuadVarsQuadratic(scip, cons);

   SCIP_CALL( SCIPallocBufferArray(scip, &lhsvars, nquadvars-1) );

   if( !SCIPisInfinity(scip, SCIPgetRhsQuadratic(scip, cons)) )
   { /* try whether constraint on right hand side is SOC */
      lhsconstant = -SCIPgetRhsQuadratic(scip, cons);

      for( i = 0; i < nquadvars; ++i )
      {
         var  = SCIPgetQuadVarsQuadratic(scip, cons)[i];
         sqrcoef = SCIPgetSqrCoefsQuadVarsQuadratic(scip, cons)[i];
         lincoef = SCIPgetLinearCoefsQuadVarsQuadratic(scip, cons)[i];
         
         assert(sqrcoef != 0.0);
            
         if( sqrcoef > 0.0 )
         {
            if( lhscount >= nquadvars - 1 )
            { /* too many variables on lhs, i.e., all variables seem to have positive coefficient */
               rhsvar = NULL;
               break;
            }
            
            lhsvars[lhscount] = var;

            if( sqrcoef != 1.0 )
            {
               if( !lhscoefs )
               {
                  SCIP_CALL( SCIPallocBufferArray(scip, &lhscoefs, nquadvars - 1) );
                  for( j = 0; j < lhscount; ++j )
                     lhscoefs[j] = 1.0;
               }
               lhscoefs[lhscount] = sqrt(sqrcoef);
            }
            else if( lhscoefs )
               lhscoefs[lhscount] = 1.0;

            if( lincoef != 0.0 )
            {
               if( !lhsoffsets )
               {
                  SCIP_CALL( SCIPallocBufferArray(scip, &lhsoffsets, nquadvars - 1) );
                  for( j = 0; j < lhscount; ++j )
                     lhsoffsets[j] = 0.0;
               }
               lhsoffsets[lhscount] = lincoef / (2 * sqrcoef);
               lhsconstant -= lincoef * lincoef / (4 * sqrcoef);
            }
            else if( lhsoffsets )
               lhsoffsets[lhscount] = 0.0;

            ++lhscount;
         }
         else if( rhsvar != NULL || SCIPisLT(scip, SCIPvarGetLbGlobal(var), lincoef / (2*sqrcoef)) )
         { /* second variable with negative coefficient -> cannot be SOC */
            /* or lower bound of variable does not ensure positivity of right hand side */
            rhsvar = NULL;
            break;
         }
         else
         {
            rhsvar       = var;
            rhscoef      = sqrt(-sqrcoef);
            rhsoffset    = lincoef / (2 * sqrcoef);
            lhsconstant -= lincoef * lincoef / (4 * sqrcoef);
         }
      }
   }
   
   if( rhsvar && lhscount >= 2 && !SCIPisNegative(scip, lhsconstant) )
   { /* found SOC constraint, so upgrade to SOC constraint(s) (below) and relax right hand side */
      SCIPdebugMessage("found right hand side of constraint <%s> to be SOC\n", SCIPconsGetName(cons));
  
      SCIP_CALL( SCIPcreateConsSOC(scip, upgdconsrhs, SCIPconsGetName(cons),
         lhscount, lhsvars, lhscoefs, lhsoffsets, lhsconstant,
         rhsvar, rhscoef, rhsoffset,
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
         SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons),  SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons)) );
      SCIPdebug( SCIP_CALL( SCIPprintCons(scip, *upgdconsrhs, NULL) ) );
   }
   else if( !SCIPisInfinity(scip, - SCIPgetLhsQuadratic(scip, cons)) )
   { /* if the first failed, try if constraint on left hand side is SOC (using negated coefficients) */
      SCIPfreeBufferArrayNull(scip, &lhscoefs);
      SCIPfreeBufferArrayNull(scip, &lhsoffsets);
      lhscount = 0;
      rhsvar = NULL;
      
      lhsconstant = SCIPgetLhsQuadratic(scip, cons);

      for (i = 0; i < nquadvars; ++i)
      {
         var  = SCIPgetQuadVarsQuadratic(scip, cons)[i];
         sqrcoef = SCIPgetSqrCoefsQuadVarsQuadratic(scip, cons)[i];
         lincoef = SCIPgetLinearCoefsQuadVarsQuadratic(scip, cons)[i];
         
         assert(sqrcoef != 0.0);
         
         if( sqrcoef < 0.0 )
         {
            if( lhscount >= nquadvars )
            { /* too many variables on lhs, i.e., all variables seem to have negative coefficient */
               rhsvar = NULL;
               break;
            }

            lhsvars[lhscount] = var;

            if( sqrcoef != -1.0 )
            {
               if( !lhscoefs )
               {
                  SCIP_CALL( SCIPallocBufferArray(scip, &lhscoefs, nquadvars - 1) );
                  for( j = 0; j < lhscount; ++j )
                     lhscoefs[j] = 1.0;
               }
               lhscoefs[lhscount] = sqrt(-sqrcoef);
            }
            else if( lhscoefs )
               lhscoefs[lhscount] = 1.0;

            if( lincoef != 0.0 )
            {
               if( !lhsoffsets )
               {
                  SCIP_CALL( SCIPallocBufferArray(scip, &lhsoffsets, nquadvars - 1) );
                  for( j = 0; j < lhscount; ++j )
                     lhsoffsets[j] = 0.0;
               }
               lhsoffsets[lhscount] = lincoef / (2 * sqrcoef);
               lhsconstant += lincoef * lincoef / (4 * sqrcoef);
            }
            else if( lhsoffsets )
               lhsoffsets[lhscount] = 0.0;

            ++lhscount;
         }
         else if( rhsvar || SCIPisLT(scip, SCIPvarGetLbGlobal(var), lincoef / (2*sqrcoef)) )
         { /* second variable with positive coefficient -> cannot be SOC */
            /* or lower bound of variable does not ensure positivity of right hand side */
            rhsvar = NULL;
            break;
         }
         else
         {
            rhsvar       = var;
            rhscoef      = sqrt(sqrcoef);
            rhsoffset    = lincoef / (2 * sqrcoef);
            lhsconstant += lincoef * lincoef / (4 * sqrcoef);
         }
      }
      
      if (rhsvar && lhscount >= 2 && !SCIPisNegative(scip, lhsconstant))
      { /* found SOC constraint, so upgrade to SOC constraint(s) (below) and relax left hand side */
         SCIPdebugMessage("found left hand side of constraint <%s> to be SOC\n", SCIPconsGetName(cons));
     
         SCIP_CALL( SCIPcreateConsSOC(scip, upgdconslhs, SCIPconsGetName(cons),
            lhscount, lhsvars, lhscoefs, lhsoffsets, lhsconstant,
            rhsvar, rhscoef, rhsoffset,
            SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
            SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons),  SCIPconsIsLocal(cons),
            SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons)) );
         SCIPdebug( SCIP_CALL( SCIPprintCons(scip, *upgdconslhs, NULL) ) );
      }
   }

   SCIPfreeBufferArray(scip, &lhsvars);
   SCIPfreeBufferArrayNull(scip, &lhscoefs);
   SCIPfreeBufferArrayNull(scip, &lhsoffsets);
   
   return SCIP_OKAY;
}
#endif

/** evaluates the left hand side of a SOC constraint */ 
static
SCIP_RETCODE evalLhs(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint to evaluate */
   SCIP_SOL*             sol                 /**< solution to evaluate, or NULL if LP solution should be used */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real      val;
   int            i;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   consdata->lhsval = consdata->constant;
   
   for( i = 0; i < consdata->nvars; ++i )
   {
      assert(!SCIPisInfinity(scip, ABS(SCIPgetSolVal(scip, sol, consdata->vars[i]))));
      
      val = SCIPgetSolVal(scip, sol, consdata->vars[i]);
      if( consdata->offsets )
         val += consdata->offsets[i];
      if( consdata->coefs )
         val *= consdata->coefs[i];
      consdata->lhsval += val * val;      
   }
   consdata->lhsval = sqrt(consdata->lhsval);
   
   return SCIP_OKAY;
}

static
SCIP_Real getGradientNorm(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol                 /**< solution or NULL if LP solution should be used */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real      g, h;
   int            i;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   if( consdata->coefs == NULL )
   {
      if( consdata->constant == 0.0 )
         return sqrt(1 + consdata->rhscoeff * consdata->rhscoeff);
      
      g  = 1 - consdata->constant / (consdata->lhsval * consdata->lhsval);
      g += consdata->rhscoeff * consdata->rhscoeff;
      return sqrt(g);
   }
   
   g = 0.0;
   for( i = 0; i < consdata->nvars; ++i )
   {
      assert(!SCIPisInfinity(scip, ABS(SCIPgetSolVal(scip, sol, consdata->vars[i]))));
      
      h  = SCIPgetSolVal(scip, sol, consdata->vars[i]);
      if( consdata->offsets != NULL )
         h += consdata->offsets[i];
      h *= consdata->coefs[i] * consdata->coefs[i];
      g += h * h;
   }
   g /= consdata->lhsval * consdata->lhsval;
   g += consdata->rhscoeff * consdata->rhscoeff;

   return sqrt(g);
}
   
/** computes violation of a SOC constraint */
static
SCIP_RETCODE computeViolation(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint to evaluate */
   SCIP_SOL*             sol,                /**< solution to evaluate, or NULL if LP solution should be used */
   SCIP_Bool             doscaling           /**< should the violation be scaled? */
   )
{
   SCIP_CONSDATA* consdata;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   SCIP_CALL( evalLhs(scip, cons, sol) );
   
   consdata->violation = consdata->lhsval - consdata->rhscoeff * (SCIPgetSolVal(scip, sol, consdata->rhsvar) + consdata->rhsoffset);
   if( consdata->violation <= 0.0 )
   { /* constraint is not violated for sure */
      consdata->violation = 0.0;
      return SCIP_OKAY;
   }
   
   if( doscaling )
   {
      SCIP_Real norm = getGradientNorm(scip, cons, sol);
      if( norm > 1.0 )
      { /* @todo scale only if > 1.0, or should it be larger SCIPsumepsilon? */
         consdata->violation /= norm;
      }
   }

   return SCIP_OKAY;
}

/** computes violations for a set of constraints */
static
SCIP_RETCODE computeViolations(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           conss,              /**< constraints to evaluate */
   int                   nconss,             /**< number of constraints to evaluate */
   SCIP_SOL*             sol,                /**< solution to evaluate, or NULL if LP solution should be used */
   SCIP_Bool             doscaling,          /**< should the violation be scaled? */
   SCIP_CONS**           maxviolcons         /**< a buffer to store pointer to maximal violated constraint, or NULL if of no interest */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real      maxviol = 0.0;
   int            c;

   assert(scip  != NULL);
   assert(conss != NULL || nconss == 0);
   
   if( maxviolcons != NULL )
      *maxviolcons = NULL;
   
   for( c = 0; c < nconss; ++c )
   {
      SCIP_CALL( computeViolation(scip, conss[c], sol, doscaling) );
      if( maxviolcons != NULL )
      {
         consdata = SCIPconsGetData(conss[c]);
         assert(consdata != NULL);
         if( consdata->violation > maxviol && SCIPisFeasPositive(scip, consdata->violation) )
         {
            maxviol      = consdata->violation;
            *maxviolcons = conss[c];
         }
      }
   }
   
   return SCIP_OKAY;
}


/** generate supporting hyperplane */
static
SCIP_RETCODE generateCut(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol,                /**< solution to separate, or NULL for LP solution */
   SCIP_ROW**            row                 /**< place to store cut */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real*     rowcoeff;
   SCIP_Real      rhs = 0.0;
   SCIP_Real      val;
   int            i;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(row  != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   assert(SCIPisPositive(scip, consdata->lhsval)); /* otherwise constraint is not be violated */
   
   SCIP_CALL( SCIPallocBufferArray(scip, &rowcoeff, consdata->nvars) );
   
   for( i = 0; i < consdata->nvars; ++i )
   {
      val  = SCIPgetSolVal(scip, sol, consdata->vars[i]);
      if( consdata->offsets != NULL )
         val += consdata->offsets[i];
      if( consdata->coefs != NULL )
         val *= consdata->coefs[i] * consdata->coefs[i];

      rowcoeff[i] = val / consdata->lhsval;
      
      val *= SCIPgetSolVal(scip, sol, consdata->vars[i]);
      rhs += val;
   }
   rhs /= consdata->lhsval;
   rhs -= consdata->lhsval - consdata->rhscoeff * consdata->rhsoffset;
   
   SCIP_CALL( SCIPcreateEmptyRow(scip, row, "soccut", -SCIPinfinity(scip), rhs, SCIPconsIsLocal(cons), TRUE, TRUE) );
   SCIP_CALL( SCIPaddVarsToRow(scip, *row, consdata->nvars, consdata->vars, rowcoeff) );
   SCIP_CALL( SCIPaddVarToRow(scip, *row, consdata->rhsvar, -consdata->rhscoeff) );
   
   SCIPfreeBufferArray(scip, &rowcoeff);
   
   return SCIP_OKAY;   
}


/** generate supporting hyperplane w.r.t. solution projected on feasible set 
 * 
 * Instead of linearizing the SOC constraint in the given solution point, this function projects the point
 * first onto the feasible set of the SOC constraint (w.r.t. euclidean norm (scaled by alpha))
 * and linearizes the SOC constraint there.
 * The hope is that this produces somewhat tighter cuts.
 * 
 * The projection has only be computed for the case gamma = 0.
 * If gamma > 0, generateCut is called. 
 * 
 * Let \f$\hat x\f$ be sol or the LP solution if sol == NULL.
 * Then the projected point \f$\tilde x\f$ is given by
 * \f[
 *   \tilde x_i = \frac{\hat x_i + \lambda \beta_i}{1-\lambda},  \quad i=1,\ldots, n; \quad
 *   \tilde x_{n+1} = \frac{\hat x_{n+1} - \lambda \beta_{n+1}}{1+\lambda}
 * \f]
 * where
 * \f[
 *   \lambda = \frac{1-A}{1+A}, \qquad 
 *   A = \frac{\alpha_{n+1}(\hat x_{n+1}+\beta_{n+1})}{\sqrt{\sum_{i=1}^n (\alpha_i(\hat x_i+\beta_i))^2}}
 * \f]
 * 
 * If lambda is very close to 1, generateCut is called.
 * 
 * The generated cut is very similar to the unprojected form.
 * The only difference is in the right hand side, which is (in the case beta = 0) multiplied by 1/(1-lambda).
 * */
static
SCIP_RETCODE generateCutProjectedPoint(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol,                /**< solution to separate, or NULL for LP solution */
   SCIP_ROW**            row                 /**< place to store cut */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real*     rowcoeff;
   SCIP_Real      rhs = 0.0;
   SCIP_Real      val;
   SCIP_Real      A, lambda;
   int            i;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(row  != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   assert(SCIPisPositive(scip, consdata->lhsval)); /* otherwise constraint is not be violated */

   if( !SCIPisZero(scip, consdata->constant) )
   {  /* have not thought about this case yet */
      SCIP_CALL( generateCut(scip, cons, sol, row) );
      return SCIP_OKAY;
   }
   
   A  = consdata->rhscoeff * (SCIPgetSolVal(scip, sol, consdata->rhsvar) + consdata->rhsoffset);
   A /= consdata->lhsval;
   
   lambda = (1.0 - A) / (1.0 + A);
   
   assert(!SCIPisNegative(scip, lambda)); /* otherwise A > 1, so constraint is not violated */
   
   SCIPdebugMessage("A = %g \t lambda = %g\n", A, lambda);
   
   if( SCIPisFeasEQ(scip, lambda, 1.0) )
   {  /* avoid numerical difficulties when dividing by (1-lambda) below */ 
      SCIP_CALL( generateCut(scip, cons, sol, row) );
      return SCIP_OKAY;
   }
   
   SCIP_CALL( SCIPallocBufferArray(scip, &rowcoeff, consdata->nvars) );

   for( i = 0; i < consdata->nvars; ++i )
   {
      val  = SCIPgetSolVal(scip, sol, consdata->vars[i]);
      if( consdata->offsets != NULL )
         val += consdata->offsets[i];
      if( consdata->coefs != NULL )
         val *= consdata->coefs[i] * consdata->coefs[i];

      rowcoeff[i] = val / consdata->lhsval;
      
      val *= SCIPgetSolVal(scip, sol, consdata->vars[i]) + (consdata->offsets != NULL ? lambda * consdata->offsets[i] : 0);
      rhs += val;
   }
   rhs /= consdata->lhsval;
   rhs -= consdata->lhsval;
   rhs /= 1.0 - lambda;
   rhs -= consdata->rhscoeff * consdata->rhsoffset;
   
   SCIP_CALL( SCIPcreateEmptyRow(scip, row, "soccut", -SCIPinfinity(scip), rhs, SCIPconsIsLocal(cons), TRUE, TRUE) );
   SCIP_CALL( SCIPaddVarsToRow(scip, *row, consdata->nvars, consdata->vars, rowcoeff) );
   SCIP_CALL( SCIPaddVarToRow(scip, *row, consdata->rhsvar, -consdata->rhscoeff) );
   
   SCIPfreeBufferArray(scip, &rowcoeff);
   
   return SCIP_OKAY;   
}

/** separates a point, if possible */
static
SCIP_RETCODE separatePoint(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           conss,              /**< constraints */
   int                   nconss,             /**< number of constraints */
   int                   nusefulconss,       /**< number of constraints that seem to be useful */
   SCIP_SOL*             sol,                /**< solution to separate, or NULL for LP solution */
   SCIP_Bool             addweakcuts,        /**< whether also weak (only slightly violated) cuts should be added in a nonconvex constraint */
   SCIP_Bool             projectpoint,       /**< whether the point in which a cut is generated should be projected onto the feasible set */
   SCIP_Bool*            success             /**< buffer to store whether the point was separated */
   )
{
   SCIP_CONSDATA*     consdata;
   SCIP_Real          efficacy;
   int                c;
   SCIP_ROW*          row;

   assert(scip    != NULL);
   assert(conss   != NULL || nconss == 0);
   assert(nusefulconss <= nconss);
   assert(success != NULL);
   
   *success = FALSE;
   
   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      if( SCIPisFeasPositive(scip, consdata->violation) )
      {
         /* generate cut */
         if( projectpoint )
         {
            SCIP_CALL( generateCutProjectedPoint(scip, conss[c], sol, &row) );
         }
         else
         {
            SCIP_CALL( generateCut(scip, conss[c], sol, &row) );
         }
         if( row == NULL ) /* failed to generate cut */
            continue;

         efficacy = SCIPgetCutEfficacy(scip, sol, row);

         if( ( addweakcuts && SCIPisFeasPositive(scip, efficacy)) ||
             (!addweakcuts && SCIPisEfficacious (scip, efficacy)) )
         { /* cut cuts off solution and efficient enough */
            SCIP_CALL( SCIPaddCut(scip, sol, row, FALSE) );
            SCIP_CALL( SCIPresetConsAge(scip, conss[c]) );
            *success = TRUE;
            SCIPdebugMessage("added cut with efficacy %g\n", efficacy);
         }

         SCIP_CALL( SCIPreleaseRow (scip, &row) );
      }

      /* enforce only useful constraints
       * others are only checked and enforced if we are still feasible or have not found a separating cut yet
       */ 
      if( c >= nusefulconss && *success )
         break;
   }

   return SCIP_OKAY;
}

/** adds the linear outer-approximation of Glineur et.al. for a SOC constraint of dimension 3
 * 
 * Input is the data for a constraint \f$\sqrt{constant + (\alpha_1(x_1+offset1))^2 + (\alpha_2(x_2+offset2))^2) \leq \alpha_3(x_3+offset3)}\f$.
 * Here constant >= 0.0, alpha3 > 0.0, and the lower bound of x3 >= -offset3.
 * Also x2 = NULL is allowed, in which case the second term is assumed to be constant, and offset2 != 0 is needed.
 * */
static
SCIP_RETCODE presolveCreateGlineurApproxDim3(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< original constraint */
   SCIP_VAR*             x1,                 /**< variable x1 */
   SCIP_VAR*             x2,                 /**< variable x2 */
   SCIP_VAR*             x3,                 /**< variable x3 */
   SCIP_Real             alpha1,             /**< coefficient of x1 */
   SCIP_Real             alpha2,             /**< coefficient of x2 */
   SCIP_Real             alpha3,             /**< coefficient of x3 */
   SCIP_Real             offset1,            /**< offset of x1 */
   SCIP_Real             offset2,            /**< offset of x2 */
   SCIP_Real             offset3,            /**< offset of x3 */
   int                   N,                  /**< size of linear approximation, need to be >= 1 */
   const char*           basename            /**< string to use for building variable and constraint names */
   )
{
   SCIP_CONS*     lincons;
   SCIP_VAR*      vars[3];
   SCIP_Real      vals[3];
   char           varname[255];
   char           linname[255];
   int            i;
   SCIP_VAR**     avars;
   SCIP_VAR**     bvars;
   SCIP_Real      val;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(x1   != NULL);
   assert(x2   != NULL || !SCIPisZero(scip, offset2));
   assert(x3   != NULL);
   assert(SCIPisPositive(scip, alpha3));
   assert(SCIPisGE(scip, SCIPconsIsLocal(cons) ? SCIPvarGetLbLocal(x3) : SCIPvarGetLbGlobal(x3), -offset3));
   assert(basename != NULL);
   assert(N >= 1);
   
   SCIPdebugMessage("Creating linear Glineur outer-approximation for <%s>.\n", basename);

   SCIP_CALL( SCIPallocBufferArray(scip, &avars, N+1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bvars, N+1) );

   /* create additional variables; we do not use avars[0] and bvars[0] */
   for( i = 1; i <= N; ++i )
   {
      SCIPsnprintf(varname, 255, "soc#%s_a%d", basename, i);
      SCIP_CALL( SCIPcreateVar(scip, &avars[i], varname, -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS, SCIPconsIsInitial(cons), FALSE, NULL, NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip, avars[i]) );

      SCIPsnprintf(varname, 255, "soc#%s_b%d", basename, i);
      SCIP_CALL( SCIPcreateVar(scip, &bvars[i], varname, -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS, SCIPconsIsInitial(cons), FALSE, NULL, NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip, bvars[i]) );
   }

   /* create linear constraints for the first case
    * cos(pi) = -1, sin(pi) = 0
    * -> a_1  = - alpha1 (x1 + offset1)    ->  -alpha1*x1 - a_1  =  alpha1*offset1 
    * -> b_1 >= | alpha2 (x2 + offset2) |  ->   alpha2*x2 - b_1 <= -alpha2*offset2
    */

   vars[0] = x1;
   vals[0] = -alpha1;
   vars[1] = avars[1];
   vals[1] = -1.0;

   SCIPsnprintf(linname, 255, "soc#%s#a%d", basename, 0);
   SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, alpha1*offset1, alpha1*offset1,
      SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
      SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
      SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
      SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
      SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
   SCIP_CALL( SCIPaddCons(scip, lincons) );
   SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

   if( x2 != NULL )
   {
      vars[0] = x2;
      vals[0] = alpha2;
      vars[1] = bvars[1];
      vals[1] = -1.0;

      SCIPsnprintf(linname, 255, "soc#%s#b%d", basename, 0);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, -SCIPinfinity(scip), -alpha2*offset2,
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
         SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
         SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
         SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

      vars[0] = x2;
      vals[0] = alpha2;
      vars[1] = bvars[1];
      vals[1] = 1.0;

      SCIPsnprintf(linname, 255, "soc#%s#B%d", basename, 0);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, -alpha2*offset2, SCIPinfinity(scip),
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
         SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
         SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
         SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );
   }
   else
   { /* x2 == NULL ->  b_1 >= |alpha2*offset2| */
      if( SCIPconsIsLocal(cons) )
      {
         SCIP_CALL( SCIPchgVarLbNode(scip, NULL, bvars[1], ABS(alpha2 * offset2)) );
      }
      else
      {
         SCIP_CALL( SCIPchgVarLbGlobal(scip, bvars[1], ABS(alpha2 * offset2)) );
      }
   }

   /* create intermediate linear constraints */
   val = M_PI;
   for( i = 1; i < N; ++i )
   {
      val /= 2.0;

      vars[0] = avars[i];
      vals[0] = cos(val);
      vars[1] = bvars[i];
      vals[1] = sin(val);
      vars[2] = avars[i+1];
      vals[2] = -1.0;

      SCIPsnprintf(linname, 255, "soc#%s#a%d", basename, i);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 3, vars, vals, 0.0, 0.0,
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
         SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
         SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
         SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

      vars[0] = avars[i];
      vals[0] = -sin(val);
      vars[1] = bvars[i];
      vals[1] = cos(val);
      vars[2] = bvars[i+1];
      vals[2] = -1.0;

      SCIPsnprintf(linname, 255, "soc#%s#b%d", basename, i);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 3, vars, vals, -SCIPinfinity(scip), 0.0,
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
         SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
         SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
         SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

      vars[0] = avars[i];
      vals[0] = -sin(val);
      vars[1] = bvars[i];
      vals[1] = cos(val);
      vars[2] = bvars[i+1];
      vals[2] = 1.0;

      SCIPsnprintf(linname, 255, "soc#%s#B%d", basename, i);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 3, vars, vals, 0.0, SCIPinfinity(scip),
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
         SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
         SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
         SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );
   }

   /* create last linear constraint */
   val /= 2.0;
   vars[0] = avars[N];
   vals[0] = -cos(val);
   vars[1] = bvars[N];
   vals[1] = -sin(val);
   vars[2] = x3;
   vals[2] = alpha3;

   SCIPsnprintf(linname, 255, "soc#%s#a%d", basename, N);
   SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 3, vars, vals, -alpha3*offset3, -alpha3*offset3,
      SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
      SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
      SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
      SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
      SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
   SCIP_CALL( SCIPaddCons(scip, lincons) );
   SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

   for( i = 1; i <= N; ++i )
   {
      SCIP_CALL( SCIPreleaseVar(scip, &avars[i]) );
      SCIP_CALL( SCIPreleaseVar(scip, &bvars[i]) );
   }
   SCIPfreeBufferArray(scip, &avars);
   SCIPfreeBufferArray(scip, &bvars);

   return SCIP_OKAY;
}

/** adds the linear outer-approximation of Ben-Tal and Nemirovski for a SOC constraint of dimension 3
 * 
 * Input is the data for a constraint \f$\sqrt{constant + (\alpha_1(x_1+offset1))^2 + (\alpha_2(x_2+offset2))^2) \leq \alpha_3(x_3+offset3)}\f$.
 * Here constant >= 0.0, alpha3 > 0.0, and the lower bound of x3 >= -offset3.
 * Also x2 = NULL is allowed, in which case the second term is assumed to be constant, and offset2 != 0 is needed.
 * */
static
SCIP_RETCODE presolveCreateBenTalNemirovskiApproxDim3(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< original constraint */
   SCIP_VAR*             x1,                 /**< variable x1 */
   SCIP_VAR*             x2,                 /**< variable x2 */
   SCIP_VAR*             x3,                 /**< variable x3 */
   SCIP_Real             alpha1,             /**< coefficient of x1 */
   SCIP_Real             alpha2,             /**< coefficient of x2 */
   SCIP_Real             alpha3,             /**< coefficient of x3 */
   SCIP_Real             offset1,            /**< offset of x1 */
   SCIP_Real             offset2,            /**< offset of x2 */
   SCIP_Real             offset3,            /**< offset of x3 */
   int                   N,                  /**< size of linear approximation, need to be >= 1 */
   const char*           basename            /**< string to use for building variable and constraint names */
   )
{
   SCIP_CONS*     lincons;
   SCIP_VAR*      vars[3];
   SCIP_Real      vals[3];
   char           varname[255];
   char           linname[255];
   int            i;
   SCIP_VAR**     avars;
   SCIP_VAR**     bvars;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(x1   != NULL);
   assert(x2   != NULL || !SCIPisZero(scip, offset2));
   assert(x3   != NULL);
   assert(SCIPisPositive(scip, alpha3));
   assert(SCIPisGE(scip, SCIPconsIsLocal(cons) ? SCIPvarGetLbLocal(x3) : SCIPvarGetLbGlobal(x3), -offset3));
   assert(basename != NULL);
   assert(N >= 1);
     
   SCIPdebugMessage("Creating linear Ben-Tal Nemirovski outer-approximation for <%s>.\n", basename);

   SCIP_CALL( SCIPallocBufferArray(scip, &avars, N+1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bvars, N+1) );

   /* create additional variables */
   for( i = 0; i <= N; ++i )
   {
      SCIPsnprintf(varname, 255, "soc#%s_a%d", basename, i);
      SCIP_CALL( SCIPcreateVar(scip, &avars[i], varname, 0.0, SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS, SCIPconsIsLocal(cons), TRUE, NULL, NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip, avars[i]) );

      SCIPsnprintf(varname, 255, "soc#%s_b%d", basename, i);
      SCIP_CALL( SCIPcreateVar(scip, &bvars[i], varname, 0.0, SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS, SCIPconsIsLocal(cons), TRUE, NULL, NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip, bvars[i]) );
   }

   /* create first linear constraints - split into two because of the absolute value */
   vars[0] = avars[0];
   vals[0] = 1.0;
   vars[1] = x1;
   vals[1] = -alpha1;

   SCIPsnprintf(linname, 255, "soc#%s#a%d", basename, 0);
   SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, alpha1 * offset1, SCIPinfinity(scip),
      SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
      SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
      SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
      SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
      TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
   SCIP_CALL( SCIPaddCons(scip, lincons) );
   SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

   vars[0] = avars[0];
   vals[0] = 1.0;
   vars[1] = x1;
   vals[1] = alpha1;

   SCIPsnprintf(linname, 255, "soc#%s#A%d", basename, 0);
   SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, -alpha1 * offset1, SCIPinfinity(scip),
      SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
      SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
      SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
      SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
      TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
   SCIP_CALL( SCIPaddCons(scip, lincons) );
   SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

   if( x2 != NULL )
   {
      vars[0] = bvars[0];
      vals[0] = 1.0;
      vars[1] = x2;
      vals[1] = -alpha2;

      SCIPsnprintf(linname, 255, "soc#%s#b%d", basename, 0);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, alpha2 * offset2, SCIPinfinity(scip),
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
         SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
         SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
         TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

      vars[0] = bvars[0];
      vals[0] = 1.0;
      vars[1] = x2;
      vals[1] = alpha2;

      SCIPsnprintf(linname, 255, "soc#%s#B%d", basename, 0);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, -alpha2 * offset2, SCIPinfinity(scip),
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
         SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
         SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
         SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
         TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );
   }
   else
   { /* second summand is just a constant */
      if( SCIPconsIsLocal(cons) )
      {
         SCIP_CALL( SCIPchgVarLbNode(scip, NULL, bvars[0], ABS(alpha2 * offset2)) );
      }
      else
      {
         SCIP_CALL( SCIPchgVarLbGlobal(scip, bvars[0], ABS(alpha2 * offset2)) );
      }
   }

   /* create intermediate linear constraints */
   for( i = 1; i <= N; ++i )
   {
      SCIP_Real val;

      val = M_PI / pow(2.0, (double) (i+1));

      vars[0] = avars[i-1];
      vals[0] = cos(val);
      vars[1] = bvars[i-1];
      vals[1] = sin(val);
      vars[2] = avars[i];
      vals[2] = -1.0;

      SCIPsnprintf(linname, 255, "soc#%s#a%d", basename, i);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 3, vars, vals, 0.0, 0.0,
          SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
          SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
          SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
          SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
          TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

      vars[0] = avars[i-1];
      vals[0] = sin(val);
      vars[1] = bvars[i-1];
      vals[1] = -cos(val);
      vars[2] = bvars[i];
      vals[2] = 1.0;

      SCIPsnprintf(linname, 255, "soc#%s#b%d", basename, i);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 3, vars, vals, 0.0, SCIPinfinity(scip),
          SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
          SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
          SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
          SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
          TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

      vars[0] = avars[i-1];
      vals[0] = -sin(val);
      vars[1] = bvars[i-1];
      vals[1] = cos(val);
      vars[2] = bvars[i];
      vals[2] = 1.0;

      SCIPsnprintf(linname, 255, "soc#%s#B%d", basename, i);
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 3, vars, vals, 0.0, SCIPinfinity(scip),
          SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
          SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
          SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
          SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
          TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, lincons) );
      SCIP_CALL( SCIPreleaseCons(scip, &lincons) );
   }

   /* create last linear constraints */
   vars[0] = x3;
   vals[0] = alpha3;
   vars[1] = avars[N];
   vals[1] = -1.0;

   SCIPsnprintf(linname, 255, "soc#%s#a%d", basename, N);
   SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, -alpha3 * offset3, SCIPinfinity(scip),
       SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
       SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
       SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
       SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
       SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
   SCIP_CALL( SCIPaddCons(scip, lincons) );
   SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

   vars[0] = avars[N];
   vals[0] = tan( M_PI / pow(2.0, (double) (N+1)) );
   vars[1] = bvars[N];
   vals[1] = -1.0;

   SCIPsnprintf(linname, 255, "soc#%s#b%d", basename, i);
   SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, linname, 2, vars, vals, 0.0, SCIPinfinity(scip),
       SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons),
       SCIPconsIsEnforced(cons), SCIPconsIsChecked(cons),
       SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons),
       SCIPconsIsModifiable(cons), SCIPconsIsDynamic(cons),
       TRUE /* removable */, SCIPconsIsStickingAtNode(cons)) );
   SCIP_CALL( SCIPaddCons(scip, lincons) );
   SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

   for( i = 0; i <= N; ++i )
   {
      SCIP_CALL( SCIPreleaseVar(scip, &avars[i]) );
      SCIP_CALL( SCIPreleaseVar(scip, &bvars[i]) );
   }
   SCIPfreeBufferArray(scip, &avars);
   SCIPfreeBufferArray(scip, &bvars);

   return SCIP_OKAY;
}

/** adds a linear outer approx for a three dimensional SOC constraint
 * 
 * chooses between Ben-Tan/Nemirovski and Glineur and calls the corresponding function
 */
static
SCIP_RETCODE presolveCreateOuterApproxDim3(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< original constraint */
   SCIP_VAR*             x1,                 /**< variable x1 */
   SCIP_VAR*             x2,                 /**< variable x2 */
   SCIP_VAR*             x3,                 /**< variable x3 */
   SCIP_Real             alpha1,             /**< coefficient of x1 */
   SCIP_Real             alpha2,             /**< coefficient of x2 */
   SCIP_Real             alpha3,             /**< coefficient of x3 */
   SCIP_Real             offset1,            /**< offset of x1 */
   SCIP_Real             offset2,            /**< offset of x2 */
   SCIP_Real             offset3,            /**< offset of x3 */
   int                   N,                  /**< size of linear approximation, need to be >= 1 */
   SCIP_Bool             glineur,            /**< whether to prefer Glineur to Ben-Tal Nemirovski */
   const char*           basename            /**< string to use for building variable and constraint names */
)
{
   if (glineur)
   {
      SCIP_CALL( presolveCreateGlineurApproxDim3(scip, cons, x1, x2, x3, alpha1, alpha2, alpha3, offset1, offset2, offset3, N, basename) );
   }
   else
   {
      SCIP_CALL( presolveCreateBenTalNemirovskiApproxDim3(scip, cons, x1, x2, x3, alpha1, alpha2, alpha3, offset1, offset2, offset3, N, basename) );
   }
   
   return SCIP_OKAY;
}

/** adds linear outer approximation of Ben-Tal and Nemirovski for a constraint \f$\gamma + \sum_{i=1}^n (\alpha_i (x_i + \beta_i))^2 <= (\alpha_{n+1} (x_{n+1} + \beta_{n+1}))^2\f$ to the LP
 * 
 * if n>2, calls same function recursively;
 * if n=2, calls presolveCreateBenTalNemirovskiApproxDim3
 */
static
SCIP_RETCODE presolveCreateOuterApprox(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   nlhsvars,           /**< number of variables on left hand side (n) */
   SCIP_VAR**            lhsvars,            /**< variables on left hand side (x_i) */
   SCIP_Real*            lhscoefs,           /**< coefficients of variables on left hand side (alpha_i), or NULL if all 1.0 */
   SCIP_Real*            lhsoffsets,         /**< offsets of variable on left hand side (beta_i), or NULL if all 0.0 */
   SCIP_VAR*             rhsvar,             /**< variable on right hand side (y) */
   SCIP_Real             rhscoeff,           /**< coefficient of variable on right hand side (alpha_{n+1}) */
   SCIP_Real             rhsoffset,          /**< offset of variable on right hand side (beta_{n+1}) */
   SCIP_Real             constant,           /**< constant term (gamma) */
   const char*           basename,           /**< prefix for variable and constraint name */
   SCIP_CONS*            origcons,           /**< original constraint for which this SOC3 set is added */
   int                   soc3_nr_auxvars,    /**< number of auxiliary variables to use for a SOC3 constraint, or 0 if automatic */
   SCIP_Bool             glineur             /**< whether Glineur should be prefered to Ben-Tal Nemirovski */
   )
{
   char       name[255];
   SCIP_VAR*  auxvar1;
   SCIP_VAR*  auxvar2;

   assert(scip     != NULL);
   assert(lhsvars  != NULL);
   assert(nlhsvars >= 2);
   assert(rhsvar   != NULL);
   assert(basename != NULL);
   assert(!SCIPisNegative(scip, constant));
   
   if( nlhsvars == 1 )
   { /* end of recursion */
      assert(SCIPisPositive(scip, constant));
      SCIP_CALL( presolveCreateOuterApproxDim3(scip, origcons,
         lhsvars[0],                               NULL,           rhsvar,
         lhscoefs   != NULL ? lhscoefs[0]   : 1.0, 1.0,            rhscoeff,
         lhsoffsets != NULL ? lhsoffsets[0] : 0.0, sqrt(constant), rhsoffset,
         soc3_nr_auxvars, glineur, basename) );
      
      return SCIP_OKAY;
   }
   
   if( nlhsvars == 2 && SCIPisZero(scip, constant) )
   { /* end of recursion */
      assert(lhsvars[0] != NULL);
      assert(lhsvars[1] != NULL);
      assert(rhsvar     != NULL);
      SCIP_CALL( presolveCreateOuterApproxDim3(scip, origcons,
         lhsvars[0],                               lhsvars[1],                               rhsvar,
         lhscoefs   != NULL ? lhscoefs[0]   : 1.0, lhscoefs   != NULL ? lhscoefs[1]   : 1.0, rhscoeff,
         lhsoffsets != NULL ? lhsoffsets[0] : 0.0, lhsoffsets != NULL ? lhsoffsets[1] : 0.0, rhsoffset,
         soc3_nr_auxvars, glineur, basename) );
      
      return SCIP_OKAY;
   }
   
   if( nlhsvars == 3 || (nlhsvars == 2 && !SCIPisZero(scip, constant)) )
   { /* a bit special case too */
      /* for first two variables on lhs, create a new aux.var and a new SOC3 */
      SCIPsnprintf(name, 255, "%s#z1", basename);
      SCIP_CALL( SCIPcreateVar(scip, &auxvar1, name, 0., SCIPinfinity(scip), 0., SCIP_VARTYPE_CONTINUOUS, TRUE, FALSE, NULL, NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip, auxvar1) );

      /* constraint alpha_0 (x_0+beta0)^2 + alpha_1 (x_1+beta1)^2 <= auxvar^2 */
      SCIP_CALL( presolveCreateOuterApproxDim3(scip, origcons,
         lhsvars[0],                               lhsvars[1],                               auxvar1,
         lhscoefs   != NULL ? lhscoefs[0]   : 1.0, lhscoefs   != NULL ? lhscoefs[1]   : 1.0, 1.0,
         lhsoffsets != NULL ? lhsoffsets[0] : 0.0, lhsoffsets != NULL ? lhsoffsets[1] : 0.0, 0.0,
         soc3_nr_auxvars, glineur, basename) );

      SCIPsnprintf(name, 255, "%s_soc3", basename);
      if( nlhsvars == 3 )
      { /* create new constraint alpha_2 (x_2+beta2)^2 + auxvar^2 <= rhscoeff * (rhsvar+rhsoffset)^2 */
         SCIP_CALL( presolveCreateOuterApproxDim3(scip, origcons,
            lhsvars[2],                               auxvar1, rhsvar,
            lhscoefs   != NULL ? lhscoefs[2]   : 1.0, 1.0,     rhscoeff,
            lhsoffsets != NULL ? lhsoffsets[2] : 0.0, 0.0,     rhsoffset,
            soc3_nr_auxvars, glineur, basename) );
      }
      else
      { /* create new constraint auxvar^2 + sqrt(constant)^2 <= rhscoeff * (rhsvar+rhsoffset)^2 */
         SCIP_CALL( presolveCreateOuterApproxDim3(scip, origcons,
            auxvar1, NULL,           rhsvar,
            1.0,     1.0,            rhscoeff,
            0.0,     sqrt(constant), rhsoffset,
            soc3_nr_auxvars, glineur, basename) );
      }
            
      SCIP_CALL( SCIPreleaseVar(scip, &auxvar1) );
      
      return SCIP_OKAY;
   }
   
   /* nlhsvars >= 4 */
   
   SCIPsnprintf(name, 255, "%s#z1", basename);
   SCIP_CALL( SCIPcreateVar(scip, &auxvar1, name, 0.0, SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS, TRUE, FALSE, NULL, NULL, NULL, NULL) );
   SCIP_CALL( SCIPaddVar(scip, auxvar1) );

   /* approx for left half of lhs */
   SCIP_CALL( presolveCreateOuterApprox(scip,
      nlhsvars/2, lhsvars, lhscoefs, lhsoffsets,
      auxvar1, 1.0, 0.0,
      constant, name, origcons, soc3_nr_auxvars, glineur) );

   SCIPsnprintf(name, 255, "%s#z2", basename);
   SCIP_CALL( SCIPcreateVar(scip, &auxvar2, name, 0., SCIPinfinity(scip), 0., SCIP_VARTYPE_CONTINUOUS, TRUE, FALSE, NULL, NULL, NULL, NULL) );
   SCIP_CALL( SCIPaddVar(scip, auxvar2) );

   /* approx for right half of lhs */
   SCIP_CALL( presolveCreateOuterApprox(scip,
      nlhsvars-nlhsvars/2, &lhsvars[nlhsvars/2], lhscoefs ? &lhscoefs[nlhsvars/2] : NULL, lhsoffsets ? &lhsoffsets[nlhsvars/2] : NULL,
      auxvar2, 1.0, 0.0,
      0.0, name, origcons, soc3_nr_auxvars, glineur) );

   /* SOC constraint binding both auxvar's */
   SCIPsnprintf(name, 255, "%s_soc3", basename);
   SCIP_CALL( presolveCreateOuterApproxDim3(scip, origcons,
      auxvar1, auxvar2, rhsvar,
      1.0,     1.0,     rhscoeff,
      0.0,     0.0,     rhsoffset,
      soc3_nr_auxvars, glineur, name) );

   SCIP_CALL( SCIPreleaseVar(scip, &auxvar1) );
   SCIP_CALL( SCIPreleaseVar(scip, &auxvar2) );
   
   return SCIP_OKAY;
}

/** propagates variable bounds */
static
SCIP_RETCODE propagateBounds(
   SCIP*           scip,      /**< SCIP data structure */
   SCIP_CONSHDLR*  conshdlr,  /**< constraint handler */
   SCIP_CONS*      cons,      /**< constraint */
   SCIP_RESULT*    result,    /**< buffer to store result of propagation */
   int*            nchgbds    /**< buffer where to add number of tightened bounds */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_INTERVAL  lhsrange;
   SCIP_INTERVAL* lhsranges;
   SCIP_INTERVAL  rhsrange;
   SCIP_INTERVAL  a, b, c;
   SCIP_Bool      infeas, tightened;
   int            i;
   
   assert(scip   != NULL);
   assert(cons   != NULL);
   assert(result != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   if( consdata->ispropagated )
   {
      SCIPdebugMessage("skip propagation for constraint %s\n", SCIPconsGetName(cons));
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }
   else
   {
      SCIPdebugMessage("try propagation for constraint %s\n", SCIPconsGetName(cons));
   }
   
   *result = SCIP_DIDNOTFIND;
   consdata->ispropagated = TRUE;
   
   /* @todo do something clever to decide whether propagation should be tried */

   SCIPintervalSet(&lhsrange, consdata->constant);
   
   SCIP_CALL( SCIPallocBufferArray(scip, &lhsranges, consdata->nvars) );
   for( i = 0; i < consdata->nvars; ++i )
   {
      SCIPintervalSetBounds(&lhsranges[i],
         MIN(SCIPvarGetLbLocal(consdata->vars[i]), SCIPvarGetUbLocal(consdata->vars[i])),
         MAX(SCIPvarGetLbLocal(consdata->vars[i]), SCIPvarGetUbLocal(consdata->vars[i])) );
      if( consdata->offsets != NULL && consdata->offsets[i] != 0.0 )
         SCIPintervalAddScalar(SCIPinfinity(scip), &lhsranges[i], lhsranges[i], consdata->offsets[i]);
      if( consdata->coefs   != NULL && consdata->coefs[i]   != 1.0 )
         SCIPintervalMulScalar(SCIPinfinity(scip), &lhsranges[i], lhsranges[i], consdata->coefs[i]);
      SCIPintervalSquare(SCIPinfinity(scip), &lhsranges[i], lhsranges[i]);
      
      SCIPintervalAdd(SCIPinfinity(scip), &lhsrange, lhsrange, lhsranges[i]);
   }

   SCIPintervalSquareRoot(SCIPinfinity(scip), &a, lhsrange);
   if( consdata->rhscoeff != 1.0 )
      SCIPintervalDivScalar(SCIPinfinity(scip), &a, a, consdata->rhscoeff);
   if( consdata->rhsoffset )
      SCIPintervalSubScalar(SCIPinfinity(scip), &a, a, consdata->rhsoffset);
   SCIP_CALL( SCIPtightenVarLb(scip, consdata->rhsvar, SCIPintervalGetInf(a), FALSE, &infeas, &tightened) );
   if( infeas )
      *result = SCIP_CUTOFF;
   else if( tightened )
   {
      *result = SCIP_REDUCEDDOM;
      ++*nchgbds;
   }

   if( *result != SCIP_CUTOFF )
   {
      SCIPintervalSetBounds(&rhsrange,
         MIN(SCIPvarGetLbLocal(consdata->rhsvar), SCIPvarGetUbLocal(consdata->rhsvar)),
         MAX(SCIPvarGetLbLocal(consdata->rhsvar), SCIPvarGetUbLocal(consdata->rhsvar)) );
      if( consdata->rhsoffset != 0.0 )
         SCIPintervalAddScalar(SCIPinfinity(scip), &rhsrange, rhsrange, consdata->rhsoffset);
      if( consdata->rhscoeff  != 1.0 )
         SCIPintervalMulScalar(SCIPinfinity(scip), &rhsrange, rhsrange, consdata->rhscoeff);
      SCIPintervalSquare(SCIPinfinity(scip), &rhsrange, rhsrange);

      SCIPintervalSub(SCIPinfinity(scip), &b, rhsrange, lhsrange);
      for( i = 0; i < consdata->nvars; ++i )
      {
         SCIPintervalUndoSub(SCIPinfinity(scip), &a, b, lhsranges[i]);
         SCIPintervalSquareRoot(SCIPinfinity(scip), &a, a);
         
         c = a;
         if( consdata->coefs   != NULL && consdata->coefs[i]   != 1.0 )
            SCIPintervalDivScalar(SCIPinfinity(scip), &c, c, consdata->coefs[i]);
         if( consdata->offsets != NULL && consdata->offsets[i] != 0.0 )
            SCIPintervalSubScalar(SCIPinfinity(scip), &c, c, consdata->offsets[i]);
         
         SCIP_CALL( SCIPtightenVarUb(scip, consdata->vars[i], SCIPintervalGetSup(c), FALSE, &infeas, &tightened) );
         if( infeas )
         {
            *result = SCIP_CUTOFF;
            break;
         }
         else if( tightened )
         {
            *result = SCIP_REDUCEDDOM;
            ++*nchgbds;
         }
         
         c = a;
         if( consdata->coefs   != NULL && consdata->coefs[i]   != -1.0 )
            SCIPintervalDivScalar(SCIPinfinity(scip), &c, c, -consdata->coefs[i]);
         if( consdata->offsets != NULL && consdata->offsets[i] !=  0.0 )
            SCIPintervalSubScalar(SCIPinfinity(scip), &c, c, consdata->offsets[i]);
         
         SCIP_CALL( SCIPtightenVarLb(scip, consdata->vars[i], SCIPintervalGetInf(c), FALSE, &infeas, &tightened) );
         if( infeas )
         {
            *result = SCIP_CUTOFF;
            break;
         }
         else if( tightened )
         {
            *result = SCIP_REDUCEDDOM;
            ++*nchgbds;
         }
      }
   }

   SCIPfreeBufferArray(scip, &lhsranges);
   
   if( *result != SCIP_DIDNOTFIND )
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
   
   return SCIP_OKAY;
}

/** chooses a branching variable from the variables on the right hand side of violated constraints and branches on it */
static
SCIP_RETCODE branchOnRhsVariable(
   SCIP*          scip,       /**< SCIP data structure */
   SCIP_CONS**    conss,      /**< constraints */
   int            nconss,     /**< number of constraints */
   SCIP_Bool*     success     /**< buffer to store whether we found a branching variable and did a branching */ 
   )
{
   SCIP_CONSDATA*   consdata;
   SCIP_VAR*        brvar = NULL;
   SCIP_Real        diam, brvardiam;
   SCIP_Real        leftub=0.0, rightlb=0.0;
   SCIP_Real        leftobjest, rightobjest;
   SCIP_Real        lb, ub;
   int              c;
   
   assert(scip    != NULL);
   assert(conss   != NULL || nconss == 0);
   assert(success != NULL);
   
   *success = FALSE;
   
   brvardiam = 3 * SCIPfeastol(scip); /* do not branch on variables with diameter less than this value */
   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      if( SCIPisFeasPositive(scip, consdata->violation) )
      {
         if( SCIPisInfinity(scip, SCIPvarGetUbLocal(consdata->rhsvar)) )
         {
            brvardiam = SCIPinfinity(scip);
            brvar = consdata->rhsvar;
            break;
         }
         
         diam = SCIPvarGetUbLocal(consdata->rhsvar) - SCIPvarGetLbLocal(consdata->rhsvar);
         if( diam > brvardiam )
         {
            brvardiam = diam;
            brvar = consdata->rhsvar;
         }
      }
   }
   
   if( brvar == NULL )
      return SCIP_OKAY;
   
   lb = SCIPvarGetLbLocal(brvar);
   ub = SCIPvarGetUbLocal(brvar);

   leftub = SCIPgetVarSol(scip, brvar);
   assert(!SCIPisNegative(scip, leftub));
   if( SCIPisInfinity(scip, leftub) )
      leftub = SCIPvarGetLbLocal(brvar) + 1000;
   else
   {
      if( leftub < 0.95 * lb + 0.05 * ub )
         leftub = 0.95 * lb + 0.05 * ub;
      else if( leftub > 0.05 * lb + 0.95 * ub )
         leftub = 0.05 * lb + 0.95 * ub;
      
      /* for very tiny intervals we set it into the middle */
      if( !SCIPisGT(scip, leftub, lb) || !SCIPisLT(scip, leftub, ub) )
         leftub = (lb + ub) * 0.5;
   }
   
   if( SCIPvarGetType(brvar) == SCIP_VARTYPE_CONTINUOUS )
   {
      rightlb     = leftub;
      leftobjest  = SCIPcalcChildEstimate(scip, brvar, leftub);
      rightobjest = leftobjest;
   }
   else
   {
      leftub  = SCIPceil(scip, leftub + 0.001);
      rightlb = leftub - 1.0;
      assert(rightlb >= 0.0);
      leftobjest  = SCIPcalcChildEstimate(scip, brvar,  leftub);
      rightobjest = SCIPcalcChildEstimate(scip, brvar, rightlb);
   }

   if( leftobjest > SCIPinfinity(scip) )
      leftobjest = SCIPinfinity(scip) / 5.0;
   if( rightobjest > SCIPinfinity(scip) )
      rightobjest = leftobjest;

   if( SCIPvarGetStatus(brvar) == SCIP_VARSTATUS_MULTAGGR )
   {
      SCIP_NODE* node;
      SCIP_CONS* cons;
      SCIP_Real  val = 1.0;
      SCIPdebugMessage("branching on multiaggregated variable %s: new intervals: [%g, %g] [%g, %g]\n", SCIPvarGetName(brvar), lb, leftub, rightlb, ub);

      SCIP_CALL( SCIPcreateChild(scip, &node, 0.0, leftobjest) );
      SCIP_CALL( SCIPcreateConsLinear(scip, &cons, "branch", 1, &brvar, &val, lb, leftub, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE) );
      SCIP_CALL( SCIPaddConsNode(scip, node, cons, NULL) );
      SCIP_CALL( SCIPreleaseCons(scip, &cons) );

      SCIP_CALL( SCIPcreateChild(scip, &node, 0.0, rightobjest) );
      SCIP_CALL( SCIPcreateConsLinear(scip, &cons, "branch", 1, &brvar, &val, rightlb, ub, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE) );
      SCIP_CALL( SCIPaddConsNode(scip, node, cons, NULL) );
      SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   }
   else
   {
      if( SCIPvarGetType(brvar) != SCIP_VARTYPE_CONTINUOUS )
      {
         SCIPdebugMessage("branching on discrete variable %s\n", SCIPvarGetName(brvar));
         SCIP_CALL( SCIPbranchVar(scip, brvar, NULL, NULL, NULL) );
      }
      else
      {
         SCIP_NODE* node;
         SCIPdebugMessage("branching on continuous variable %s: new intervals: [%g, %g] [%g, %g]\n", SCIPvarGetName(brvar), lb, leftub, rightlb, ub);

         SCIP_CALL( SCIPcreateChild(scip, &node, 0.0, leftobjest) );
         SCIP_CALL( SCIPchgVarUbNode(scip, node, brvar, leftub) );

         SCIP_CALL( SCIPcreateChild(scip, &node, 0.0, rightobjest) );
         SCIP_CALL( SCIPchgVarLbNode(scip, node, brvar, rightlb) );
      }
   }

   *success = TRUE;

   return SCIP_OKAY;
}

/*
 * Callback methods of constraint handler
 */


/** NLPI initialization method of constraint handler
 * 
 * The constraint handler should create an NLPI representation of the constraints in the provided NLPI.
 */
SCIP_RETCODE SCIPconsInitNlpiSOC(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler for SOC constraints */
   SCIP_NLPI*            nlpi,               /**< NLPI where to add constraints */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< SOC constraints */
   SCIP_HASHMAP*         var_scip2nlp        /**< mapping from SCIP variables to variable indices in NLPI */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real*     lhs;
   SCIP_Real*     rhs;
   int*           linoffset = NULL;   /* row offsets */
   int*           linindex  = NULL;    /* column indices */
   SCIP_Real*     lincoeff  = NULL;    /* coefficients of linear variables */
   int            linnnz;
   int*           nquadrows;
   int**          quadrowidx;
   int**          quadoffset;
   int**          quadindex;
   SCIP_Real**    quadcoeff;
   int            quadnnz;
   int            i, j;
   int            lincnt;      /* how many linear cofficients written so far */
      
   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(nlpi     != NULL);
   assert(conss    != NULL || nconss == 0);

   if( nconss == 0 )
      return SCIP_OKAY;
   
   /* @todo try different nlp-formulations of a soc constraint (sqrt not good, but convex would be nice) */

   /* linear part exists if there are offsets */
   linnnz = 0;
   for( i = 0; i < nconss; ++i )
   {
      consdata = SCIPconsGetData(conss[i]);
      assert(consdata != NULL);
      
      if( consdata->offsets != NULL )
         for( j = 0; j < consdata->nvars; ++j )
            if( consdata->offsets[j] )
               ++linnnz;
      
      if( consdata->rhsoffset != 0.0 )
         ++linnnz;
   }

   assert(var_scip2nlp != NULL);

   SCIP_CALL( SCIPallocBufferArray(scip, &lhs, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &rhs, nconss) );

   if( linnnz != 0 )
   {
      SCIP_CALL( SCIPallocBufferArray(scip, &linoffset, nconss+1) );
      SCIP_CALL( SCIPallocBufferArray(scip, &linindex,  linnnz) );
      SCIP_CALL( SCIPallocBufferArray(scip, &lincoeff,  linnnz) );
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &nquadrows,  nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadrowidx, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadoffset, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadindex,  nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadcoeff,  nconss) );

   lincnt = 0;
   for( i = 0; i < nconss; ++i )
   {
      consdata = SCIPconsGetData(conss[i]);
      assert(consdata != NULL);

      lhs[i] = -SCIPinfinity(scip);
      rhs[i] = -consdata->constant;
      
      if( linoffset != NULL )
         linoffset[i] = lincnt;

      quadnnz = consdata->nvars + 1;

      nquadrows[i] = consdata->nvars + 1;
      SCIPallocBufferArray(scip, &quadrowidx[i], consdata->nvars + 1);
      SCIPallocBufferArray(scip, &quadoffset[i], consdata->nvars + 2);
      SCIPallocBufferArray(scip, &quadindex[i],  consdata->nvars + 1);
      SCIPallocBufferArray(scip, &quadcoeff[i],  consdata->nvars + 1);
      
      for( j = 0; j < consdata->nvars; ++j )
      {
         quadrowidx[i][j] = (int) (size_t) SCIPhashmapGetImage(var_scip2nlp, consdata->vars[j]);
         quadoffset[i][j] = j;
         quadindex [i][j] = j;
         quadcoeff [i][j] = consdata->coefs ? (consdata->coefs[j] * consdata->coefs[j]) : 1.0;
         
         if( consdata->offsets && consdata->offsets[j] )
         {
            linindex[lincnt] = quadrowidx[i][j];
            lincoeff[lincnt] = 2 * quadcoeff[i][j] * consdata->offsets[j];
            ++lincnt;
            
            rhs[i] -= quadcoeff[i][j] * consdata->offsets[j] * consdata->offsets[j];
         }
      }
      quadrowidx[i][consdata->nvars] = (int) (size_t) SCIPhashmapGetImage(var_scip2nlp, consdata->rhsvar);
      quadoffset[i][consdata->nvars] = consdata->nvars;
      quadindex [i][consdata->nvars] = consdata->nvars;
      quadcoeff [i][consdata->nvars] = - consdata->rhscoeff * consdata->rhscoeff;
      
      if( consdata->rhsoffset )
      {
         linindex[lincnt] = quadrowidx[i][consdata->nvars];
         lincoeff[lincnt] = - 2 * consdata->rhscoeff * consdata->rhscoeff * consdata->rhsoffset;
         ++lincnt;
         
         rhs[i] += consdata->rhscoeff * consdata->rhscoeff * consdata->rhsoffset * consdata->rhsoffset;
      }
      
      quadoffset[i][consdata->nvars + 1] = consdata->nvars + 1;
   }
   if( linoffset )
   {
      linoffset[nconss] = lincnt;
      assert(lincnt == linnnz);
   }

   SCIP_CALL( SCIPnlpiAddConstraints(scip, nlpi, nconss,
      lhs, rhs,
      linoffset, linindex, lincoeff,
      nquadrows, quadrowidx, quadoffset, quadindex, quadcoeff,
      NULL, NULL, NULL) );

   for( i = 0; i < nconss; ++i )
   {
      SCIPfreeBufferArrayNull(scip, &quadrowidx[i]);
      SCIPfreeBufferArrayNull(scip, &quadoffset[i]);
      SCIPfreeBufferArrayNull(scip, &quadindex[i]);
      SCIPfreeBufferArrayNull(scip, &quadcoeff[i]);
   }

   SCIPfreeBufferArray(scip, &lhs);
   SCIPfreeBufferArray(scip, &rhs);

   if( linoffset != NULL )
   {
      SCIPfreeBufferArray(scip, &linoffset);
      SCIPfreeBufferArray(scip, &linindex);
      SCIPfreeBufferArray(scip, &lincoeff);
   }

   SCIPfreeBufferArray(scip, &nquadrows);
   SCIPfreeBufferArray(scip, &quadrowidx);
   SCIPfreeBufferArray(scip, &quadoffset);
   SCIPfreeBufferArray(scip, &quadindex);
   SCIPfreeBufferArray(scip, &quadcoeff);

   return SCIP_OKAY;
}

/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
#if 1
static
SCIP_DECL_CONSFREE(consFreeSOC)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   SCIPfreeMemory(scip, &conshdlrdata);

   return SCIP_OKAY;
}
#else
#define consFreeSOC NULL
#endif


/** initialization method of constraint handler (called after problem was transformed) */
#if 0
static
SCIP_DECL_CONSINIT(consInitSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitSOC NULL
#endif


/** deinitialization method of constraint handler (called before transformed problem is freed) */
#if 0
static
SCIP_DECL_CONSEXIT(consExitSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitSOC NULL
#endif


/** presolving initialization method of constraint handler (called when presolving is about to begin) */
#if 0
static
SCIP_DECL_CONSINITPRE(consInitpreSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitpreSOC NULL
#endif


/** presolving deinitialization method of constraint handler (called after presolving has been finished) */
#if 0
static
SCIP_DECL_CONSEXITPRE(consExitpreSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitpreSOC NULL
#endif


/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
#if 1
static
SCIP_DECL_CONSINITSOL(consInitsolSOC)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;
   int c;

   assert(scip != NULL);
   assert(conshdlr != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   assert(conshdlrdata->eventhdlr);

   /* @todo catch events only if propfreq > 0 */ 
   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      SCIP_CALL( catchVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) );
   }
   
   conshdlrdata->nlpheur        = SCIPfindHeur(scip, "nlp");
   conshdlrdata->nextbranchnode = conshdlrdata->branchfreq;
   
   return SCIP_OKAY;
}
#else
#define consInitsolSOC NULL
#endif


/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
#if 1
static
SCIP_DECL_CONSEXITSOL(consExitsolSOC)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   int c;

   assert(scip     != NULL);
   assert(conshdlr != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   assert(conshdlrdata->eventhdlr);

   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      SCIP_CALL( dropVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) );
   }
   
   conshdlrdata->nlpheur = NULL;
   
   return SCIP_OKAY;
}
#else
#define consExitsolSOC NULL
#endif


/** frees specific constraint data */
#if 1
static
SCIP_DECL_CONSDELETE(consDeleteSOC)
{
   int i;

   assert(scip      != NULL);
   assert(conshdlr  != NULL);
   assert(cons      != NULL);
   assert(consdata  != NULL);
   assert(*consdata != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   SCIPdebugMessage("Deleting SOC constraint <%s>.\n", SCIPconsGetName(cons) );
   
   for( i = 0; i < (*consdata)->nvars; ++i )
   {
      SCIP_CALL( SCIPreleaseVar(scip, &(*consdata)->vars[i]) );
   }
   
   SCIPfreeMemoryArray(scip, &(*consdata)->vars);
   SCIPfreeMemoryArrayNull(scip, &(*consdata)->coefs);
   SCIPfreeMemoryArrayNull(scip, &(*consdata)->offsets);
   
   SCIPfreeMemoryArrayNull(scip, &(*consdata)->lhsbndchgeventdatas);

   SCIPfreeMemory(scip, consdata);

   return SCIP_OKAY;
}
#else
#define consDeleteSOC NULL
#endif


/** transforms constraint data into data belonging to the transformed problem */ 
#if 1
static
SCIP_DECL_CONSTRANS(consTransSOC)
{  
   SCIP_CONSDATA*     consdata;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     sourcedata;
   char               s[SCIP_MAXSTRLEN];
   int                i;

   assert(scip       != NULL);
   assert(conshdlr   != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(sourcecons != NULL);
   assert(targetcons != NULL);

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   SCIPdebugMessage("Transforming SOC constraint: <%s>.\n", SCIPconsGetName(sourcecons) );

   /* get data of original constraint */
   sourcedata = SCIPconsGetData(sourcecons);
   assert(sourcedata       != NULL);
   assert(sourcedata->vars != NULL);
   
   /* create constraint data */
   SCIP_CALL( SCIPallocMemory(scip, &consdata) );

   consdata->nvars = sourcedata->nvars;
   SCIP_CALL( SCIPallocMemoryArray(scip, &consdata->vars, consdata->nvars) );
   SCIP_CALL( SCIPgetTransformedVars(scip, consdata->nvars, sourcedata->vars, consdata->vars) );
   for( i = 0; i < consdata->nvars; ++i )
   {
      SCIP_CALL( SCIPcaptureVar(scip, consdata->vars[i]) );
   }

   if( sourcedata->coefs != NULL )
   {
      SCIP_CALL( SCIPduplicateMemoryArray(scip, &consdata->coefs, sourcedata->coefs, consdata->nvars) );
   }
   else
      consdata->coefs = NULL;


   if( sourcedata->offsets != NULL )
   {
      SCIP_CALL( SCIPduplicateMemoryArray(scip, &consdata->offsets, sourcedata->offsets, consdata->nvars) );
   }
   else
      consdata->offsets = NULL;
   
   consdata->constant = sourcedata->constant;
   
   SCIP_CALL( SCIPgetTransformedVar(scip, sourcedata->rhsvar, &consdata->rhsvar) );
   consdata->rhsoffset = sourcedata->rhsoffset;
   consdata->rhscoeff  = sourcedata->rhscoeff;
   
   consdata->lhsbndchgeventdatas = NULL;
   consdata->ispropagated = FALSE;
   consdata->isapproxadded = FALSE;

   /* create transformed constraint with the same flags */
   (void) SCIPsnprintf(s, SCIP_MAXSTRLEN, "t_%s", SCIPconsGetName(sourcecons));
   SCIP_CALL( SCIPcreateCons(scip, targetcons, s, conshdlr, consdata,
    SCIPconsIsInitial(sourcecons), SCIPconsIsSeparated(sourcecons),
    SCIPconsIsEnforced(sourcecons), SCIPconsIsChecked(sourcecons),
    SCIPconsIsPropagated(sourcecons), SCIPconsIsLocal(sourcecons),
    SCIPconsIsModifiable(sourcecons), SCIPconsIsDynamic(sourcecons),
    SCIPconsIsRemovable(sourcecons), SCIPconsIsStickingAtNode(sourcecons)) );

   return SCIP_OKAY;
}
#else
#define consTransSOC NULL
#endif


/** LP initialization method of constraint handler */
#if 0
static
SCIP_DECL_CONSINITLP(consInitlpSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitlpSOC NULL
#endif


/** separation method of constraint handler for LP solutions */
#if 1
static
SCIP_DECL_CONSSEPALP(consSepalpSOC)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONS*         maxviolcon;
   SCIP_Bool          sepasuccess;

   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(conss    != NULL || nconss == 0);
   assert(result   != NULL);

   *result = SCIP_DIDNOTFIND;
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   SCIP_CALL( computeViolations(scip, conss, nconss, NULL, conshdlrdata->doscaling, &maxviolcon) );
   if( maxviolcon == NULL )
      return SCIP_OKAY;

   SCIP_CALL( separatePoint(scip, conss, nconss, nusefulconss, NULL, FALSE, conshdlrdata->projectpoint, &sepasuccess) );
   if( sepasuccess )
      *result = SCIP_SEPARATED;

   return SCIP_OKAY;
}
#else
#define consSepalpSOC NULL
#endif


/** separation method of constraint handler for arbitrary primal solutions */
#if 1
static
SCIP_DECL_CONSSEPASOL(consSepasolSOC)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONS*         maxviolcon;
   SCIP_Bool          sepasuccess;
   
   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(conss    != NULL || nconss == 0);
   assert(result   != NULL);
   assert(sol      != NULL);

   *result = SCIP_DIDNOTFIND;
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   SCIP_CALL( computeViolations(scip, conss, nconss, sol, conshdlrdata->doscaling, &maxviolcon) );
   if( maxviolcon == NULL )
      return SCIP_OKAY;

   SCIP_CALL( separatePoint(scip, conss, nconss, nusefulconss, sol, TRUE, conshdlrdata->projectpoint, &sepasuccess) );
   if( sepasuccess )
      *result = SCIP_SEPARATED;

   return SCIP_OKAY;
}
#else
#define consSepasolSOC NULL
#endif


/** constraint enforcing method of constraint handler for LP solutions */
static
SCIP_DECL_CONSENFOLP(consEnfolpSOC)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   SCIP_CONS*         maxviolcons;
   SCIP_Bool          success;
   SCIP_Bool          allow_weak_cuts;
   int                nbndchg;
   int                c;
   
   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(conss    != NULL || nconss == 0);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result   != NULL);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   SCIP_CALL( computeViolations(scip, conss, nconss, NULL, conshdlrdata->doscaling, &maxviolcons) );
   
   if( maxviolcons == NULL )
   {
      *result = SCIP_FEASIBLE;
      return SCIP_OKAY;
   }

   /* try separation */
   allow_weak_cuts = !conshdlrdata->branchfreq || SCIPnodeGetNumber(SCIPgetCurrentNode(scip)) < conshdlrdata->nextbranchnode;
   if( !allow_weak_cuts )
      conshdlrdata->nextbranchnode += conshdlrdata->branchfreq;
   SCIP_CALL( separatePoint(scip, conss, nconss, nusefulconss, NULL, allow_weak_cuts, conshdlrdata->projectpoint, &success) );
   if( success )
   {
      SCIPdebugMessage("enforced by separation\n");
      *result = SCIP_SEPARATED;
      return SCIP_OKAY;
   }
   
   /* try propagation */
   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);
      if( !SCIPisFeasPositive(scip, consdata->violation) )
         continue;
      
      nbndchg = 0;
      SCIP_CALL( propagateBounds(scip, conshdlr, conss[c], result, &nbndchg) );
      if( *result == SCIP_CUTOFF || *result == SCIP_REDUCEDDOM )
      {
         SCIPdebugMessage("enforced by %s\n", *result == SCIP_CUTOFF ? "cutting off node" : "reducing domain");
         return SCIP_OKAY;
      }
   }
      
   /* branch on variable on right hand side */
   SCIP_CALL( branchOnRhsVariable(scip, &maxviolcons, 1, &success) );
   if( !success ) /* if branching on maximal violated constraint was not possible, consider all (violated) constraints */
   {
      SCIP_CALL( branchOnRhsVariable(scip, conss, nconss, &success) );
   }
   if( success )
   {
      SCIPdebugMessage("branched on right hand side variable\n");
      *result = SCIP_BRANCHED;
      return SCIP_OKAY;
   }
   else if( !allow_weak_cuts )
   { /* try again separation, now also allow weaker cuts */
      SCIP_CALL( separatePoint(scip, conss, nconss, nusefulconss, NULL, TRUE, conshdlrdata->projectpoint, &success) );
      if( success )
      {
         SCIPdebugMessage("enforced by separation via weak cut\n");
         *result = SCIP_SEPARATED;
         return SCIP_OKAY;
      }
   }

   SCIPwarningMessage("could not enforce feasibility by separating or branching; declaring solution with viol %g feasible\n", SCIPconsGetData(maxviolcons)->violation);
   *result = SCIP_FEASIBLE;

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
SCIP_DECL_CONSENFOPS(consEnfopsSOC)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONS*         maxviolcons;
   
   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(conss    != NULL || nconss == 0);
   assert(result   != NULL);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   SCIP_CALL( computeViolations(scip, conss, nconss, NULL, conshdlrdata->doscaling, &maxviolcons) );
   
   if( maxviolcons == NULL )
      *result = SCIP_FEASIBLE;
   
   *result = SCIP_INFEASIBLE;

   return SCIP_OKAY;
}

/** feasibility check method of constraint handler for integral solutions */
static
SCIP_DECL_CONSCHECK(consCheckSOC)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   SCIP_Real          maxviol = 0.0;
   int                c;
   
   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(conss    != NULL || nconss == 0);
   assert(result   != NULL );
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   *result = SCIP_FEASIBLE;
   
   for( c = 0; c < nconss; ++c )
   {
      SCIP_CALL( computeViolation(scip, conss[c], sol, conshdlrdata->doscaling) );
      
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      if( SCIPisFeasPositive(scip, consdata->violation) )
      {
         *result = SCIP_INFEASIBLE;
         
         if( printreason )
         {
            SCIPinfoMessage(scip, NULL, "\nWARNING: solution not feasible w.r.t. constraint: (violation: %f)\n",  consdata->violation);
            SCIP_CALL( SCIPprintCons(scip, conss[c], NULL) );            
         }

         if( conshdlrdata->nlpheur != NULL )
         {
            if( consdata->violation > maxviol )
               maxviol = consdata->violation;
         }
         else        
            return SCIP_OKAY;
      }
   }
   
   if( conshdlrdata->nlpheur && sol != NULL && *result == SCIP_INFEASIBLE )
      SCIP_CALL( SCIPheurNlpUpdateStartpoint(scip, conshdlrdata->nlpheur, sol, maxviol) );

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
#if 1
static
SCIP_DECL_CONSPROP(consPropSOC)
{  
   SCIP_RESULT propresult;
   int         c;
   int         nchgbds = 0;

   assert(scip     != NULL);
   assert(conss    != NULL || nconss == 0);
   assert(result   != NULL);

   *result = SCIP_DIDNOTFIND;

   for( c = 0; c < nconss && *result != SCIP_CUTOFF; ++c )
   {
      SCIP_CALL( propagateBounds(scip, conshdlr, conss[c], &propresult, &nchgbds) );
      if (propresult != SCIP_DIDNOTFIND && propresult != SCIP_DIDNOTRUN)
         *result = propresult;
   }

   return SCIP_OKAY;
}
#else
#define consPropSOC NULL
#endif


/** presolving method of constraint handler */
#if 1
static
SCIP_DECL_CONSPRESOL(consPresolSOC)
{
   SCIP_CONSHDLRDATA*  conshdlrdata;
   SCIP_CONSDATA*      consdata;
   int                 c;
   SCIP_RESULT         propresult;
   
   assert(scip     != NULL);
   assert(conss    != NULL || nconss == 0);
   assert(conshdlr != NULL);
   assert(result   != NULL);
   
   *result = SCIP_DIDNOTFIND;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      if( conshdlrdata->nauxvars > 0 && !consdata->isapproxadded )
      {
         SCIP_CALL( presolveCreateOuterApprox(scip, consdata->nvars, consdata->vars, consdata->coefs, consdata->offsets, consdata->rhsvar, consdata->rhscoeff, consdata->rhscoeff, consdata->constant, SCIPconsGetName(conss[c]), conss[c], conshdlrdata->nauxvars, conshdlrdata->glineur) );
         consdata->isapproxadded = TRUE;
      }

      /* @todo remove fixed variables, replace aggregated and negated vars (if on lhs only?) */

      /* @todo use varevents to recognize whether propagation might make sense */
      if( nnewfixedvars || nnewchgbds )
         consdata->ispropagated = FALSE;
      
      SCIP_CALL( propagateBounds(scip, conshdlr, conss[c], &propresult, nchgbds) );
      switch( propresult )
      {
         case SCIP_DIDNOTRUN:
         case SCIP_DIDNOTFIND:
            break;
         case SCIP_REDUCEDDOM:
            *result = SCIP_SUCCESS;
            break;
         case SCIP_CUTOFF:
            *result = SCIP_CUTOFF;
            SCIPdebugMessage("infeasible in presolve due to propagation for constraint %s\n", SCIPconsGetName(conss[c]));
            break;
         default:
            SCIPerrorMessage("unexpected result from propagation: %d\n", propresult);
            return SCIP_ERROR;
      }
   }

   return SCIP_OKAY;
}
#else
#define consPresolSOC NULL
#endif


/** propagation conflict resolving method of constraint handler */
#if 0
static
SCIP_DECL_CONSRESPROP(consRespropSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consRespropSOC NULL
#endif


/** variable rounding lock method of constraint handler */
static
SCIP_DECL_CONSLOCK(consLockSOC)
{
   SCIP_CONSDATA* consdata;
   int            i;

   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(cons     != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   SCIPdebugMessage("Locking constraint <%s>.\n", SCIPconsGetName(cons));

   /* Changing variables x_i, i <= n, in both directions can lead to an infeasible solution. */
   for( i = 0; i < consdata->nvars; ++i )
   {
      SCIP_CALL( SCIPaddVarLocks(scip, consdata->vars[i], nlockspos + nlocksneg, nlockspos + nlocksneg) );
   }

   /* Rounding x_{n+1} up will not violate a solution. */
   SCIP_CALL( SCIPaddVarLocks(scip, consdata->rhsvar, nlockspos, nlocksneg) );

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSACTIVE(consActiveSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consActiveSOC NULL
#endif


/** constraint deactivation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDEACTIVE(consDeactiveSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeactiveSOC NULL
#endif


/** constraint enabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSENABLE(consEnableSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consEnableSOC NULL
#endif


/** constraint disabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDISABLE(consDisableSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDisableSOC NULL
#endif


/** constraint display method of constraint handler */
#if 1
static
SCIP_DECL_CONSPRINT(consPrintSOC)
{  
   SCIP_CONSDATA* consdata;
   int            i;

   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(cons     != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   SCIPinfoMessage(scip, file, " sqrt( ");
   if( consdata->constant != 0.0 )
   {
      SCIPinfoMessage(scip, file, " %g", consdata->constant);
   }
   
   for( i = 0; i < consdata->nvars; ++i )
   {
      if( consdata->coefs != NULL && consdata->coefs[i] != 1.0 )
      { /* nondefault coefficient */
         if( consdata->offsets && consdata->offsets[i] )
         { /* nondefault offset */
            SCIPinfoMessage(scip, file, "+ (%g * (<%s> + %g) )^2 ", consdata->coefs[i], SCIPvarGetName(consdata->vars[i]), consdata->offsets[i]);
         }
         else
         { /* offset 0.0 */
            SCIPinfoMessage(scip, file, "+ (%g * <%s>)^2 ", consdata->coefs[i], SCIPvarGetName(consdata->vars[i]));
         }
      }
      else
      { /* coefficient 1.0 */
         if( consdata->offsets != NULL && consdata->offsets[i] )
         { /* nondefault offset */
            SCIPinfoMessage(scip, file, "+ (<%s> + %g)^2 ", SCIPvarGetName(consdata->vars[i]), consdata->offsets[i]);
         }
         else
         { /* offset 0.0 */
            SCIPinfoMessage(scip, file, "+ <%s>^2 ", SCIPvarGetName(consdata->vars[i]));
         }
      }
   }
   
   SCIPinfoMessage(scip, file, ") <= ");
   if( consdata->rhscoeff != 1.0 )
      if( consdata->rhsoffset != 0.0 )
      {
         SCIPinfoMessage(scip, file, "%g * (<%s> + %g)\n", consdata->rhscoeff, SCIPvarGetName(consdata->rhsvar), consdata->rhsoffset);
      }
      else
      {
         SCIPinfoMessage(scip, file, "%g * <%s>\n", consdata->rhscoeff, SCIPvarGetName(consdata->rhsvar));
      }
   else
      if( consdata->rhsoffset != 0.0 )
      {
         SCIPinfoMessage(scip, file, "<%s> + %g\n", SCIPvarGetName(consdata->rhsvar), consdata->rhsoffset);
      }
      else
      {
         SCIPinfoMessage(scip, file, "<%s>\n", SCIPvarGetName(consdata->rhsvar));
      }

   return SCIP_OKAY;
}
#else
#define consPrintSOC NULL
#endif


/** constraint copying method of constraint handler */
#if 1
static
SCIP_DECL_CONSCOPY(consCopySOC)
{
   SCIP_CONSDATA* consdata;
   SCIP_VAR**     vars;
   SCIP_VAR*      rhsvar;
   int            i;
   
   assert(scip       != NULL);
   assert(conshdlr   != NULL);
   assert(cons       != NULL);
   assert(sourcescip != NULL);
   assert(sourcecons != NULL);
   assert(varmap     != NULL);
   assert(success    != NULL);

   consdata = SCIPconsGetData(sourcecons);
   assert(consdata != NULL);
   
   rhsvar = (SCIP_VAR*) SCIPhashmapGetImage(varmap, consdata->rhsvar);
   if( rhsvar == NULL )
   {
      *success = FALSE;
      return SCIP_OKAY;
   }

   *success = TRUE; /* think positive */

   SCIP_CALL( SCIPallocBufferArray(sourcescip, &vars, consdata->nvars) );
   
   for( i = 0; i < consdata->nvars; ++i )
   {
      vars[i] = (SCIP_VAR*) SCIPhashmapGetImage(varmap, consdata->vars[i]);
      if( vars[i] == NULL )
      {
         *success = FALSE;
         break;
      }
   }
   
   if( *success )
   {
      SCIP_CALL( SCIPcreateConsSOC(scip, cons, name ? name : SCIPconsGetName(sourcecons),
         consdata->nvars, vars, consdata->coefs, consdata->offsets, consdata->constant,
         rhsvar, consdata->rhscoeff, consdata->rhsoffset,
         initial, separate, enforce, check, propagate, local, modifiable, dynamic, removable) );
      assert(stickingatnode == FALSE);
   }
   else
      *cons = NULL;
      
   SCIPfreeBufferArray(sourcescip, &vars);

   return SCIP_OKAY;
}
#else
#define consCopySOC NULL
#endif


/** constraint parsing method of constraint handler */
#if 0
static
SCIP_DECL_CONSPARSE(consParseSOC)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of soc constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consParseSOC NULL
#endif

/*
 * constraint specific interface methods
 */

/** creates the handler for second order cone constraints and includes it in SCIP */
SCIP_RETCODE SCIPincludeConshdlrSOC(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   /* create constraint handler data */
   SCIP_CALL( SCIPallocMemory(scip, &conshdlrdata) );
   conshdlrdata->nlpheur   = NULL;
   
   SCIP_CALL( SCIPincludeEventhdlr(scip, CONSHDLR_NAME, "signals a bound change to a second order cone constraint",
      NULL, NULL, NULL, NULL, NULL, NULL, processVarEvent, NULL) );
   conshdlrdata->eventhdlr = SCIPfindEventhdlr(scip, CONSHDLR_NAME);

   /* include constraint handler */
   SCIP_CALL( SCIPincludeConshdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
         CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_EAGERFREQ, CONSHDLR_MAXPREROUNDS, 
         CONSHDLR_DELAYSEPA, CONSHDLR_DELAYPROP, CONSHDLR_DELAYPRESOL, CONSHDLR_NEEDSCONS,
         consFreeSOC, consInitSOC, consExitSOC, 
         consInitpreSOC, consExitpreSOC, consInitsolSOC, consExitsolSOC,
         consDeleteSOC, consTransSOC, consInitlpSOC,
         consSepalpSOC, consSepasolSOC, consEnfolpSOC, consEnfopsSOC, consCheckSOC, 
         consPropSOC, consPresolSOC, consRespropSOC, consLockSOC,
         consActiveSOC, consDeactiveSOC, 
         consEnableSOC, consDisableSOC,
         consPrintSOC, consCopySOC, consParseSOC,
         conshdlrdata) );

   /* add soc constraint handler parameters */
   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/scaling",      "whether a constraint should be scaled w.r.t. the current gradient norm when checking for feasibility",          &conshdlrdata->doscaling,    FALSE, TRUE,          NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/projectpoint", "whether the reference point of a cut should be projected onto the feasible set of the SOC constraint",          &conshdlrdata->projectpoint, FALSE, FALSE,         NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam (scip, "constraints/"CONSHDLR_NAME"/nauxvars",     "number of auxiliary variables to use when creating a linear outer approx. of a SOC3 constraint; 0 to turn off", &conshdlrdata->nauxvars,     FALSE, 2, 0, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam (scip, "constraints/"CONSHDLR_NAME"/branchfreq",   "frequency of branching on a node if only weak cuts could be added in enforcement; 0 to turn off",               &conshdlrdata->branchfreq,   TRUE,  0, 0, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/glineur",      "whether the Glineur Outer Approximation should be used instead of Ben-Tal Nemirovski",                          &conshdlrdata->glineur,      FALSE, TRUE,          NULL, NULL) );

#ifdef QUADCONSUPGD_PRIORITY
   /* notify function that upgrades quadratic constraint to SOC's */
   SCIP_CALL( SCIPincludeQuadconsUpgrade(scip, upgradeConsQuadratic, QUADCONSUPGD_PRIORITY, CONSHDLR_NAME) );
#endif

   return SCIP_OKAY;
}



/** creates and captures a second order cone constraint */
SCIP_RETCODE SCIPcreateConsSOC(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   int                   nvars,              /**< number of variables on left hand side of constraint (n) */
   SCIP_VAR**            vars,               /**< array with variables on left hand side (x_i) */
   SCIP_Real*            coefs,              /**< array with coefficients of left hand side variables (alpha_i), or NULL if all 1.0 */
   SCIP_Real*            offsets,            /**< array with offsets of variables (beta_i), or NULL if all 0.0 */
   SCIP_Real             constant,           /**< constant on left hand side (gamma) */
   SCIP_VAR*             rhsvar,             /**< variable on right hand side of constraint (x_{n+1}) */
   SCIP_Real             rhscoeff,           /**< coefficient of variable on right hand side (alpha_{n+1}) */
   SCIP_Real             rhsoffset,          /**< offset of variable on right hand side (beta_{n+1}) */
   SCIP_Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP?
                                              *   Usually set to TRUE. Set to FALSE for 'lazy constraints'. */
   SCIP_Bool             separate,           /**< should the constraint be separated during LP processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             enforce,            /**< should the constraint be enforced during node processing?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             check,              /**< should the constraint be checked for feasibility?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             propagate,          /**< should the constraint be propagated during node processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             local,              /**< is constraint only valid locally?
                                              *   Usually set to FALSE. Has to be set to TRUE, e.g., for branching constraints. */
   SCIP_Bool             modifiable,         /**< is constraint modifiable (subject to column generation)?
                                              *   Usually set to FALSE. In column generation applications, set to TRUE if pricing
                                              *   adds coefficients to this constraint. */
   SCIP_Bool             dynamic,            /**< is constraint subject to aging?
                                              *   Usually set to FALSE. Set to TRUE for own cuts which 
                                              *   are seperated as constraints. */
   SCIP_Bool             removable           /**< should the relaxation be removed from the LP due to aging or cleanup?
                                              *   Usually set to FALSE. Set to TRUE for 'lazy constraints' and 'user cuts'. */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSDATA* consdata;
   int            i;
   
   assert(scip != NULL);
   assert(cons != NULL);

   /* find the soc constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      SCIPerrorMessage("soc constraint handler not found\n");
      return SCIP_PLUGINNOTFOUND;
   }
   
   assert(vars     != NULL);
   assert(nvars    >= 2);
   assert(rhsvar   != NULL);
   assert(constant >= 0.0);
   assert(!SCIPisInfinity(scip, ABS(rhsoffset)));
   assert(!SCIPisInfinity(scip, constant));
   assert(rhscoeff >= 0.0);
   assert(!local || SCIPisGE(scip, SCIPvarGetLbLocal(rhsvar), -rhsoffset));
   assert( local || SCIPisGE(scip, SCIPvarGetUbLocal(rhsvar), -rhsoffset));

   /* create constraint data */
   SCIP_CALL( SCIPallocMemory(scip, &consdata) );
   
   consdata->nvars = nvars;
   SCIP_CALL( SCIPduplicateMemoryArray(scip, &consdata->vars, vars, nvars) );
   for( i = 0; i < nvars; ++i )
   {
      SCIP_CALL( SCIPcaptureVar(scip, vars[i]) );
   }
   
   if( coefs != NULL )
   {
      SCIP_CALL( SCIPduplicateMemoryArray(scip, &consdata->coefs, coefs, nvars) );
   }
   else
      consdata->coefs = NULL;
   
   if( offsets != NULL )
   {
      SCIP_CALL( SCIPduplicateMemoryArray(scip, &consdata->offsets, offsets, nvars) );
   }
   else
      consdata->offsets = NULL;
   
   consdata->constant  = constant;
   consdata->rhsvar    = rhsvar;
   consdata->rhscoeff  = rhscoeff;
   consdata->rhsoffset = rhsoffset;
   
   consdata->lhsbndchgeventdatas = NULL;
   consdata->ispropagated        = FALSE;
   consdata->isapproxadded       = FALSE;
   
   /* create constraint */
   SCIP_CALL( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
         local, modifiable, dynamic, removable, FALSE) );
   
   if( SCIPgetStage(scip) > SCIP_STAGE_INITSOLVE )
   {
      SCIP_CONSHDLRDATA* conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      
      SCIP_CALL( catchVarEvents(scip, conshdlrdata->eventhdlr, *cons) );
   }

   return SCIP_OKAY;
}
