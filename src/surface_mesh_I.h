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
   Christoph Kloss (JKU Linz, DCS Computing GmbH, Linz)
   Philippe Seil (JKU Linz)
------------------------------------------------------------------------- */

#ifndef LMP_SURFACE_MESH_I_H
#define LMP_SURFACE_MESH_I_H

#define NTRY_MC_SURFACE_MESH_I_H 30000
#define NITER_MC_SURFACE_MESH_I_H 5
#define TOLERANCE_MC_SURFACE_MESH_I_H 0.05

/*NL*/ #define DEBUGMODE_SURFACE_MESH false

/* ----------------------------------------------------------------------
   constructors, destructor
------------------------------------------------------------------------- */


template<int NUM_NODES>
SurfaceMesh<NUM_NODES>::SurfaceMesh(LAMMPS *lmp)
:   TrackingMesh<NUM_NODES>(lmp),
    isInsertionMesh_(false),
    curvature_(1.-EPSILON_CURVATURE),

    // TODO should keep areaMeshSubdomain up-to-date more often for insertion faces
    areaMesh_     (*this->prop().template addGlobalProperty   < ScalarContainer<double> >                 ("areaMesh",     "comm_none","frame_trans_rot_invariant","restart_no",2)),

    //NP neigh topology is communicated at exchange and borders
    //NP  (neigh topology is created once and never changed)

    //NP no forward communication at all
    //NP if scale,move,translate: properties are manipulated/updated via CustomValueTracker

    area_         (*this->prop().template addElementProperty< ScalarContainer<double> >                   ("area",         "comm_none","frame_trans_rot_invariant", "restart_no",2)),
    areaAcc_      (*this->prop().template addElementProperty< ScalarContainer<double> >                   ("areaAcc",      "comm_none","frame_trans_rot_invariant", "restart_no",2)),
    edgeLen_      (*this->prop().template addElementProperty< VectorContainer<double,NUM_NODES> >         ("edgeLen",      "comm_none","frame_trans_rot_invariant", "restart_no")),
    edgeVec_      (*this->prop().template addElementProperty< MultiVectorContainer<double,NUM_NODES,3> >  ("edgeVec",      "comm_none","frame_scale_trans_invariant","restart_no")),
    edgeNorm_     (*this->prop().template addElementProperty< MultiVectorContainer<double,NUM_NODES,3> >  ("edgeNorm",     "comm_none","frame_scale_trans_invariant","restart_no")),
    surfaceNorm_  (*this->prop().template addElementProperty< VectorContainer<double,3> >                 ("surfaceNorm",  "comm_none","frame_scale_trans_invariant","restart_no")),
    edgeActive_   (*this->prop().template addElementProperty< VectorContainer<bool,NUM_NODES> >           ("edgeActive",   "comm_exchange_borders","frame_invariant",            "restart_no")),
    cornerActive_ (*this->prop().template addElementProperty< VectorContainer<bool,NUM_NODES> >           ("cornerActive", "comm_exchange_borders","frame_invariant",            "restart_no")),
    hasNonCoplanarSharedNode_(*this->prop().template addElementProperty< VectorContainer<bool,NUM_NODES> >("hasNonCoplanarSharedNode","comm_exchange_borders","frame_invariant", "restart_no")),
    nNeighs_      (*this->prop().template addElementProperty< ScalarContainer<int> >                      ("nNeighs",      "comm_exchange_borders","frame_invariant",            "restart_no")),
    //NP fundamental assumption: no hanging nodes
    neighFaces_   (*this->prop().template addElementProperty< VectorContainer<int,NUM_NODES> >            ("neighFaces",   "comm_exchange_borders","frame_invariant",            "restart_no"))
{
    //NP allocate 4 scalar spaces
    areaMesh_.add(0.);
    areaMesh_.add(0.);
    areaMesh_.add(0.);
    areaMesh_.add(0.);
    /*NL*///this->error->all(FLERR,"check: use ID instead of index for neigh list, areCoplanar etc");
}

template<int NUM_NODES>
SurfaceMesh<NUM_NODES>::~SurfaceMesh()
{}

