/* FILE : Romberg.c
 * AUTH : Maxim Yurkin
 * DESCR: 1D Romberg integration routine based on
 *        Davis P.J., Rabinowitz P. "Methods of numerical integration", Academic Press, 1975.
 *        Chapter 6.3
 *        When function is periodic simple trapezoidal rule is more suitable (Chapter 2.9), it
 *        is implemented inside the general Romberg framework.
 *
 *        Error estimates (for both cases) are based on the "bracketing" criterion (pp.330-331),
 *        they seems to be reliable (it is not so certain for trapezoid rule).
 *
 *        1D Romberg works on precalculated array of data.
 *
 *        2D Romberg is two-level integration, where final error is estimated based on both
 *        the errors of outer and inner integration. It uses function pointer to calculate values
 *        as needed. Therefore it is adaptive, but can be used in non-adaptive regime on
 *        precalculated values.
 *        Two instances of Romberg 2D should not be used in parallel (they use common storage).
 *        E.g. calculation of Csca inside orientation averaging must not be done.
 *
 *        Integration parameters are desribed in a special structure Parms_1D defined in types.h
 *        They must be set outside of the Romberg routine.
 *
 *        All the routines normalize the result on the interval width, i.e.
 *        actually averaging takes place.
 *
 *        Different implementation of Romberg integration was first coded by Martin Frijlink
 *
 * Copyright (C) 2006 University of Amsterdam
 * This code is covered by the GNU General Public License.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vars.h"
#include "types.h"
#include "const.h"
#include "Romberg.h"
#include "memory.h"
#include "io.h"

/* LOCAL VARIABLES */

static int dim;                /* dimension of the data (integrated simultaneously) */
static int N_eval;             /* number of function evaluations in inner cycle */
static int N_tot_eval;         /* total number of function evaluation */
static int no_convergence;     /* number of inner integrals that did not converge */
static FILE *file;             /* file to print info */
/* used in inner loop */
static int size_in;            /* size of M array */
static double **M_in,          /* array of M values */
              *T_in,           /* T_m^0 */
              *dummy_in;       /* save function values */
/* used in outer loop */
static int size_out;
static double **M_out,*T_out,*dummy_out;  /* analogous to the above */
/* common arrays with frequently used values */
static double  *tv1,     /* 4^m */
               *tv2,     /* 1/(4^m-1) */
               *tv3;     /* 2*4^m-1 */
/* pointer to the function that is integrated */
static double (*func)(int theta,int phi,double *res);
static const Parms_1D *input;  /* parameters of integration */

/*============================================================*/

