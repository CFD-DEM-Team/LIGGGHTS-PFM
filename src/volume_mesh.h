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

#ifndef LMP_VOLUME_MESH_H
#define LMP_VOLUME_MESH_H

#include "tracking_mesh.h"
#include "container.h"

#include "surface_mesh.h"

/*

TODO

integrate volume mesh
neigh list build
etc


*/


namespace LAMMPS_NS{

template<int NUM_NODES,int N_FACES>
class VolumeMesh : public TrackingMesh<NUM_NODES>
{
  public:

    void useAsInsertionMesh();

    void addElement(double **nodeToAdd);
    void buildNeighbours();

    void move(double *vecTotal, double *vecIncremental);
    void move(double *vecIncremental);
    void scale(double factor);

    bool isInside(double *p);

    //NP generateRandomSubbox (used by fix insert/pack): return random pos in my subbox
    //NP generateRandomSubboxWithin: return random pos in my subbox delta away from proc boundaries
    //NP generateRandomOwnedGhost used internally: random pos on owned or ghost element

    virtual int generateRandomOwnedGhost(double *pos) = 0;
    virtual int generateRandomSubbox(double *pos) = 0;
    virtual int generateRandomSubboxWithin(double *pos,double delta) = 0;

    // public inline access

    // area of total mesh - all elements (all processes)
    //NP is equal to allreduce of all owned elements
    inline double volMeshGlobal()
    { return volMesh_(0);}

    // area of owned elements
    inline double volMeshOwned()
    { return volMesh_(1);}

    // area of ghost elements
    inline double volMeshGhost()
    { return volMesh_(2);}

    // area of owned and ghost elements in my subdomain
    inline double volMeshSubdomain()
    { return volMesh_(3);}

  protected:

    VolumeMesh();
    virtual ~VolumeMesh();

    void deleteElement(int n);

    void refreshOwned(int setupFlag);
    void refreshGhosts(int setupFlag);

    inline void recalcLocalVolProperties();
    inline void recalcGhostVolProperties();

    void calcVolPropertiesOfNewElement();

    virtual bool isInside(int nElem, double *p) =0;
    virtual double calcVol(int nElem) =0;
    virtual double calcCenter(int nElem) =0;

    int randomOwnedGhostElement();

    void rotate(double *totalQ, double *dQ,double *totalDispl,double *dDispl);
    void rotate(double *dQ,double *dDispl);

    // inline access
    inline double&  vol(int i)         {return (vol_)(i);}
    inline double&  volAcc(int i)      {return (volAcc_)(i);}

    // mesh properties

    ScalarContainer<double>& volMesh_; //NP see above what is contained

    // per-element properties

    ScalarContainer<double> &vol_;
    ScalarContainer<double> &volAcc_;

    // neighbor topology
    ScalarContainer<int>& nNeighs_;
    VectorContainer<int,NUM_NODES>& neighElems_;

  private:

    int searchElementByVolAcc(double vol,int lo, int hi);

    // flag indicating usage as insertion mesh
    bool isInsertionMesh_;
};

// *************************************
#include "volume_mesh_I.h"
// *************************************

} /* LAMMPS_NS */

#endif /* VOLUMEMESH_H_ */
