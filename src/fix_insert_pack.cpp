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

#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "fix_insert_pack.h"
#include "atom.h"
#include "atom_vec.h"
#include "force.h"
#include "update.h"
#include "comm.h"
#include "modify.h"
#include "region.h"
#include "domain.h"
#include "random_park.h"
#include "memory.h"
#include "error.h"
#include "fix_particledistribution_discrete.h"
#include "fix_template_sphere.h"
#include "vector_liggghts.h"
#include "mpi_liggghts.h"
#include "particleToInsert.h"
#include "fix_multisphere.h"

#define SEED_OFFSET 12

using namespace LAMMPS_NS;
using namespace FixConst;

/* ---------------------------------------------------------------------- */

FixInsertPack::FixInsertPack(LAMMPS *lmp, int narg, char **arg) :
  FixInsert(lmp, narg, arg)
{
  // set defaults first, then parse args
  init_defaults();

  bool hasargs = true;
  while(iarg < narg && hasargs)
  {
    hasargs = false;
    if (strcmp(arg[iarg],"region") == 0) {
      if (iarg+2 > narg) error->fix_error(FLERR,this,"");
      int iregion = domain->find_region(arg[iarg+1]);
      if (iregion == -1) error->fix_error(FLERR,this,"region ID does not exist");
      ins_region = domain->regions[iregion];
      iarg += 2;
      hasargs = true;
    } else if (strcmp(arg[iarg],"volumefraction_region") == 0) {
      if (iarg+2 > narg) error->fix_error(FLERR,this,"");
      volumefraction_region = atof(arg[iarg+1]);
      if(volumefraction_region < 0. || volumefraction_region > 1.)
        error->fix_error(FLERR,this,"Invalid volumefraction");
      iarg += 2;
      hasargs = true;
    } else if (strcmp(arg[iarg],"particles_in_region") == 0) {
      if (iarg+2 > narg)
        error->fix_error(FLERR,this,"");
      ntotal_region = atoi(arg[iarg+1]);
      if(ntotal_region <= 0) error->fix_error(FLERR,this,"'ntotal_region' > 0 required");
      iarg += 2;
      hasargs = true;
    } else if (strcmp(arg[iarg],"mass_in_region") == 0) {
      if (iarg+2 > narg) error->fix_error(FLERR,this,"");
      masstotal_region = atof(arg[iarg+1]);
      if(masstotal_region <= 0.)
        error->fix_error(FLERR,this,"'masstotal_region' > 0 required");
      iarg += 2;
      hasargs = true;
    } else if (strcmp(arg[iarg],"ntry_mc") == 0) {
      if (iarg+2 > narg) error->fix_error(FLERR,this,"");
      ntry_mc = atoi(arg[iarg+1]);
      if(ntry_mc < 1000) error->fix_error(FLERR,this,"ntry_mc must be > 1000");
      iarg += 2;
      hasargs = true;
    } else if(strcmp(style,"insert/pack") == 0)
        error->fix_error(FLERR,this,"unknown keyword");
  }

  // no fixed total number of particles inserted by this fix exists
  ninsert_exists = 0;
}

/* ---------------------------------------------------------------------- */

FixInsertPack::~FixInsertPack()
{

}

/* ---------------------------------------------------------------------- */

//NP do NOT call FixInsert::init_defaults() from here
//NP this would overwrite settings that were parsed in FixInsert constructor
//NP since init_defaults() is called from constructor of both classes, both
//NP FixInsert::init_defaults() and FixInsertPack::init_defaults() are called
//NP at the correct point

void FixInsertPack::init_defaults()
{
      ins_region = NULL;
      ntry_mc = 100000;

      volumefraction_region = 0.0;
      ntotal_region = 0;
      masstotal_region = 0.0;

      region_volume = region_volume_local = 0.;
}

/* ----------------------------------------------------------------------
   perform error checks
------------------------------------------------------------------------- */