double Romberg1D(/* Performs integration of data */
                    const Parms_1D param,/* parameters of integration */
                    const int size,      /* size of block of data */
                    const double *data,  /* written as sequential blocks */
                    double *res)         /* where to put result */

   /* Since all values are already calculated, no adaptation is used (all data is used)
      Result is normalized on the interval width, i.e. actually averaging takes place.
      returns relative mean-square error; if function is periodic then only the first column
      of the table is used - i.e. trapezoid rule;
      This function is completely independent */
{
  int m,m0,comp,i,j,step,index,Msize;
  double abs_res,abs_err; /* norms (squared) of result and error */
  double temp;
  double **M1,*T1,*t1,*t2,*t3;  /* analogous to those used in 2D Romberg */

  /* allocate memory */
  Msize = param.periodic ? 0 : param.Jmax;
  MALLOC_DMATRIX(M1,Msize+1,size,ONE);
  MALLOC_VECTOR(T1,double,size,ONE);
    /* common to fasten calculations; needed only for really Romberg */
  if (Msize!=0) {
    MALLOC_VECTOR(t1,double,Msize+1,ONE);
    MALLOC_DVECTOR2(t2,1,Msize,ONE);
    MALLOC_VECTOR(t3,double,Msize+1,ONE);
    t1[0]=1;
    for (i=1;i<Msize;i++) {
      t1[i]=t1[i-1]*4;
      t2[i]=1/(t1[i]-1);
      t3[i-1]=2*t1[i-1]-1;
    }
  }
  /* integration */
  if (param.min==param.max) {
    memcpy(res,data,size*sizeof(double));
    abs_err=0;
    abs_res=0;  /* that is not to do unnecessary calculations */
  }
  else {
    m0=0;  /* equals 0 for periodic, m otherwise */
    for (m=0;m<param.Jmax;m++) {
      /* calculate T_0^m */
      if (m==0) {
        if (param.equival) memcpy(T1,data,size*sizeof(double));
        else {
          index=(param.Grid_size-1)*size;
          for (comp=0;comp<size;++comp) T1[comp]=0.5*(data[comp]+data[index+comp]);
        }
      }
      else {
        if (param.periodic) for (comp=0;comp<size;++comp)
          T1[comp]=0.5*(T1[comp]+M1[0][comp]);
        else {
          for (comp=0;comp<size;++comp) T1[comp]=t3[m-1]*t2[m]*(T1[comp]-M1[0][comp])+M1[0][comp];
          m0=m;
        }
      }
      /* get new integrand values (M_0^m) */
      step=(param.Grid_size-1)>>m;
      for (comp=0;comp<size;++comp) M1[m0][comp]=0;
      for (j=step>>1;j<param.Grid_size;j+=step) {
        index=j*size;
        for (comp=0;comp<size;++comp) M1[m0][comp]+=data[index+comp];
      }
      temp=pow(2,-m);
      for (comp=0;comp<size;++comp) M1[m0][comp]*=temp;
      /* generate M_1^(m-1), M_2^(m-2), ..., M_(m-1)^1, M_m^0 */
      if (m0!=0) for (i=m-1;i>=0;i--) for (comp=0;comp<size;comp++)
        M1[i][comp]=t2[m-i]*(t1[m-i]*M1[i+1][comp]-M1[i][comp]);
    }
    /* set result and error */
    abs_res=abs_err=0;
    for (comp=0;comp<size;++comp) {
      res[comp]=0.5*(M1[0][comp]+T1[comp]);
      abs_res+=res[comp]*res[comp];
      temp=0.5*fabs(M1[0][comp]-T1[comp]);
      abs_err+=temp*temp;
    }
  }
  /* free all */
  Free_dMatrix(M1,Msize+1);
  Free_general(T1);
  if (Msize!=0) {
    Free_general(t1);
    Free_dVector2(t2,1);
    Free_general(t3);
  }
  /* return */
  if (abs_res==0) return 0;
  else return (sqrt(abs_err/abs_res));
}

/*============================================================*/

static void AllocateAll(void)
   /* allocates all needed memory */
{
  int i,maxdim;

  /* inner loop vectors */
  size_in=input[PHI].periodic ? 0 : input[PHI].Jmax;
  MALLOC_DMATRIX(M_in,size_in+1,dim,ONE);
  MALLOC_VECTOR(T_in,double,dim,ONE);
  MALLOC_VECTOR(dummy_in,double,dim,ONE);
  /* outer loop vectors */
  size_out=input[THETA].periodic ? 0 : input[THETA].Jmax;
  MALLOC_DMATRIX(M_out,size_out+1,dim,ONE);
  MALLOC_VECTOR(T_out,double,dim,ONE);
  MALLOC_VECTOR(dummy_out,double,dim,ONE);
  /* common to fasten calculations; needed only for really Romberg */
  maxdim=MAX(size_in,size_out);
  if (maxdim!=0) {
    MALLOC_VECTOR(tv1,double,maxdim+1,ONE);
    MALLOC_DVECTOR2(tv2,1,maxdim,ONE);
    MALLOC_VECTOR(tv3,double,maxdim+1,ONE);
    tv1[0]=1;
    for (i=1;i<maxdim;i++) {
      tv1[i]=tv1[i-1]*4;
      tv2[i]=1/(tv1[i]-1);
      tv3[i-1]=2*tv1[i-1]-1;
    }
  }
}

/*============================================================*/

