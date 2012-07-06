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

/* ----------------------------------------------------------------------
   Contributing authors:
   Philippe Seil (JKU Linz)
   Christoph Kloss (JKU Linz, DCS Computing GmbH, Linz)
------------------------------------------------------------------------- */


#ifndef LMP_CONTACT_HISTORY_I_H
#define LMP_CONTACT_HISTORY_I_H

  //NP coded in the header, so they can be inlined by other classes

  /* ---------------------------------------------------------------------- */

  inline void FixContactHistory::handleContact(int iP, int idTri, double *&history)
  {
    // check if contact with iTri was there before
    // if so, set history to correct location and return
    if(haveContact(iP,idTri,history))
      return;

    // else check if one of the present contacts is coplanar with iTri
    // if so, copy history, set history pointer to correct location,
    // set delete flag and return
    if(hasContactCoplanarTo(iP,idTri,history))
      return;

    // else new contact - add contact
    addNewTriContactToExistingParticle(iP,idTri,history);
  }

  /* ---------------------------------------------------------------------- */

  inline void FixContactHistory::handleNoContact(int iP, int idTri)
  {
    // check if contact is present - if yes, set deleteflag
    //NP do not delete at this point to enable shear history transfer

    for(int j = 0; j < npartner[iP]; j++)
    {
        if(partner[iP][j] == idTri)
        {
            delflag[iP][j] = true;
            break;
        }
    }
  }

  /* ---------------------------------------------------------------------- */

  inline bool FixContactHistory::haveContact(int iP, int idTri, double *&history)
  {
    int *tri = partner[iP];

    for(int i = 0; i < npartner[iP]; i++)
    {
        if(tri[i] == idTri)
        {
            history = contacthistory[iP][i];
            return true;
        }
    }
    return false;
  }

  /* ---------------------------------------------------------------------- */

  inline bool FixContactHistory::hasContactCoplanarTo(int iP, int idTri, double *&history)
  {

    int *tri = partner[iP];
    for(int i = 0; i < npartner[iP]; i++)
    {
      if(mesh_->areCoplanar(tri[i],idTri))
      {
        tri[i] = idTri;
        history = contacthistory[iP][i];
        delflag[iP][i] = false;
        return true;
      }
    }
    return false;
  }

  /* ---------------------------------------------------------------------- */

  inline void FixContactHistory::addNewTriContactToExistingParticle(int iP, int idTri, double *&history)
  {
      int numCont = npartner[iP];
      if(numCont == maxtouch)
        grow_arrays_maxtouch(atom->nmax);

      partner[iP][numCont] = idTri;
      delflag[iP][numCont] = false;
      history = contacthistory[iP][numCont];
      for(int i = 0; i < dnum; i++)
        history[i] = 0.;

      npartner[iP]++;
  }

  /* ---------------------------------------------------------------------- */

  inline int FixContactHistory::n_contacts()
  {
    int ncontacts = 0, nlocal = atom->nlocal;

    for(int i = 0; i < nlocal; i++)
           ncontacts += npartner[i];
    return ncontacts;
  }

  /* ---------------------------------------------------------------------- */

  inline int FixContactHistory::n_contacts(int contact_groupbit)
  {
    int ncontacts = 0, nlocal = atom->nlocal;
    int *mask = atom->mask;

    for(int i = 0; i < nlocal; i++)
        if(mask[i] & contact_groupbit)
           ncontacts += npartner[i];
    return ncontacts;
  }
#endif