void FixInsertPack::calc_insertion_properties()
{
    double dt = update->dt;

    // error check on region
    if(!ins_region)
        error->fix_error(FLERR,this,"must define an insertion region");
    ins_region->reset_random(seed + SEED_OFFSET);
    ins_region->volume_mc(ntry_mc,region_volume,region_volume_local);
    if(region_volume <= 0. || region_volume_local < 0. || region_volume_local > region_volume)
        error->one(FLERR,"Fix insert: Region volume calculation with MC failed");

    if(ins_region->dynamic_check())
        error->fix_error(FLERR,this,"dynamic regions are not allowed");

    /*NL*///fprintf(screen,"FixInsertPack: Volume of insertion region: %f\n",region_volume);

    // error check on insert_every
    if(insert_every < 0)
        error->fix_error(FLERR,this,"must define 'insert_every'");

    // error checks to disallow args from FixInsert
    if(ninsert > 0 || massinsert > 0.)
        error->fix_error(FLERR,this,"specifying 'nparticles' or 'mass' not allowed");
    if(nflowrate > 0. || massflowrate > 0.)
        error->fix_error(FLERR,this,"specifying 'nflowrate' or 'massflowrate' not allowed");


    // error check if exactly one target is specified
    int n_defined = 0;
    if(volumefraction_region > 0.) n_defined++;
    if(ntotal_region > 0) n_defined++;
    if(masstotal_region > 0.) n_defined++;

    if(n_defined != 1)
        error->fix_error(FLERR,this,"must define exactly one keyword out of 'volumefraction_region', 'particles_in_region', and 'mass_in_region'");

}

/* ----------------------------------------------------------------------
   number of particles to insert this timestep
   depends on number of particles in region already
------------------------------------------------------------------------- */

int FixInsertPack::calc_ninsert_this()
{
  int nlocal = atom->nlocal;
  double **x = atom->x;
  double *rmass = atom->rmass;
  double *radius = atom->radius;
  int *mask = atom->mask;

  int ninsert_this = 0;

  // check if region extends outside simulation box
  // if so, throw error if boundary setting is "f f f"

  if(ins_region->bbox_extends_outside_box())
  {
      for(int idim = 0; idim < 3; idim++)
        for(int iface = 0; iface < 2; iface++)
            if(domain->boundary[idim][iface] == 1)
                error->fix_error(FLERR,this,"Insertion region extends outside simulation box and a fixed boundary is used."
                            "Please use non-fixed boundaries in this case only");
  }

  // get number of particles, masss and occupied volume in insertion region
  // use all particles, not only those in the fix group

  int np_region = 0;
  double vol_region = 0., mass_region = 0.;
  double _4Pi3 = 4.*M_PI/3.;
  for(int i = 0; i < nlocal; i++)
  {
      //NP only count single particles
      if(fix_multisphere && fix_multisphere->belongs_to(i) >= 0) continue;
      if
      (
        ((!all_in_flag) && ins_region->match(x[i][0],x[i][1],x[i][2]))    ||
        (( all_in_flag) && ins_region->match_shrinkby_cut(x[i],radius[i]))
      )
      {
          np_region++;
          vol_region += _4Pi3*radius[i]*radius[i]*radius[i];
          mass_region += rmass[i];
      }
  }

  //NP count bodies for multisphere
  int nbody;
  double x_bound_body[3], mass_body, r_bound_body, density_body;
  if(multisphere)
  {
      nbody = multisphere->n_body();

      for(int ibody = 0; ibody < nbody; ibody++)
      {

          multisphere->x_bound(x_bound_body,ibody);
          r_bound_body = multisphere->r_bound(ibody);
          if
          (
              !all_in_flag && ins_region->match(x_bound_body[0],x_bound_body[1],x_bound_body[2])  ||
              all_in_flag && ins_region->match_shrinkby_cut(x_bound_body,r_bound_body)
          )
          {
              np_region++;
              mass_body = multisphere->mass(ibody);
              density_body = multisphere->density(ibody);
              vol_region += mass_body/density_body;
              mass_region += mass_body;
          }
      }
  }

  // calculate and return number of particles that is missing

  if(volumefraction_region > 0.)
  {
     MPI_Sum_Scalar(vol_region,world);
      ninsert_this = static_cast<int>((volumefraction_region*region_volume - vol_region) / fix_distribution->vol_expect() + random->uniform());
  }
  else if(ntotal_region > 0)
  {
     MPI_Sum_Scalar(np_region,world);
      ninsert_this = ntotal_region - np_region;
  }
  else if(masstotal_region > 0.)
  {
     MPI_Sum_Scalar(mass_region,world);
      ninsert_this = static_cast<int>((masstotal_region - mass_region) / fix_distribution->mass_expect() + random->uniform());
  }
  else error->one(FLERR,"Internal error in FixInsertPack::calc_ninsert_this()");

  // can be < 0 due to round-off etc
  if(ninsert_this < 0) ninsert_this = 0;

  //NP number of particles in region, volume and mass are not counted correctly for clumps
  //NP update - should be correct now
  //NP if(fix_rm && (np_region > 0 || vol_region > 0. || mass_region > 0.))
  //NP   error->warning(FLERR,"Fix insert/pack insertion volume is partly filled and you are using multisphere particles - command does not work accurately in this case");

  /*NL*/ // fprintf(screen,"ninsert_this %d\n",ninsert_this);

  return ninsert_this;
}

