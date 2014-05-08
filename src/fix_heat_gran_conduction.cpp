/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations

   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com

   Christoph Kloss, christoph.kloss@cfdem.com
   Copyright 2009-2012 JKU Linz
   Copyright 2012-     DCS Computing GmbH, Linz

   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   This software is distributed under the GNU General Public License.

   See the README file in the top-level directory.
------------------------------------------------------------------------- */

#include "fix_heat_gran_conduction.h"

#include "atom.h"
#include "compute_pair_gran_local.h"
#include "fix_property_atom.h"
#include "fix_property_global.h"
#include "force.h"
#include "math_extra.h"
#include "mech_param_gran.h"
#include "modify.h"
#include "neigh_list.h"
#include "pair_gran.h"

using namespace LAMMPS_NS;
using namespace FixConst;

/* ---------------------------------------------------------------------- */

FixHeatGranCond::FixHeatGranCond(class LAMMPS *lmp, int narg, char **arg) : FixHeatGran(lmp, narg, arg)
{
  iarg_ = 5;

  area_correction_flag = 0;

  bool hasargs = true;
  while(iarg_ < narg && hasargs)
  {
    hasargs = false;
    if(strcmp(arg[iarg_],"area_correction") == 0) {
      if (iarg_+2 > narg) error->fix_error(FLERR,this,"not enough arguments for keyword 'area_correction'");
      if(strcmp(arg[iarg_+1],"yes") == 0)
        area_correction_flag = 1;
      else if(strcmp(arg[iarg_+1],"no") == 0)
        area_correction_flag = 0;
      else error->fix_error(FLERR,this,"expecting 'yes' otr 'no' after 'area_correction'");
      iarg_ += 2;
      hasargs = true;
    } else if(strcmp(style,"heat/gran/conduction") == 0)
        error->fix_error(FLERR,this,"unknown keyword");
  }

  fix_conductivity = NULL;
  conductivity = NULL;

}

/* ---------------------------------------------------------------------- */

FixHeatGranCond::~FixHeatGranCond()
{

  //NP could delete fixes with no callbacks here since FixHeatGran has no callbacks

  if (conductivity)
    delete []conductivity;
}

/* ---------------------------------------------------------------------- */

void FixHeatGranCond::post_create()
{
  FixHeatGran::post_create();
}

/* ---------------------------------------------------------------------- */

void FixHeatGranCond::pre_delete(bool unfixflag)
{

  // tell cpl that this fix is deleted
  if(cpl && unfixflag) cpl->reference_deleted();

}

/* ---------------------------------------------------------------------- */