/* ----------------------------------------------------------------------
   set mesh curvature, used for mesh topology
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::setCurvature(double _curvature)
 {
    curvature_ = _curvature;
}

/* ----------------------------------------------------------------------
   set flag if used as insertion mesh
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::useAsInsertionMesh()
{
    /*NL*/ //this->error->all(FLERR,"useAsInsertionMesh() called");
    isInsertionMesh_ = true;
}

/* ----------------------------------------------------------------------
   add and delete an element
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::addElement(double **nodeToAdd)
{
    TrackingMesh<NUM_NODES>::addElement(nodeToAdd);

    //NP need to do this because some classes may access data before
    //NP setup() is called
    calcSurfPropertiesOfNewElement();
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::deleteElement(int n)
{
    TrackingMesh<NUM_NODES>::deleteElement(n);
}

/* ----------------------------------------------------------------------
   recalculate properties on setup (on start and during simulation)
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::refreshOwned(int setupFlag)
{
    TrackingMesh<NUM_NODES>::refreshOwned(setupFlag);
    // (re)calculate all properties for owned elements
    //NP calculates properties for newly arrived elements
    //NP also removes round-off isues for moving mesh
    recalcLocalSurfProperties();
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::refreshGhosts(int setupFlag)
{
    TrackingMesh<NUM_NODES>::refreshGhosts(setupFlag);

    recalcGhostSurfProperties();
}

/* ----------------------------------------------------------------------
   recalculate properties of local elements
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::recalcLocalSurfProperties()
{
    //NP could use this function instead of rotating
    //NP all properties
    //NP execute this after re-neighboring via
    //NP refresh() so no round-off issues occur

    // areaMeshGlobal [areaMesh_(0)] and areaMeshOwned [areaMesh_(1)]
    // calculated here

    double areaAccOff;

    areaMesh_(0) = 0.;
    areaMesh_(1) = 0.;

    int nlocal = this->sizeLocal();

    for(int i = 0; i < nlocal; i++)
    {
      calcEdgeVecLen(i, edgeLen(i), edgeVec(i));
      calcSurfaceNorm(i, surfaceNorm(i));
      calcEdgeNormals(i, edgeNorm(i));
      area(i) = calcArea(i);
      areaAcc(i) = area(i);
      if(i > 0) areaAcc(i) += areaAcc(i-1);

      // add to local area
      areaMesh_(1) += area(i);
      /*NL*///fprintf(this->screen,"triangle %d: area %f, areaacc %f, mesharea %f\n",i,area_(i),areaAcc_(i),areaMesh_);
    }

    // mesh area must be summed up
    MPI_Sum_Scalar(areaMesh_(1),areaMesh_(0),this->world);

    /*NL*/// fprintf(this->screen,"proc %d, areaMeshGlobal() %f,areaMeshOwned() %f,areaMeshGhost() %f\n",
    /*NL*///         this->comm->me,areaMeshGlobal(),areaMeshOwned(),areaMeshGhost());
    /*NL*/// this->error->all(FLERR,"check this");
}

