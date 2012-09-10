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

#ifndef LMP_MPI_LIGGGHTS_H
#define LMP_MPI_LIGGGHTS_H

#include "mpi.h"

/* ---------------------------------------------------------------------- */
// a poor man's inline MPI wrappers for LIGGGHTS
/* ---------------------------------------------------------------------- */

namespace LAMMPS_NS
{

inline void MPI_Sum_Vector(double *vector,int len, MPI_Comm comm)
{
    double *vector_all = new double [len];
    MPI_Allreduce(vector,vector_all,len,MPI_DOUBLE,MPI_SUM,comm);
    for(int i = 0; i < len; i++) vector[i] = vector_all[i];
    delete []vector_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Sum_Scalar(double &scalar,MPI_Comm comm)
{
    double scalar_all;
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_DOUBLE,MPI_SUM,comm);
    scalar = scalar_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Sum_Scalar(double &scalar,double &scalar_all,MPI_Comm comm)
{
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_DOUBLE,MPI_SUM,comm);
}

/* ---------------------------------------------------------------------- */

inline void MPI_Sum_Vector(int *vector,int len,MPI_Comm comm)
{
    int *vector_all = new int [len];
    MPI_Allreduce(vector,vector_all,len,MPI_INT,MPI_SUM,comm);
    for(int i = 0; i < len; i++) vector[i] = vector_all[i];
    delete []vector_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Sum_Scalar(int &scalar,MPI_Comm comm)
{
    int scalar_all;
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_INT,MPI_SUM,comm);
    scalar = scalar_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Sum_Scalar(int &scalar,int &scalar_all,MPI_Comm comm)
{
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_INT,MPI_SUM,comm);
}

/* ---------------------------------------------------------------------- */

inline void MPI_Min_Scalar(double &scalar,MPI_Comm comm)
{
    double scalar_all;
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_DOUBLE,MPI_MIN,comm);
    scalar = scalar_all;
}
/* ---------------------------------------------------------------------- */

inline void MPI_Min_Scalar(double scalar, double &scalar_all,MPI_Comm comm)
{
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_DOUBLE,MPI_MIN,comm);
}

/* ---------------------------------------------------------------------- */

inline void MPI_Max_Scalar(double &scalar,MPI_Comm comm)
{
    double scalar_all;
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_DOUBLE,MPI_MAX,comm);
    scalar = scalar_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Max_Scalar(double scalar, double &scalar_all,MPI_Comm comm)
{
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_DOUBLE,MPI_MAX,comm);
}

/* ---------------------------------------------------------------------- */

inline void MPI_Min_Scalar(int &scalar,MPI_Comm comm)
{
    int scalar_all;
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_INT,MPI_MIN,comm);
    scalar = scalar_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Min_Scalar(int scalar, int &scalar_all,MPI_Comm comm)
{
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_INT,MPI_MIN,comm);
}

/* ---------------------------------------------------------------------- */

inline void MPI_Max_Scalar(int &scalar,MPI_Comm comm)
{
    int scalar_all;
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_INT,MPI_MAX,comm);
    scalar = scalar_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Max_Scalar(int scalar, int &scalar_all,MPI_Comm comm)
{
    MPI_Allreduce(&scalar,&scalar_all,1,MPI_INT,MPI_MAX,comm);
}

/* ---------------------------------------------------------------------- */

inline void MPI_Max_Vector(double *vector,int len,MPI_Comm comm)
{
    double *vector_all = new double[len];
    MPI_Allreduce(vector,vector_all,len,MPI_DOUBLE,MPI_MAX,comm);
    for(int i = 0; i < len; i++) vector[i] = vector_all[i];
    delete []vector_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Max_Vector(int *vector,int len,MPI_Comm comm)
{
    int *vector_all = new int[len];
    MPI_Allreduce(vector,vector_all,len,MPI_INT,MPI_MAX,comm);
    for(int i = 0; i < len; i++) vector[i] = vector_all[i];
    delete []vector_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Min_Vector(int *vector,int len,MPI_Comm comm)
{
    int *vector_all = new int[len];
    MPI_Allreduce(vector,vector_all,len,MPI_INT,MPI_MIN,comm);
    for(int i = 0; i < len; i++) vector[i] = vector_all[i];
    delete []vector_all;
}

/* ---------------------------------------------------------------------- */

inline void MPI_Allgather_Sum_Scalar(int scalar,int &scalar_acc,MPI_Comm comm)
{
    int rank,size, *allg;

    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    allg = new int[size];

    MPI_Allgather(&scalar,1,MPI_INT,allg,1,MPI_INT,comm);

    scalar_acc = 0;
    for (int iproc = 1; iproc < rank; iproc++)
       scalar_acc = scalar_acc + allg[iproc-1];

    delete []allg;
}

/* ----------------------------------------------------------------------
   Gather vector data from all processors at proc 0
   returns allocated and populated array vector0 to caller
------------------------------------------------------------------------- */

inline int MPI_Gather0_Vector(double *vector, int size ,double *&vector_0,MPI_Comm comm)
{
    int me,nprocs, *recvcnts, *displs;
    int size_0;

    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &me);

    recvcnts = new int[nprocs];
    displs = new int[nprocs];

    //NP gather recvcount for each processor
    //NP MPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm)
    MPI_Allgather(&size,1,MPI_INT,recvcnts,1,MPI_INT,comm);

    size_0 = 0;
    displs[0] = 0;
    for (int iproc = 1; iproc < nprocs; iproc++)
    {
        size_0 += recvcnts[iproc-1];
        displs[iproc] = displs[iproc-1] + recvcnts[iproc-1];
    }
    size_0 += recvcnts[nprocs-1];


    //NP allocate
    if(me == 0)
        vector_0 = new double[size_0];
    else
        vector_0 = 0;

    //NP use MPI_Gatherv to gather vector data at proc 0
    //NP MPI_Gatherv(sendbuf, sendcnt, sendtype, recvbuf, recvcnts, displs, recvtype, root, comm)

    MPI_Gatherv(vector,size,MPI_DOUBLE,vector_0, recvcnts, displs, MPI_DOUBLE,0, comm);

    delete []recvcnts;
    delete []displs;

    return size_0;
}


}; // end namespace LAMMPS_NS


#endif