/* ---------------------------------------------------------------------- */

double FixInsertPack::insertion_fraction()
{
    /*NL*/ //fprintf(screen,"proc %d: region_volume_local %f , region_volume %f\n",comm->me,region_volume_local,region_volume);
    return region_volume_local/region_volume;
}

/* ---------------------------------------------------------------------- */

inline int FixInsertPack::is_nearby(int i)
{
    double pos[3], rad, cut;

    vectorCopy3D(atom->x[i],pos);
    rad = atom->radius[i];

    // choose right distance depending on all_in_flag

    if(all_in_flag) cut = maxrad;
    else cut = rad + maxrad;

    if(ins_region->match_expandby_cut(pos,cut)) return 1;
    return 0;
}

/* ----------------------------------------------------------------------
   generate random positions within insertion volume
   perform overlap check via xnear if requested
   returns # bodies and # spheres that could actually be inserted
------------------------------------------------------------------------- */

void FixInsertPack::x_v_omega(int ninsert_this_local,int &ninserted_this_local, int &ninserted_spheres_this_local, double &mass_inserted_this_local)
{
    ninserted_this_local = ninserted_spheres_this_local = 0;
    mass_inserted_this_local = 0.;

    double pos[3];
    ParticleToInsert *pti;
    /*NL*/// fprintf(screen,"STARTED\n");

    // no overlap check
    if(!check_ol_flag)
    {
        for(int itotal = 0; itotal < ninsert_this_local; itotal++)
        {
            pti = fix_distribution->pti_list[ninserted_this_local];
            double rbound = pti->r_bound_ins;

            if(all_in_flag) ins_region->generate_random_shrinkby_cut(pos,rbound,true);
            else ins_region->generate_random(pos,true);

            // could ramdonize vel, omega, quat here

            if(pos[0] == 0. && pos[1] == 0. && pos[2] == 0.)
                error->one(FLERR,"FixInsertPack::x_v_omega() illegal position");
            ninserted_spheres_this_local += pti->set_x_v_omega(pos,v_insert,omega_insert,quat_insert);
            mass_inserted_this_local += pti->mass_ins;
            ninserted_this_local++;

            /*NL*///printVec3D(screen,"random pos",pos);
        }
    }
    // overlap check
    // account for maxattempt
    // pti checks against xnear and adds self contributions
    else
    {
        int ntry = 0;
        int maxtry = ninsert_this_local * maxattempt;
        /*NL*/fprintf(screen,"proc %d ninsert_this_local %d maxtry %d\n",comm->me,ninsert_this_local,maxtry);

        while(ntry < maxtry && ninserted_this_local < ninsert_this_local)
        {
            /*NL*///fprintf(screen,"proc %d setting props for pti #%d, maxtry %d\n",comm->me,ninserted_this_local,maxtry);
            pti = fix_distribution->pti_list[ninserted_this_local];
            double rbound = pti->r_bound_ins;

            int nins = 0;
            while(nins == 0 && ntry < maxtry)
            {
                //NP do not need this since calling with flag true will only generate subbox positions
                //do
                //{
                    if(all_in_flag) ins_region->generate_random_shrinkby_cut(pos,rbound,true);
                    else ins_region->generate_random(pos,true);
                    ntry++;
                //}
                //while(ntry < maxtry && domain->dist_subbox_borders(pos) < rbound);

                // could ramdonize vel, omega, quat here

                nins = pti->check_near_set_x_v_omega(pos,v_insert,omega_insert,quat_insert,xnear,nspheres_near);

                /*NL*///printVec3D(screen,"random pos",pos);
                /*NL*///fprintf(screen,"nins %d\n",nins);
            }

            if(nins > 0)
            {
                ninserted_spheres_this_local += nins;
                mass_inserted_this_local += pti->mass_ins;
                ninserted_this_local++;
            }
        }
    }
    /*NL*/ //fprintf(screen,"FINISHED on proc %d\n",comm->me);
}

/* ---------------------------------------------------------------------- */

void FixInsertPack::restart(char *buf)
{
    FixInsert::restart(buf);

    ins_region->reset_random(seed + SEED_OFFSET);
}