/* ----------------------------------------------------------------------
   recalculate properties of ghost elements
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::recalcGhostSurfProperties()
{
    double pos[3], areaCheck;
    int n_succ, n_iter;
    int nlocal = this->sizeLocal();
    int nall = this->sizeLocal()+this->sizeGhost();

    // areaMeshGhost [areaMesh_(2)] and areaMeshSubdomain [areaMesh_(3)]
    // calculated here

    // accumulated area includes owned and ghosts
    areaMesh_(2) = 0.;
    for(int i = nlocal; i < nall; i++)
    {

      calcEdgeVecLen(i, edgeLen(i), edgeVec(i));
      calcSurfaceNorm(i, surfaceNorm(i));
      calcEdgeNormals(i, edgeNorm(i));
      area(i) = calcArea(i);
      areaAcc(i) = area(i);
      if(i > 0) areaAcc(i) += areaAcc(i-1);

      // add to ghost area
      areaMesh_(2) += area(i);
    }

    /*NL*/// fprintf(this->screen,"proc %d, areaMeshGlobal() %f,areaMeshOwned() %f,areaMeshGhost() %f\n",
    /*NL*///         this->comm->me,areaMeshGlobal(),areaMeshOwned(),areaMeshGhost());
    /*NL*/// this->error->all(FLERR,"check this");

    /*NL*///fprintf(this->screen,"proc %d: areaMeshOwned+Ghost %f areaAcc(lastGhost) %f SHOULD BE EQUAL\n",
    /*NL*///        this->comm->me,areaMeshOwned()+areaMeshGhost(),areaAcc(nall-1));
    /*NL*/// this->error->all(FLERR,"CHECK this");

    /*NL*///fprintf(this->screen,"proc %d: isInsertionMesh_ %s\n",this->comm->me,isInsertionMesh_?"true":"false");
    /*NL*/// this->error->all(FLERR,"CHECK this");

    // calc area of owned and ghost elements in my subdomain
    //NP use monte carlo
    areaMesh_(3) = 0.;
    areaCheck = 0.;

    if(isInsertionMesh_)
    {
        n_succ = 0;
        n_iter = 0;

        // iterate long enough so MC has the desired tolerance
        while( (n_iter < NITER_MC_SURFACE_MESH_I_H) &&
               (fabs((areaCheck-areaMeshGlobal()))/areaMeshGlobal() > TOLERANCE_MC_SURFACE_MESH_I_H) )
        {
            // only generate random positions if I have any mesh elements
            if(nall)
            {
                for(int i = 0; i < NTRY_MC_SURFACE_MESH_I_H; i++)
                {
                    // pick a random position on owned or ghost element
                    if((generateRandomOwnedGhost(pos)) >= 0 && (this->domain->is_in_extended_subdomain(pos)))
                        n_succ++;
                }
            }
            n_iter++;
            areaMesh_(3) = static_cast<double>(n_succ)/static_cast<double>(NTRY_MC_SURFACE_MESH_I_H*n_iter) * (areaMeshOwned()+areaMeshGhost());

            MPI_Sum_Scalar(areaMesh_(3),areaCheck,this->world);
            /*NL*/// fprintf(this->screen,"proc %d: iter  %d area %f, areaCheck %f areaMeshGlobal %f\n",this->comm->me,n_iter,areaMesh_(3),areaCheck);
        }

        if(fabs((areaCheck-areaMeshGlobal()))/areaMeshGlobal() > TOLERANCE_MC_SURFACE_MESH_I_H)
        {
            /*NL*/ fprintf(this->screen,"proc %d: area %f, areaCheck %f areaMeshGlobal %f\n",this->comm->me,areaMesh_(3),areaCheck,areaMeshGlobal());
            this->error->all(FLERR,"Local mesh area calculation failed, try boosting NITER_MC_SURFACE_MESH_I_H");
        }

        // correct so sum of all owned areas is equal to global area
        areaMesh_(3) *= areaMeshGlobal()/areaCheck;

        /*NL*///fprintf(this->screen,"proc %d: sizeGlobal() %d, sizeLocal() %d, sizeGhost() %d,  areaMeshGlobal %f areaMeshOwned %f  areaMeshGhost %f areaMeshLocal %f n_iter %d tolerance %f\n",
        /*NL*///        this->comm->me,this->sizeGlobal(),this->sizeLocal(),this->sizeGhost(),areaMeshGlobal(),areaMeshOwned(),areaMeshGhost(),areaMeshLocal(),
        /*NL*///        n_iter,((areaCheck-areaMeshGlobal()))/areaMeshGlobal());
        /*NL*///this->error->all(FLERR,"CHECK this");
    }

    /*NL*/ //if(this->map(21) >= 0) fprintf(this->screen,"proc %d has ID 21 and edgeActive(21)[1] is %s\n",this->comm->me,edgeActive(this->map(21))[1]?"y":"n");
}

/* ----------------------------------------------------------------------
   recalculate some of the properties
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::recalcVectors()
{
    for(int i=0;i<this->size();i++)
    {
      calcEdgeVecLen(i, edgeLen_(i), edgeVec(i));
      calcSurfaceNorm(i, surfaceNorm(i));
      calcEdgeNormals(i, edgeNorm(i));
    }
}

/* ----------------------------------------------------------------------
   generate a random Element by areaAcc
------------------------------------------------------------------------- */