int FixHeatGranCond::setmask()
{
  int mask = FixHeatGran::setmask();
  mask |= POST_FORCE;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixHeatGranCond::init()
{
  // initialize base class
  FixHeatGran::init();

  const double *Y, *nu, *Y_orig;
  double expo, Yeff_ij, Yeff_orig_ij, ratio;
  int max_type = pair_gran->mpg->max_type();

  if (conductivity) delete []conductivity;
  conductivity = new double[max_type];
  fix_conductivity = static_cast<FixPropertyGlobal*>(modify->find_fix_property("thermalConductivity","property/global","peratomtype",max_type,0,style));

  // pre-calculate conductivity for possible contact material combinations
  for(int i=1;i< max_type+1; i++)
      for(int j=1;j<max_type+1;j++)
      {
          conductivity[i-1] = fix_conductivity->compute_vector(i-1);
          if(conductivity[i-1] < 0.) error->all(FLERR,"Fix heat/gran/conduction: Thermal conductivity must not be < 0");
      }

  // calculate heat transfer correction

  if(area_correction_flag)
  {
    if(!force->pair_match("gran",0))
        error->fix_error(FLERR,this,"area correction only works with using granular pair styles");

    expo = 1./pair_gran->stressStrainExponent();

    Y = static_cast<FixPropertyGlobal*>(modify->find_fix_property("youngsModulus","property/global","peratomtype",max_type,0,style))->get_values();
    nu = static_cast<FixPropertyGlobal*>(modify->find_fix_property("poissonsRatio","property/global","peratomtype",max_type,0,style))->get_values();
    Y_orig = static_cast<FixPropertyGlobal*>(modify->find_fix_property("youngsModulusOriginal","property/global","peratomtype",max_type,0,style))->get_values();

    // allocate a new array within youngsModulusOriginal
    static_cast<FixPropertyGlobal*>(modify->find_fix_property("youngsModulusOriginal","property/global","peratomtype",max_type,0,style))->new_array(max_type,max_type);

    // feed deltan_ratio into this array
    for(int i = 1; i < max_type+1; i++)
    {
      for(int j = 1; j < max_type+1; j++)
      {
        Yeff_ij      = 1./((1.-pow(nu[i-1],2.))/Y[i-1]     +(1.-pow(nu[j-1],2.))/Y[j-1]);
        Yeff_orig_ij = 1./((1.-pow(nu[i-1],2.))/Y_orig[i-1]+(1.-pow(nu[j-1],2.))/Y_orig[j-1]);
        ratio = pow(Yeff_ij/Yeff_orig_ij,expo);
        /*NL*/ //fprintf(screen,"ratio for type pair %d/%d is %f, Yeff_ij %f, Yeff_orig_ij %f, expo %f\n",i,j,ratio,Yeff_ij,Yeff_orig_ij,expo);
        static_cast<FixPropertyGlobal*>(modify->find_fix_property("youngsModulusOriginal","property/global","peratomtype",max_type,0,style))->array_modify(i-1,j-1,ratio);
      }
    }

    // get reference to deltan_ratio
    deltan_ratio = static_cast<FixPropertyGlobal*>(modify->find_fix_property("youngsModulusOriginal","property/global","peratomtype",max_type,0,style))->get_array_modified();
  }

  //NP Get pointer to all the fixes (also those that have the material properties)
  updatePtrs();

  // error checks on coarsegraining
  if(force->cg_active())
    error->cg(FLERR,this->style);
}

/* ---------------------------------------------------------------------- */

void FixHeatGranCond::post_force(int vflag)
{
  //template function for using touchflag or not
  if(history_flag == 0) post_force_eval<0>(vflag,0);
  if(history_flag == 1) post_force_eval<1>(vflag,0);

}

/* ---------------------------------------------------------------------- */

void FixHeatGranCond::cpl_evaluate(ComputePairGranLocal *caller)
{
  if(caller != cpl) error->all(FLERR,"Illegal situation in FixHeatGranCond::cpl_evaluate");
  if(history_flag == 0) post_force_eval<0>(0,1);
  if(history_flag == 1) post_force_eval<1>(0,1);
}

/* ---------------------------------------------------------------------- */

template <int HISTFLAG>
void FixHeatGranCond::post_force_eval(int vflag,int cpl_flag)
{
  double hc,contactArea,delta_n,flux,dirFlux[3];
  int i,j,ii,jj,inum,jnum;
  double xtmp,ytmp,ztmp,delx,dely,delz;
  double radi,radj,radsum,rsq,r,tcoi,tcoj;
  int *ilist,*jlist,*numneigh,**firstneigh;
  int *touch,**firsttouch;

  int newton_pair = force->newton_pair;

  //NP see implementation of match in atom.cpp, this accounts for hybrid/overlay
  if (strcmp(force->pair_style,"hybrid")==0)
    error->warning(FLERR,"Fix heat/gran/conduction implementation may not be valid for pair style hybrid");
  if (strcmp(force->pair_style,"hybrid/overlay")==0)
    error->warning(FLERR,"Fix heat/gran/conduction implementation may not be valid for pair style hybrid/overlay");

  inum = pair_gran->list->inum;
  ilist = pair_gran->list->ilist;
  numneigh = pair_gran->list->numneigh;
  firstneigh = pair_gran->list->firstneigh;
  if(HISTFLAG) firsttouch = pair_gran->listgranhistory->firstneigh;

  double *radius = atom->radius;
  double **x = atom->x;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  int *mask = atom->mask;

  //NP update because re-allocation might have taken place
  updatePtrs();

  // loop over neighbors of my atoms
  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    radi = radius[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];
    if(HISTFLAG) touch = firsttouch[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      j &= NEIGHMASK;

      if (!(mask[i] & groupbit) && !(mask[j] & groupbit)) continue;

      //NP no touch flag available - need to evaluate here
      if(!HISTFLAG)
      {
        delx = xtmp - x[j][0];
        dely = ytmp - x[j][1];
        delz = ztmp - x[j][2];
        rsq = delx*delx + dely*dely + delz*delz;
        radj = radius[j];
        radsum = radi + radj;
      }

      //NP touchflag if available or distance if no touchflag available
      if ((HISTFLAG && touch[jj]) || (!HISTFLAG && (rsq < radsum*radsum))) {  //contact
        //NP for history we need to
        if(HISTFLAG)
        {
          delx = xtmp - x[j][0];
          dely = ytmp - x[j][1];
          delz = ztmp - x[j][2];
          rsq = delx*delx + dely*dely + delz*delz;
          radj = radius[j];
          radsum = radi + radj;
          if(rsq >= radsum*radsum) continue;
        }

        r = sqrt(rsq);

        //NP adjust overlap that may be superficially large due to softening
        if(area_correction_flag)
        {
          delta_n = radsum - r;
          delta_n *= deltan_ratio[type[i]-1][type[j]-1];
          r = radsum - delta_n;
        }

        contactArea = - M_PI/4 * ( (r-radi-radj)*(r+radi-radj)*(r-radi+radj)*(r+radi+radj) )/(r*r); //contact area of the two spheres

        tcoi = conductivity[type[i]-1];
        tcoj = conductivity[type[j]-1];
        if (tcoi < SMALL || tcoj < SMALL) hc = 0.;
        else hc = 4.*tcoi*tcoj/(tcoi+tcoj)*sqrt(contactArea);

        flux = (Temp[j]-Temp[i])*hc;

        dirFlux[0] = flux*delx;
        dirFlux[1] = flux*dely;
        dirFlux[2] = flux*delz;
        if(!cpl_flag)
        {
          //Add half of the flux (located at the contact) to each particle in contact
          heatFlux[i] += flux;
          directionalHeatFlux[i][0] += 0.50 * dirFlux[0];
          directionalHeatFlux[i][1] += 0.50 * dirFlux[1];
          directionalHeatFlux[i][2] += 0.50 * dirFlux[2];
          if (newton_pair || j < nlocal)
          {
            heatFlux[j] -= flux;
            directionalHeatFlux[j][0] += 0.50 * dirFlux[0];
            directionalHeatFlux[j][1] += 0.50 * dirFlux[1];
            directionalHeatFlux[j][2] += 0.50 * dirFlux[2];
          }

        }

        if(cpl_flag && cpl) cpl->add_heat(i,j,flux);
      }
    }
  }

  //NP reverse comm to send heat fluxes back
  //NP only necessary in case of newton_pair=1, since pair stored once on all procs
  if(newton_pair) fix_heatFlux->do_reverse_comm();
  if(newton_pair) fix_directionalHeatFlux->do_reverse_comm();
}

/* ----------------------------------------------------------------------
   register and unregister callback to compute
------------------------------------------------------------------------- */

void FixHeatGranCond::register_compute_pair_local(ComputePairGranLocal *ptr)
{
   /*NL*///fprintf(screen,"FixHeatGran::register_compute_pair_local, ptr->id %s\n",ptr->id);

   if(cpl != NULL)
      error->all(FLERR,"Fix heat/gran/conduction allows only one compute of type pair/local");
   cpl = ptr;
}

void FixHeatGranCond::unregister_compute_pair_local(ComputePairGranLocal *ptr)
{
   /*NL*///fprintf(screen,"FixHeatGran::unregister_compute_pair_local\n");
   if(cpl != ptr)
       error->all(FLERR,"Illegal situation in FixHeatGranCond::unregister_compute_pair_local");
   cpl = NULL;
}