static void FreeAll(void)
   /* frees all memory */
{
  /* inner */
  Free_dMatrix(M_in,size_in+1);
  Free_general(T_in);
  Free_general(dummy_in);
  /* outer */
  Free_dMatrix(M_out,size_out+1);
  Free_general(T_out);
  Free_general(dummy_out);
  /* common */
  if (size_in!=0 || size_out!=0) {
    Free_general(tv1);
    Free_dVector2(tv2,1);
    Free_general(tv3);
  }
}

/*============================================================*/

static void RombergIterate(double **M,     /* array of M values */
                           const int m)    /* maximum order */
    /* performs one Romberg iteration; transforms previous array of M into a new one
       M_m^k=((4^m)*M_(m-1)^(k+1)-M_(m-1)^k)/(4^m-1);
       our storage implies M_(m-1-k)^k -old-> M[k] -new-> M_(m-k)^k */
{
  int k,comp;

  for (k=m-1;k>=0;k--)
    for (comp=0;comp<dim;comp++)
      M[k][comp]=tv2[m-k]*(tv1[m-k]*M[k+1][comp]-M[k][comp]);
}

/*============================================================*/

static double InnerInitT(const int fixed,double *res)
  /* Calculate term T_0^0 for the inner integration of func
     over phi_min < phi < phi_max for fixed theta = th_f  */
{
  int comp;
  double err;

  /* calculate first point */
  err=(*func)(fixed,0,res);
  N_eval++;

  if (!input[PHI].equival) {
    /* calculate last point */
    err=0.5*(err+(*func)(fixed,input[PHI].Grid_size-1,dummy_in));
    N_eval++;
    for (comp=0;comp<dim;++comp)
      res[comp] = 0.5*(dummy_in[comp]+res[comp]);
  }
  return err;
}

/*============================================================*/

static double InnerTrapzd(const int fixed,double *res,const int n)
  /* Calculate n'th refinement (term M_0^n) for the inner integration
     of func over phi_min < phi < phi_max for fixed theta = th_f  */
{
  int j,comp,step;
  double temp,err;

  step=(input[PHI].Grid_size-1)>>n;
  /* init sum */
  for (comp=0;comp<dim;++comp) res[comp]=0;
  err=0;
  /* accumulate sum */
  for (j=step>>1;j<input[PHI].Grid_size;j+=step) {
    err+=(*func)(fixed,j,dummy_in);
    N_eval++;
    for (comp=0;comp<dim;++comp) res[comp]+=dummy_in[comp];
  }
  /* scale it */
  temp=pow(2,-n);
  for (comp=0;comp<dim;++comp) res[comp]*=temp;
  return (err*temp);
}

/*============================================================*/

static double InnerRomberg(const int fixed,double *res)
  /* Integrate (average) func for fixed theta=fixed; returns estimate of the absolute error
     (for the first element); if function is periodic then only the first column of the table
     is used  - i.e. trapezoid rule */
{
  int m,m0,comp;
  double abs_res,abs_err; /* norms of result and error */
  double int_err; /* absolute error of previous layer integration */
  double err;

  if (input[PHI].min==input[PHI].max) {
    int_err=(*func)(fixed,0,res);
    N_eval++;
    return int_err;
  }
  m0=0;  /* equals 0 for periodic, m otherwise */
  for (m=0;m<input[PHI].Jmax;m++) {
    /* calculate T_0^m */
    if (m==0) int_err=InnerInitT(fixed,T_in);
    else {
      if (input[PHI].periodic) for (comp=0;comp<dim;++comp)
        T_in[comp]=0.5*(T_in[comp]+M_in[0][comp]);
      else {
        for (comp=0;comp<dim;++comp)
          T_in[comp]=tv3[m-1]*tv2[m]*(T_in[comp]-M_in[0][comp])+M_in[0][comp];
        m0=m;
      }
    }
    /* get new integrand values (M_0^m) */
    int_err=0.5*(int_err+InnerTrapzd(fixed,M_in[m0],m));
    /* generate M_1^(m-1), M_2^(m-2), ..., M_(m-1)^1, M_m^0 */
    if (m0!=0) RombergIterate(M_in,m);
    /* get error and check for convergence */
    if (m>=input[PHI].Jmin-1) {
      abs_res=0.5*fabs(M_in[0][0]+T_in[0]);
      abs_err=0.5*fabs(M_in[0][0]-T_in[0])+int_err;
      if (abs_res==0) err=0;
      else err=abs_err/abs_res;
      if (err<input[PHI].eps) break;
    }
  }
  /* set result */
  for (comp=0;comp<dim;++comp)
    res[comp]=0.5*(M_in[0][comp]+T_in[comp]);
  /* set no_convergence */
  if (err>=input[PHI].eps) {
    fprintf(file,"Inner_qromb converged only to d=%g for cosine value #%d\n",err,fixed);
    fflush(file);
    no_convergence++;
  }
  return (abs_err);
}