template<int NUM_NODES>
inline int SurfaceMesh<NUM_NODES>::randomOwnedGhostElement()
{
    //NP disallow to use this unless this is an insertion mesh
    if(!isInsertionMesh_) this->error->one(FLERR,"Illegal call for non-insertion mesh");

    double r = this->random_->uniform() * (areaMeshOwned()+areaMeshGhost());
    /*NL*/ //fprintf(this->screen,"areaMeshOwned()+areaMeshGhost() %f\n",areaMeshOwned()+areaMeshGhost());

    int first = 0;
    int last = this->sizeLocal()+this->sizeGhost()-1;

    return searchElementByAreaAcc(r,first,last);
}

template<int NUM_NODES>
inline int SurfaceMesh<NUM_NODES>::searchElementByAreaAcc(double area,int lo, int hi)
{
    /*NL*/ //fprintf(this->screen,"areaAcc(lo) %f areaAcc(hi) %f\n",areaAcc(lo),areaAcc(hi));

    if( (lo < 1 || area > areaAcc(lo-1)) && (area <= areaAcc(lo)) )
        return lo;
    if( (hi < 1 || area > areaAcc(hi-1)) && (area <= areaAcc(hi)) )
        return hi;

    int mid = static_cast<int>((lo+hi)/2);
    if(area > areaAcc(mid))
        return searchElementByAreaAcc(area,mid,hi);
    else
        return searchElementByAreaAcc(area,lo,mid);
}

/* ----------------------------------------------------------------------
   calculate surface properties of new element
   only called once on import
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::calcSurfPropertiesOfNewElement()
{
    //NP IMPORTANT: do not use add() functions here
    //NP rather use set
    //NP this is b/c elements have been added already
    //NP in TrackingMesh::addElement()
    //NP via customValues_.grow()

    //NP always called in serial, so use sizeLocal()

    int n = this->sizeLocal()-1;

    double *vecTmp3,*vecTmpNumNodes,**nodeTmp;
    create<double>(vecTmp3,3);
    create<double>(vecTmpNumNodes,NUM_NODES);
    create<double>(nodeTmp,NUM_NODES,3);

    // calculate edge vectors and lengths
    calcEdgeVecLen(n,vecTmpNumNodes,nodeTmp);
    edgeLen_.set(n,vecTmpNumNodes);
    edgeVec_.set(n,nodeTmp);

    /*NP
    printVec3D(this->screen,"edge len", vecTmpNumNodes);
    printVec3D(this->screen,"edge vec0", nodeTmp[0]);
    printVec3D(this->screen,"edge vec1", nodeTmp[1]);
    printVec3D(this->screen,"edge vec2", nodeTmp[2]);

    printVec3D(this->screen,"edge len", edgeLen(n));
    printVec3D(this->screen,"edge vec0", edgeVec(n)[0]);
    printVec3D(this->screen,"edge vec1", edgeVec(n)[1]);
    printVec3D(this->screen,"edge vec2", edgeVec(n)[2]);

    printVec3D(this->screen,"edge len", edgeLen(n+1));
    printVec3D(this->screen,"edge vec0", edgeVec(n+1)[0]);
    printVec3D(this->screen,"edge vec1", edgeVec(n+1)[1]);
    printVec3D(this->screen,"edge vec2", edgeVec(n+1)[2]);*/

    // calc surface normal
    calcSurfaceNorm(n,vecTmp3);
    surfaceNorm_.set(n,vecTmp3);

    // calc edge normal in plane pointing outwards of area_
    // should be (edgeVec_ cross surfaceNormal)
    calcEdgeNormals(n,nodeTmp);
    edgeNorm_.set(n,nodeTmp);

    // calc area_ from previously obtained values and add to container
    // calcArea is pure virtual and implemented in derived class(es)
    //NP need not parallelize, every provess calculates areaMesh_ and areaAcc_
    double area_elem = calcArea(n);
    areaMesh_(0) += area_elem;
    area_(n) = area_elem;
    areaAcc_(n) = area_elem;
    if(n > 0) areaAcc_(n) += areaAcc_(n-1);

    // cannot calc areaMesh_(1), areaMesh_(2), areaMesh_(3) here since
    // not parallelized at this point
    //NP but is calculated anyway via refresh() and refreshGhosts()
    //NP in MultiNodeMeshParallel::setup() and initialSetup()

    /*NL*///fprintf(this->screen,"triangle %d: id %d,area %f, areaacc %f, mesharea %f\n",n,this->id_(n),area_(n),areaAcc_(n),areaMesh_(0));

    destroy<double>(nodeTmp);
    destroy<double>(vecTmpNumNodes);
    destroy<double>(vecTmp3);

    //NP  inititalize neigh topology - need not do this
    /*NP*
    bool t[NUM_NODES], f[NUM_NODES];
    int neighs[NUM_NODES];
    for(int i=0;i<NUM_NODES;i++)
    {
        neighs[i] = -1;
        t[i] = true;
        f[i] = false;
    }
    nNeighs_.set(n,0);
    neighFaces_.set(n,neighs);
    edgeActive_.set(n,t);
    cornerActive_.set(n,t);
    hasNonCoplanarSharedNode_.set(n,f);
    */
}

