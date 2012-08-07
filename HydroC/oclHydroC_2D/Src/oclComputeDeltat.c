/*
  A simple 2D hydro code
  (C) Romain Teyssier : CEA/IRFU           -- original F90 code
  (C) Pierre-Francois Lavallee : IDRIS      -- original F90 code
  (C) Guillaume Colin de Verdiere : CEA/DAM -- for the C version
*/

/*

This software is governed by the CeCILL license under French law and
abiding by the rules of distribution of free software.  You can  use, 
modify and/ or redistribute the software under the terms of the CeCILL
license as circulated by CEA, CNRS and INRIA at the following URL
"http://www.cecill.info". 

As a counterpart to the access to the source code and  rights to copy,
modify and redistribute granted by the license, users are provided only
with a limited warranty  and the software's author,  the holder of the
economic rights,  and the successive licensors  have only  limited
liability. 

In this respect, the user's attention is drawn to the risks associated
with loading,  using,  modifying and/or developing or reproducing the
software by the user in light of its specific status of free software,
that may mean  that it is complicated to manipulate,  and  that  also
therefore means  that it is reserved for developers  and  experienced
professionals having in-depth computer knowledge. Users are therefore
encouraged to load and test the software's suitability as regards their
requirements in conditions enabling the security of their systems and/or 
data to be ensured and,  more generally, to use and operate it in the 
same conditions as regards security. 

The fact that you are presently reading this means that you have had
knowledge of the CeCILL license and that you accept its terms.

*/

#include <stdio.h>
// #include <stdlib.h>
#include <malloc.h>
// #include <unistd.h>
#include <math.h>

#include "parametres.h"
#include "oclComputeDeltat.h"
#include "oclHydroGodunov.h"
#include "utils.h"
#include "oclEquationOfState.h"

#include "oclInit.h"
#include "ocltools.h"
#include "oclReduce.h"

#define IHV(i, j, v)  ((i) + Hnxt * ((j) + Hnyt * (v)))

void
oclComputeQEforRow(const long j, cl_mem uold, cl_mem q, cl_mem e,
                   const double Hsmallr, const long Hnx, const long Hnxt, 
		   const long Hnyt, const long Hnxyt)
{
  cl_int err = 0;
  dim3 gws, lws;
  cl_event event;
  double elapsk;

  OCLSETARG09(ker[LoopKQEforRow], j, uold, q, e, Hsmallr, Hnxt, Hnyt, Hnxyt, Hnx);
  elapsk = oclLaunchKernel(ker[LoopKQEforRow], cqueue, Hnx, THREADSSZ);
}

void
oclCourantOnXY(cl_mem courant, const long Hnx, const long Hnxyt, cl_mem c, cl_mem q, double Hsmallc)
{
  double elapsk;
  OCLSETARG06(ker[LoopKcourant], q, courant, Hsmallc, c, Hnxyt, Hnx);
  elapsk = oclLaunchKernel(ker[LoopKcourant], cqueue, Hnx, THREADSSZ);
}

void
oclComputeDeltat(double *dt, const hydroparam_t H, hydrowork_t * Hw, hydrovar_t * Hv, hydrovarwork_t * Hvw)
{
  long j;
  cl_mem uoldDEV, qDEV, eDEV, cDEV, courantDEV;
  double *lcourant;
  double maxCourant;
  long Hnxyt = H.nxyt;
  cl_int err = 0;
  long offsetIP = IHVW(0, IP);
  long offsetID = IHVW(0, ID);
  int slices = 1;

  WHERE("compute_deltat");

  //   compute time step on grid interior

  // on recupere les buffers du device qui sont deja alloues
  oclGetUoldQECDevicePtr(&uoldDEV, &qDEV, &eDEV, &cDEV);

  lcourant = (double *) calloc(Hnxyt, sizeof(double));
  courantDEV = clCreateBuffer(ctx, CL_MEM_COPY_HOST_PTR | CL_MEM_READ_WRITE, Hnxyt * sizeof(double), lcourant, &err);
  oclCheckErr(err, "clCreateBuffer");

  for (j = H.jmin + ExtraLayer; j < H.jmax - ExtraLayer; j++) {
    oclComputeQEforRow(j, uoldDEV, qDEV, eDEV, H.smallr, H.nx, H.nxt, H.nyt, H.nxyt);
    oclEquationOfState(offsetIP, offsetID, 0, H.nx, H.smallc, H.gamma, slices, H.nxyt, qDEV, eDEV, cDEV);
    // on calcule courant pour chaque cellule de la ligne pour tous les j
    oclCourantOnXY(courantDEV, H.nx, H.nxyt, cDEV, qDEV, H.smallc);
  }

  // on cherche le max global des max locaux
  maxCourant = oclReduceMax(courantDEV, H.nx);

  *dt = H.courant_factor * H.dx / maxCourant;
  err = clReleaseMemObject(courantDEV);
  oclCheckErr(err, "clReleaseMemObject");
  free(lcourant);
}                               // compute_deltat


//EOF