/*============================================================*/

static double OuterInitT(double *res)
  /* Calculate term T_0^0 for the outer integration of func;
      returns absolute error of the inner integration */
{
  int comp;
  double err;

  /* calculate first point */
  err=InnerRomberg(0,res);

  if (!input[THETA].equival) {
    /* calculate last point */
    err=0.5*(err+InnerRomberg(input[THETA].Grid_size-1,dummy_out));
    for (comp=0;comp<dim;++comp)
      res[comp] = 0.5*(dummy_out[comp]+res[comp]);
  }
  return err;
}

/*============================================================*/

static double OuterTrapzd(double *res,const int n)
  /* Calculate n'th refinement for the outer integration of func (term M_0^n);
     returns absolute error of the inner integration */
{
  int j,comp,step;
  double temp,err;

  step=(input[THETA].Grid_size-1)>>n;
  /* init sum */
  for (comp=0;comp<dim;++comp) res[comp]=0;
  err=0;
  /* accumulate sum */
  for (j=step>>1;j<input[THETA].Grid_size;j+=step) {
    err+=InnerRomberg(j,dummy_out);
    for (comp=0;comp<dim;++comp) res[comp]+=dummy_out[comp];
  }
  /* scale it */
  temp=pow(2,-n);
  for (comp=0;comp<dim;++comp) res[comp]*=temp;
  return (err*temp);
}

/*============================================================*/

static double OuterRomberg(double *res)
  /* Performs outer integration (averaging). Returns relative error of integration
     (for the first element). If function is periodic then only the first column of the table
     is used - i.e. trapezoid rule  */
{
  int m,m0,comp;
  double abs_res,abs_err; /* norms of result and error */
  double int_err; /* absolute error of previous layer integration */
  double err;

  if (input[THETA].min==input[THETA].max) {
    N_eval=0;
    int_err=InnerRomberg(0,res);
    fprintf(file,"single\t\t%d integrand-values were used.\n",N_eval);
    N_tot_eval+=N_eval;
    return ((res[0]==0) ? 0 : (int_err/fabs(res[0])));
  }
  m0=0;  /* equals 0 for periodic, m otherwise */
  for (m=0;m<input[THETA].Jmax;m++) {
    /* calculate T_0^m */
    if (m==0) {
      N_eval=0;
      int_err=OuterInitT(T_out);
      fprintf(file,"init\t\t%d integrand-values were used.\n",N_eval);
      N_tot_eval+=N_eval;
    }
    else {
      if (input[THETA].periodic) for (comp=0;comp<dim;++comp)
        T_out[comp]=0.5*(T_out[comp]+M_out[0][comp]);
      else {
        for (comp=0;comp<dim;++comp)
          T_out[comp]=tv3[m-1]*tv2[m]*(T_out[comp]-M_out[0][comp])+M_out[0][comp];
        m0=m;
      }
    }
    /* get new integrand values (M_0^m) */
    N_eval=0;
    int_err=0.5*(int_err+OuterTrapzd(M_out[m0],m));
    fprintf(file,"%d\t\t%d integrand-values were used.\n",m+1,N_eval);
    fflush(file);
    N_tot_eval+=N_eval;
    /* generate M_1^(m-1), M_2^(m-2), ..., M_(m-1)^1, M_m^0 */
    if (m0!=0) RombergIterate(M_out,m);
    /* get error and check for convergence */
    if (m>=input[THETA].Jmin-1) {
      abs_res=0.5*fabs(M_out[0][0]+T_out[0]);
      /* absolute error is sum of the errors for current integration and accumulated inner error */
      abs_err=0.5*fabs(M_out[0][0]-T_out[0])+int_err;
      if (abs_res==0) err=0;
      else err=abs_err/abs_res;
      if (err<input[THETA].eps) break;
    }
  }
  /* set result */
  for (comp=0;comp<dim;++comp)
    res[comp]=0.5*(M_out[0][comp]+T_out[comp]);

  return (err);
}