/* ----------------------------------------------------------------------
   sub-functions needed to calculate mesh properties
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::calcEdgeVecLen(int nElem, double *len, double **vec)
{
    for(int i=0;i<NUM_NODES;i++)
    {
      vectorSubtract3D(
        MultiNodeMesh<NUM_NODES>::node_(nElem)[(i+1)%NUM_NODES],
        MultiNodeMesh<NUM_NODES>::node_(nElem)[i],vec[i]);
      len[i] = vectorMag3D(vec[i]);
      vectorScalarDiv3D(vec[i],len[i]);
    }
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::calcEdgeLen(int nElem, double *edgeLen)
{
    for(int i=0;i<NUM_NODES;i++)
      edgeLen[i] = vectorMag3D(edgeVec(nElem)[i]);
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::calcSurfaceNorm(int nElem, double *surfNorm)
{
    vectorCross3D(edgeVec(nElem)[0],edgeVec(nElem)[1],surfNorm);
    vectorScalarDiv3D(surfNorm, vectorMag3D(surfNorm));
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::calcEdgeNormals(int nElem, double **edgeNorm)
{
    for(int i=0;i<NUM_NODES;i++){
      vectorCross3D(edgeVec(nElem)[i],surfaceNorm(nElem),edgeNorm[i]);
      vectorScalarDiv3D(edgeNorm[i],vectorMag3D(edgeNorm[i]));
    }
}

/* ----------------------------------------------------------------------
   build neighlist, generate mesh topology, check (in)active edges and nodes
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::buildNeighbours()
{
    // iterate over all surfaces, over ghosts as well
    int nall = this->sizeLocal()+this->sizeGhost();

    // inititalize neigh topology - reset to default, ~n
    bool t[NUM_NODES], f[NUM_NODES];
    int neighs[NUM_NODES];
    for(int i=0;i<NUM_NODES;i++)
    {
        neighs[i] = -1;
        t[i] = true;
        f[i] = false;
    }
    for(int i = 0; i < nall; i++)
    {
        nNeighs_.set(i,0);
        neighFaces_.set(i,neighs);
        edgeActive_.set(i,t);
        cornerActive_.set(i,t);
        hasNonCoplanarSharedNode_.set(i,f);
    }

    /*NL*/// if(this->map(21) >= 0) fprintf(this->screen,"B proc %d has 21 and edgeActive(21)[1] is %s\n",this->comm->me,edgeActive(this->map(21))[1]?"y":"n");

    // build neigh topology, ~n*n/2
    for(int i = 0; i < nall; i++)
    {
      for(int j = i+1; j < nall; j++)
      {
        //NP continue of do not share any node
        int iNode(0), jNode(0), iEdge(0), jEdge(0);
        if(!this->shareNode(i,j,iNode,jNode)) continue;

        if(shareEdge(i,j,iEdge,jEdge))
          handleSharedEdge(i,iEdge,j,jEdge, areCoplanar(this->id(i),this->id(j)));
        else
          handleSharedNode(i,iNode,j,jNode, areCoplanar(this->id(i),this->id(j)));

        /*NL*/// if(this->map(21) >= 0) fprintf(this->screen,"A1 proc %d has 21 and edgeActive(21)[1] is %s\n",this->comm->me,edgeActive(this->map(21))[1]?"y":"n");
      }
    }

    /*NL*/ //if(this->map(21) >= 0) fprintf(this->screen,"BN proc %d has 21 and edgeActive(21)[1] is %s\n",this->comm->me,edgeActive(this->map(21))[1]?"y":"n");
    /*NL*/// this->error->all(FLERR,"end");
}

