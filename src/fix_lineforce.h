/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#ifdef FIX_CLASS

FixStyle(lineforce,FixLineForce)

#else

#ifndef LMP_FIX_LINEFORCE_H
#define LMP_FIX_LINEFORCE_H

#include "fix.h"

namespace LAMMPS_NS {

class FixLineForce : public Fix {
 public:
  FixLineForce(class LAMMPS *, int, char **);
  int setmask();
  void setup(int);
  void min_setup(int);
  void post_force(int);
  void post_force_respa(int, int, int);
  void min_post_force(int);

  void get_dir(double *dir)
  { dir[0] = xdir;dir[1] = ydir;dir[2] = zdir;}

 private:
  double xdir,ydir,zdir;
};

}

#endif
#endif

/* ERROR/WARNING messages:

E: Illegal ... command

Self-explanatory.  Check the input script syntax and compare to the
documentation for the command.  You can use -echo screen as a
command-line option when running LAMMPS to see the offending line.

*/