/*============================================================*/

void Romberg2D(const Parms_1D parms_input[2],
	       double (*func_input)(int theta,int phi,double *res),
	       const int dim_input,double *res,const char *fname)
/* Integrate 2D func with Romberg's method according to input's parameters.
   function func_input returns the estimate of the absolute error;
   Argument dim_input gives the number of components of (double *).
   Consistency between func and dim_input is the user's responsibilty.
   Result is normalized on the interval widths, i.e. actually averaging takes place. */
{
  double error;
  char buf1[MAX_WORD],buf2[MAX_WORD];
  const char true_s[]="true",false_s[]="false";
  const char *se1,*se2,*sp1,*sp2;

  /* initialize global values */
  dim = dim_input;
  func = func_input;
  input = parms_input;
  file=FOpenErr(fname,"w",ONE_POS);
  no_convergence = 0;
  N_tot_eval=0;

  AllocateAll();      /* allocate memory */

  if (orient_avg) {
    strcpy(buf1,"BETA");
    strcpy(buf2,"GAMMA");
  }
  else {
    strcpy(buf1,"THETA");
    strcpy(buf2,"PHI");
  }
  if (input[THETA].equival) se1=true_s;
  else se1=false_s;
  if (input[PHI].equival) se2=true_s;
  else se2=false_s;
  if (input[THETA].periodic) sp1=true_s;
  else sp1=false_s;
  if (input[PHI].periodic) sp2=true_s;
  else sp2=false_s;
  /* print info */
  fprintf(file,"                   %4s(rad)   cos(%s)\n"\
               "EPS                    %-7g   %g\n"\
               "Refinement stages:\n"\
               "Minimum                %-7d   %d\n"\
               "Maximum                %-7d   %d\n"\
               "lower boundary         %-7g   %g\n"\
               "upper boundary         %-7g   %g\n"\
               "equivalent min&max     %-7s   %s\n"\
               "periodic               %-7s   %s\n",
               buf2,buf1,
               input[PHI].eps,input[THETA].eps,
               input[PHI].Jmin,input[THETA].Jmin,
               input[PHI].Jmax,input[THETA].Jmax,
               input[PHI].min,input[THETA].min,
               input[PHI].max,input[THETA].max,
               se2,se1,sp2,sp1);
  fprintf(file,"\n\nOuter-Loop\tInner Loop\n");
  fflush(file);

  error=OuterRomberg(res);    /* main calculation */

  /* finalize log */
  if (error<input[THETA].eps) {
    if (no_convergence==0) PrintBoth(file,"All inner integrations converged\n"\
                                          "The outer integration converged\n");
    else PrintBoth(file,"%d inner integrations did not converge.\n"\
                        "The outer integration converged\n",no_convergence);
  }
  else {
    if (no_convergence==0) PrintBoth(file,"Only the outer integration did not converge \n"\
                                          "It reached d=%g\n",error);
    else PrintBoth(file,
                   "%d inner integrations did not converge.\n"\
                   "The outer integration did not converge\n"\
                   "The outer integration reached d=%g\n",no_convergence,error);
  }
  PrintBoth(file,"In total %d evaluations were used\n",N_tot_eval);
  FCloseErr(file,fname,ONE_POS);

  FreeAll();  /* free all memory */
}