/* ----------------------------------------------------------------------
   functions to generate mesh topology
------------------------------------------------------------------------- */

template<int NUM_NODES>
bool SurfaceMesh<NUM_NODES>::areCoplanar(int tag_a, int tag_b)
{
    int a = this->map(tag_a);
    int b = this->map(tag_b);

    if(a < 0 || b < 0)
        this->error->one(FLERR,"Internal error: Illegal call to SurfaceMesh::areCoplanar()");

    // check if two faces are coplanar
    // eg used to transfer shear history btw planar faces

    double dot = vectorDot3D(surfaceNorm(a),surfaceNorm(b));
    /*NL*/// fprintf(this->screen,"a %d b %d  dot %f\n",a,b, dot);
    /*NL*/// printVec3D(this->screen,"surfaceNorm(a)",surfaceNorm(a));
    /*NL*/// printVec3D(this->screen,"surfaceNorm(b)",surfaceNorm(b));
    /*NL*/// if(fabs(dot) > curvature_) fprintf(this->screen,"a %d b %d  are coplanar \n",a,b);

    // need fabs in case surface normal is other direction
    if(fabs(dot) > curvature_) return true;
    else return false;
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::growSurface(int iSrf, double by)
{
    double *tmp = new double[3];
    for(int i=0;i<NUM_NODES;i++)
    {
      vectorSubtract3D(MultiNodeMesh<NUM_NODES>::node(iSrf)[i],this->center_(iSrf),tmp);
      vectorScalarMult3D(tmp,by);
      vectorAdd3D(MultiNodeMesh<NUM_NODES>::node(iSrf)[i],
                      tmp,MultiNodeMesh<NUM_NODES>::node(iSrf)[i]);
    }
    delete[] tmp;
    return;
}

template<int NUM_NODES>
bool SurfaceMesh<NUM_NODES>::shareEdge(int iSrf, int jSrf, int &iEdge, int &jEdge)
{
    int i,j;
    if(this->shareNode(iSrf,jSrf,i,j)){
      // following implementation of shareNode(), the only remaining option to
      // share an edge is that the next node of iSrf is equal to the previous
      // node if jSrf
      if(i==0 && MultiNodeMesh<NUM_NODES>::nodesAreEqual(iSrf,NUM_NODES-1,jSrf,(j+1)%NUM_NODES)){
        iEdge = NUM_NODES-1;
        jEdge = j;
        return true;
      }
      if(MultiNodeMesh<NUM_NODES>::nodesAreEqual(iSrf,
                      (i+1)%NUM_NODES,jSrf,(j-1+NUM_NODES)%NUM_NODES)){
        iEdge = i;//(ii-1+NUM_NODES)%NUM_NODES;
        jEdge = (j-1+NUM_NODES)%NUM_NODES;
        return true;
      }
    }
    iEdge = -1; jEdge = -1;
    return false;
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::handleSharedEdge(int iSrf, int iEdge, int jSrf, int jEdge, bool coplanar)
{
    // set neighbor topology
    neighFaces_(iSrf)[nNeighs_(iSrf)] = this->id(jSrf);
    neighFaces_(jSrf)[nNeighs_(jSrf)] = this->id(iSrf);
    nNeighs_(iSrf)++;
    nNeighs_(jSrf)++;

    /*NL*///if(this->comm->nprocs > 1)
    /*NL*///    this->error->all(FLERR,"Have to ensured active/inactive edge and corner this is done the same on 2 procs");

    // deactivate one egde
    // other as well if coplanar
    //NP IMPORTANT have to use ID criterion in parallel because local i/j are different
    if(coplanar)
    {
        edgeActive(iSrf)[iEdge] = false;
        edgeActive(jSrf)[jEdge] = false;
    }
    else
    {
        if(this->id(iSrf) < this->id(jSrf))
            edgeActive(iSrf)[iEdge] = false;
        else
            edgeActive(jSrf)[jEdge] = false;
    }

    /*NL*/ //fprintf(this->screen,"proc %d called for iSrf %d ID %d jSrf %d ID %d iEdge %d jEdge %d, coplanar %s, edgeActive(jSrf)[jEdge] %s\n",
    /*NL*/ //               this->comm->me,iSrf,this->id(iSrf),jSrf,this->id(jSrf),iEdge,jEdge,coplanar?"yes":"no",edgeActive(jSrf)[jEdge]?"y":"n");

    handleSharedNode(iSrf,iEdge,jSrf,(jEdge+1)%NUM_NODES,coplanar);
    handleSharedNode(iSrf,(iEdge+1)%NUM_NODES,jSrf,jEdge,coplanar);
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::handleSharedNode(int iSrf, int iNode, int jSrf, int jNode, bool coplanar)
{
    // coplanar - deactivate both

  int id_i = this->id(iSrf), id_j = this->id(jSrf);

  /*NL*/if(DEBUGMODE_SURFACE_MESH){
  /*NL*/  fprintf(this->screen,"triangles %d and %d, coplanar %d\n",id_i, id_j, coplanar);
  /*NL*/  fprintf(this->screen," *** iNode %d, jNode %d | iActive %d jActive %d\n",
/*NL*/	    iNode,jNode,cornerActive(iSrf)[iNode],cornerActive(jSrf)[jNode]);
  /*NL*/ }

    if(coplanar)
    {
      if( hasNonCoplanarSharedNode(iSrf)[iNode] || hasNonCoplanarSharedNode(jSrf)[jNode] ){
        if(this->id(iSrf) < this->id(jSrf))
            cornerActive(iSrf)[iNode] = false;
        else
            cornerActive(jSrf)[jNode] = false;

      } else{
        cornerActive(iSrf)[iNode] = false;
        cornerActive(jSrf)[jNode] = false;
      }
    }
    // non-coplanar - let one live
    //NP let the one with the highest ID live
    else
    {
      // save that there exists a non-coplanar shared node
      hasNonCoplanarSharedNode(iSrf)[iNode] = true;
      hasNonCoplanarSharedNode(jSrf)[jNode] = true;
        if(this->id(iSrf) < this->id(jSrf))
            cornerActive(iSrf)[iNode] = false;
        else
            cornerActive(jSrf)[jNode] = false;
    }

/*NL*/    if(DEBUGMODE_SURFACE_MESH)
/*NL*/      fprintf(this->screen," *** iNode %d, jNode %d | iActive %d jActive %d\n",
/*NL*/	      iNode,jNode,cornerActive(iSrf)[iNode],cornerActive(jSrf)[jNode]);

    /*NP  --- OLD ---
    cornerActive(iSrf)[iNode] = false;
    if
    ( coplanar &&
     !( hasNonCoplanarSharedNode(iSrf)[iNode] || hasNonCoplanarSharedNode(jSrf)[jNode] )
    )
      cornerActive(jSrf)[jNode] = false;
    else
    {
      hasNonCoplanarSharedNode(iSrf)[iNode] = true;
      hasNonCoplanarSharedNode(jSrf)[jNode] = true;
      cornerActive(jSrf)[jNode] = true;
    }*/
}

/* ----------------------------------------------------------------------
   move mesh
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::move(double *vecTotal, double *vecIncremental)
{
    TrackingMesh<NUM_NODES>::move(vecTotal,vecIncremental);
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::move(double *vecIncremental)
{
    TrackingMesh<NUM_NODES>::move(vecIncremental);
}

/* ----------------------------------------------------------------------
   scale mesh
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::scale(double factor)
{
    TrackingMesh<NUM_NODES>::scale(factor);

    /*NP
    dont have to do this here
    areaMesh_(0) = 0.;
    for(int i=0;i<this->size();i++)
    {
      calcEdgeVecLen(i, edgeLen(i), edgeVec(i));
      calcSurfaceNorm(i, surfaceNorm(i));
      calcEdgeNormals(i, edgeNorm(i));
      area(i) = calcArea(i);
      areaMesh_(0) += area(i);
      areaAcc(i) = area(i);
      if(i > 0) areaAcc(i) += areaAcc(i-1);
    }
    */
}

/* ----------------------------------------------------------------------
   rotate mesh
------------------------------------------------------------------------- */

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::rotate(double *totalQ, double *dQ,double *origin)
{
    TrackingMesh<NUM_NODES>::rotate(totalQ,dQ,origin);

    // find out if rotating every property is cheaper than
    // re-calculating them from the new nodes
    /*NP
    for(int i=0;i<this->center_.size();i++)
    {
      printVec3D(this->screen,"edgeLen from autorotate",edgeLen(i));
      printVec3D(this->screen,"edgeVec from autorotate",edgeVec(i)[0]);
      printVec3D(this->screen,"edgeVec from autorotate",edgeVec(i)[1]);
      printVec3D(this->screen,"edgeVec from autorotate",edgeVec(i)[2]);
      calcEdgeVecLen(i, edgeLen(i), edgeVec(i));
      calcSurfaceNorm(i, surfaceNorm(i));
      calcEdgeNormals(i, edgeNorm(i));
      printVec3D(this->screen,"edgeLen from recalc",edgeLen(i));
      printVec3D(this->screen,"edgeVec from recalc",edgeVec(i)[0]);
      printVec3D(this->screen,"edgeVec from recalc",edgeVec(i)[1]);
      printVec3D(this->screen,"edgeVec from recalc",edgeVec(i)[2]);
    }
    */
}

template<int NUM_NODES>
void SurfaceMesh<NUM_NODES>::rotate(double *dQ,double *origin)
{
    TrackingMesh<NUM_NODES>::rotate(dQ,origin);

    // find out if rotating every property is cheaper than
    // re-calculating them from the new nodes
    /*NP
    dont have to do this here
    for(int i=0;i<this->center_.size();i++)
    {
      printVec3D(this->screen,"edgeLen from autorotate",edgeLen(i));
      printVec3D(this->screen,"edgeVec from autorotate",edgeVec(i)[0]);
      printVec3D(this->screen,"edgeVec from autorotate",edgeVec(i)[1]);
      printVec3D(this->screen,"edgeVec from autorotate",edgeVec(i)[2]);
      calcEdgeVecLen(i, edgeLen(i), edgeVec(i));
      calcSurfaceNorm(i, surfaceNorm(i));
      calcEdgeNormals(i, edgeNorm(i));
      printVec3D(this->screen,"edgeLen from recalc",edgeLen(i));
      printVec3D(this->screen,"edgeVec from recalc",edgeVec(i)[0]);
      printVec3D(this->screen,"edgeVec from recalc",edgeVec(i)[1]);
      printVec3D(this->screen,"edgeVec from recalc",edgeVec(i)[2]);
    }
    this->error->all(FLERR,"end");
    */
}

/* ----------------------------------------------------------------------
   check if faces is planar
   used to check if a face can be used for particle insertion
------------------------------------------------------------------------- */

template<int NUM_NODES>
bool SurfaceMesh<NUM_NODES>::isPlanar()
{
    int id_j;
    int flag = 0;

    int nlocal = this->sizeLocal();

    for(int i = 0; i < this->sizeLocal(); i++)
    {
        if(flag) break;

        for(int ineigh = 0; ineigh < nNeighs_(i); ineigh++)
        {
            id_j = neighFaces_(i)[ineigh];
            if(!areCoplanar(this->id(i),id_j))
                flag = 1;
        }
    }

    MPI_Max_Scalar(flag,this->world);

    if(flag) return false;
    return true;
}

/* ----------------------------------------------------------------------
   check if point on surface - only valid if pos is in my subbox
------------------------------------------------------------------------- */

template<int NUM_NODES>
bool SurfaceMesh<NUM_NODES>::isOnSurface(double *pos)
{
    bool on_surf = false;

    int nall = this->sizeLocal()+this->sizeGhost();

    // brute force
    // loop over ghosts as well as they might overlap my subbox
    for(int i = 0; i < nall; i++)
    {
        on_surf = on_surf || isInElement(pos,i);
        if(on_surf) break;
    }

    return on_surf;
}

#endif
