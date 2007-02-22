/* FILE: ADDAmain.c
 * AUTH: Alfons Hoekstra
 * DESCR: Main. Reads command line parameters, initialize variables, 
 *        output log.
 *
 *        sequential version, Michel Grimminck Jan 1995 
 *
 *        Currently is developed by Maxim Yurkin
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#ifdef _WIN32  /* Windows */
# include <windows.h>
#endif

#include "cmplx.h"
#include "const.h"
#include "comm.h"
#include "debug.h"
#include "crosssec.h"
#include "fft.h"

/* global variables */
extern int nDip;
extern double volume_ratio;
extern double lambda;	         /* defined in calculator.c */
extern double dpl;
extern int    nTheta;

int shape;                       /* particle shape definition */
int boxX,boxY,boxZ;              /* grid of calculation box */
int symX,symY,symZ;              /* symmetries in x y and z direction of
				    particle */
int symR;                        /* symmetry if 90 deg. rotation about z axes */
int NoSymmetry;                  /* do not use particle symmetries */
int symmetry_enforced;           /* enforce use of symmetry */
double prop_0[3],prop[3];         /* propagation vector of incident wave (initial and rotated) */
double incPolX_0[3],incPolX[3],incPolY_0[3],incPolY[3];  /* incident polariztions */
int PolRelation;                 /* type of polarization relation */
int ScatRelation;                /* type of formulae for scattering quantities */
int IntRelation;                 /* type of formula for interaction term */
int prognose;                    /* make a prognose about needed ram,virtmem */
int store_int_field;		 /* save full internal fields to file */
char directory[200];             /* directory to save data in */
FILE *logfile;
double gridspace;                /* inter-dipole distance */
double sizeX;
int maxiter;                     /* maximum number of iterations */
int jagged;                      /* particle is jagged */
int beamtype;                    /* barton,davis,lminus */
double beam_w0,beam_x0,beam_y0,beam_z0;  /* beam properties (meters) */
doublecomplex ref_index[MAXNMAT];          /* a set of refraction indexes */
int Nmat;                        /* number of different refraction indexes */
int mat_count[MAXNMAT+1];          /* number of dipoles in a material */
double coat_ratio;
double coat_x,coat_y,coat_z;
double append_ratio, ratio_x, ratio_y, ratio_z, cratio_x, cratio_y, cratio_z; /* for limphocytes particle*/
double xc0,yc0,zc0,xc1,yc1,zc1,xc2,yc2,zc2,xc3,yc3,zc3,xc4,yc4,zc4,
       ratio0, ratio1, ratio2, ratio3, ratio4, ratio5, delta, inc_ratio; /* for leucocyte2 particle */
double diskratio;
double ellipsX, ellipsY, ellipsZ; /* for ellipsoidal particle */
double aspect_r, betaY, betaZ; /* for rotatable erithrocyte, for rotatable smoothed disk and rotatable oblate spheroid */
double aspect_ratio, alphaY, alphaX; /* for stick particle */

int yzplane = true;              /* Calculate the field in the yz-plane */
int xzplane = false;		 /* Calculate the field in the xz-plane */
int all_dir = false;             /* Calculate the field for all directions on a theta-phi grid
                                     (internal parameter - initialized by other options) */
int store_all_dir = false;       /* Store the field for all directions */
int calc_Cext = false;           /* Calculate the extinction cross-section */
int calc_Cabs = false;           /* Calculate the absorption cross-section */
int calc_Csca = false;           /* Calculate the scattering cross-section by integration */
int calc_vec = false;            /* Calculate the unnormalized asymmetry-parameter */
int calc_asym = false;           /* Calculate the asymmetry-parameter */
int calc_mat_force = false;      /* Calculate the scattering force by matrix-evaluation */
int store_force = false;         /* Write radiation pressure per dipole to file */
int reduced_FFT;                 /* reduced number of storage for FFT, when matrix is symmetric */

int IterMethod;                  /* iterative method to use */
double eps;                      /* relative error to reach */

char Romb_parms[200];
char avg_parms[200];
char aggregate_file[200];
char logname[200];

char save_geom_fname[200];       /* geometry file name to save dipole configuration */

double alph_deg, bet_deg, gam_deg;  /* Euler angles of particle orientation in degrees */
int orient_avg;               /* whether to use orientation averaging*/

char avg_string[200];          /* string for output of function that reads averaging parameters */

int avg_inc_pol;              /* whether to average polarization prescription over incident polarization */
int volcor;                   /* whether to use volume correction */
int save_geom;                /* whether to save dipole configuration in .geom file */

/* Some timing variables */
clock_t Timing_TotalTime,Timing_FFT_Init,
  Timing_OneIter, Timing_OneIterCalc, Timing_OneIterComm, 
  Timing_EField, Timing_calc_EField, Timing_comm_EField,
  Timing_EField_ad, Timing_calc_EField_ad, Timing_comm_EField_ad,
  Timing_FileIO, Timing_Integration, Timing_EFieldPlane,
  Timing_IntField, Timing_ScatQuan, Timing_Init, Timing_Particle,
  Timing_IntFieldOne,Timing_InitIter,Timing_Dm_Init;

unsigned long TotalIter,TotalEval,TotalEFieldPlane;

clock_t  tstart_main;

int equivalent=true; /* whether to assume that min and max values of alpha,      */
                     /* gamma (phi) are completely equivalent */

extern void init_shape(void);
extern void Calculator(void);
extern int make_particle(void);

/*============================================================*/

int main (int argc, char *argv[])
{
  char          sbuffer[500],name[100],shapename[20];
  int		i,j,Nexp,Narg;
  FILE          *Nexpfile;
  time_t        start,end;
  char          procname[50];
  double 	temp,doubTmp; /*buffer variables*/
  int		realdips;
#ifdef _WIN32  /* Windows; for obtaining computer name */
  TCHAR cname[MAX_COMPUTERNAME_LENGTH+1];
  DWORD cname_size=MAX_COMPUTERNAME_LENGTH+1;
#endif
  time(&start);
  tstart_main = clock();

  /* initialize communications */
  init_comm(&argc,&argv);

  /* defaults */
  prop_0[0]=0;        /*by default beam propagates along z-axis*/
  prop_0[1]=0;
  prop_0[2]=1;
  directory[0]=0;
  lambda=2*PI;
    /* initialize ref_index of scatterer */
  Nmat=1;
  ref_index[0][re]=1.5;
  ref_index[0][im]=0.0;
    /* initialize to null to determine further whether it is initialized */
  logfile=NULL;
  logname[0]=0;

  boxX=boxY=boxZ=UNDEF;
  sizeX=UNDEF;
  dpl=10;
  strcpy(name,"run");
  nTheta=UNDEF;
  eps=1.0e-5;
  shape=SPHERE; strcpy(shapename,"sphere");
  store_int_field=false;
  PolRelation=LDR;
  ScatRelation=DRAINE;
  IntRelation=POINT_DIP;
  IterMethod=IT_CGNR;
  NoSymmetry=false;
  symmetry_enforced=false;
  prognose=false;
  maxiter=UNDEF;
  jagged=1;
  procname[0]=0;
  beamtype=PLANE;
  strcpy(Romb_parms,"Romb5.dat");
  strcpy(avg_parms,"avg_params.dat");
  orient_avg=false;
  alph_deg=bet_deg=gam_deg=0.0;
  volcor=true;
  realdips=UNDEF;
  reduced_FFT=true;
  save_geom=false;
  save_geom_fname[0]=0;
  /* read command line */
  i=1; if (argc>1) do {
    /* get number of arguments */
    Narg=0;
    do {
      Narg++;
      if ((i+Narg)>=argc) break;
      if (argv[i+Narg][0]=='-') break;
    } while(true);
    Narg--;

    if (strcmp(argv[i],"-lambda")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -lambda option (1 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%lf",&lambda);
    }
    else if (strcmp(argv[i],"-grid")==0) {
      if (Narg!=1 && Narg!=3) {
	printz("Illegal number of arguments (%d) to -grid option (1 or 3 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%i",&boxX);         /* boxes are further multiplied by jagged if needed */
      if (Narg==3) {
        sscanf(argv[++i],"%i",&boxY);
        sscanf(argv[++i],"%i",&boxZ);
      }
    }
    else if (strcmp(argv[i],"-m")==0) {
      if (Narg%2!=0 || Narg==0) {
	printz("Illegal number of arguments (%d) to -m option (even expected)\n",Narg);
	stop(1);
      }
      Nmat=Narg/2;
      if (Nmat>MAXNMAT) {
        printz("Too many materials (%d), maximum %d are supported.\n"\
               "Increase parameter MAXNMAT and recompile\n",Nmat,MAXNMAT);
        stop(1);
      }
      for (j=0;j<Nmat;j++) {
        sscanf(argv[++i],"%lf",&ref_index[j][re]);
        sscanf(argv[++i],"%lf",&ref_index[j][im]);
      }
    }
    else if (strcmp(argv[i],"-dpl")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -dpl option (1 expected)\n",Narg);
        stop(1);
      }
      sscanf(argv[++i],"%lf",&dpl);
    }
    else if (strcmp(argv[i],"-eps")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -eps option (1 expected)\n",Narg);
        stop(1);
      }
      sscanf(argv[++i],"%lf",&doubTmp);
      eps=pow(10,-doubTmp);
    }
/*    else if (strcmp(argv[i],"-cylinder")==0) {
      if (Narg!=2) {
        printz("Illegal number of arguments (%d) to -cylinder option\n",Narg);
        stop(1);
      }
      shape=CYLINDER; strcpy(shapename,"cylinder");
      sscanf(argv[++i],"%lf",&aspect_ratio);
      sscanf(argv[++i],"%lf",&append_ratio);
    }  */
/*    else if (strcmp(argv[i],"-lymphocyte1")==0) {
      if (Narg!=5) {
        printz("Illegal number of arguments (%d) to -lymphocyte1 option\n",Narg);
        stop(1);
      }
      shape=LYMPHOCYTE1; strcpy(shapename,"lymphocyte1");
      coat_x=0.0; coat_y=0.0; coat_z=0.0;
      sscanf(argv[++i],"%lf",&coat_ratio);
      sscanf(argv[++i],"%lf",&coat_x);
      sscanf(argv[++i],"%lf",&coat_y);
      sscanf(argv[++i],"%lf",&coat_z);
      sscanf(argv[++i],"%lf",&append_ratio);
    } */
/*    else if (strcmp(argv[i],"-lymphocyte2")==0) {
      if (Narg!=11) {
        printz("Illegal number of arguments (%d) to -lymphocyte2 option\n",Narg);
        stop(1);
      }
      shape=LYMPHOCYTE2; strcpy(shapename,"lymphocyte2");
      sscanf(argv[++i],"%lf",&coat_ratio);
      sscanf(argv[++i],"%lf",&coat_x);
      sscanf(argv[++i],"%lf",&coat_y);
      sscanf(argv[++i],"%lf",&coat_z);
      sscanf(argv[++i],"%lf",&append_ratio);
      sscanf(argv[++i],"%lf",&ratio_x);
      sscanf(argv[++i],"%lf",&ratio_y);
      sscanf(argv[++i],"%lf",&ratio_z);
      sscanf(argv[++i],"%lf",&cratio_x);
      sscanf(argv[++i],"%lf",&cratio_y);
      sscanf(argv[++i],"%lf",&cratio_z);
    } */
/*    else if (strcmp(argv[i],"-leucocyte2")==0) {
      if (Narg!=22) {
        printz("Illegal number of arguments (%d) to -leucocyte option\n",Narg);
        stop(1);
      }
      shape=LEUCOCYTE2; strcpy(shapename,"leucocyte2");
      sscanf(argv[++i],"%lf",&ratio0);
      sscanf(argv[++i],"%lf",&xc0);
      sscanf(argv[++i],"%lf",&yc0);
      sscanf(argv[++i],"%lf",&zc0);
      sscanf(argv[++i],"%lf",&ratio1);
      sscanf(argv[++i],"%lf",&xc1);
      sscanf(argv[++i],"%lf",&yc1);
      sscanf(argv[++i],"%lf",&zc1);
      sscanf(argv[++i],"%lf",&ratio2);
      sscanf(argv[++i],"%lf",&xc2);
      sscanf(argv[++i],"%lf",&yc2);
      sscanf(argv[++i],"%lf",&zc2);
      sscanf(argv[++i],"%lf",&ratio3);
      sscanf(argv[++i],"%lf",&xc3);
      sscanf(argv[++i],"%lf",&yc3);
      sscanf(argv[++i],"%lf",&zc3);
      sscanf(argv[++i],"%lf",&ratio4);
      sscanf(argv[++i],"%lf",&xc4);
      sscanf(argv[++i],"%lf",&yc4);
      sscanf(argv[++i],"%lf",&zc4);
      sscanf(argv[++i],"%lf",&inc_ratio);
      sscanf(argv[++i],"%lf",&delta);
    } */
/*    else if (strcmp(argv[i],"-rbc_rot")==0) {
      if (Narg!=3) {
        printz("Illegal number of arguments (%d) to -rbc_rot option\n",Narg);
        stop(1);
      }
      shape=RBC_ROT; strcpy(shapename,"rbc_rot");
      sscanf(argv[++i],"%lf",&aspect_r);
      sscanf(argv[++i],"%lf",&betaY);
      sscanf(argv[++i],"%lf",&betaZ);
    }  */
/*    else if (strcmp(argv[i],"-sdisk_rot")==0) {
      if (Narg!=3) {
        printz("Illegal number of arguments (%d) to -sdisk_rot option\n",Narg);
        stop(1);
      }
      shape=SDISK_ROT; strcpy(shapename,"sdisk_rot");
      sscanf(argv[++i],"%lf",&aspect_r);
      sscanf(argv[++i],"%lf",&betaY);
      sscanf(argv[++i],"%lf",&betaZ);
    }  */
/*    else if (strcmp(argv[i],"-spheroid_rot")==0) {
      if (Narg!=3) {
        printz("Illegal number of arguments (%d) to -spheroid_rot option\n",Narg);
        stop(1);
      }
      shape=SPHEROID_ROT; strcpy(shapename,"spheroid_rot");
      sscanf(argv[++i],"%lf",&aspect_r);
      sscanf(argv[++i],"%lf",&betaY);
      sscanf(argv[++i],"%lf",&betaZ);
    } */
/*    else if (strcmp(argv[i],"-stick")==0) {
      if (Narg!=4) {
        printz("Illegal number of arguments (%d) to -stick option\n",Narg);
	stop(1);
      }
      shape=STICK; strcpy(shapename,"stick");
      sscanf(argv[++i],"%lf",&aspect_ratio);
      sscanf(argv[++i],"%lf",&alphaY);
      sscanf(argv[++i],"%lf",&alphaX);
      sscanf(argv[++i],"%lf",&append_ratio);
    } */
    else if (strcmp(argv[i],"-size")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -size option (1 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%lf",&sizeX);
      dpl=UNDEF;
    }
    else if (strcmp(argv[i],"-test")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -test option (0 expected)\n",Narg);
	stop(1);
      }
      strcpy(name,"test");
    }
    else if (strcmp(argv[i],"-ntheta")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -ntheta option (1 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%i",&nTheta);
      nTheta++;
    }
    else if (strcmp(argv[i],"-maxiter")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -maxiter option (1 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%i",&maxiter);
    }
    else if (strcmp(argv[i],"-spherebox")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -spherebox option (1 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%lf",&coat_ratio);
      shape=SPHEREBOX;
    }
    else if (strcmp(argv[i],"-coated")==0) {
      if (Narg!=1 && Narg!=4) {
        printz("Illegal number of arguments (%d) to -coated option (1 or 4 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%lf",&coat_ratio);
      shape=COATED; strcpy(shapename,"coat");
      if (Narg==4) {
	sscanf(argv[++i],"%lf",&coat_x);
	sscanf(argv[++i],"%lf",&coat_y);
	sscanf(argv[++i],"%lf",&coat_z);
      }
      else coat_x=coat_y=coat_z=0.0;
    }
    else if (strcmp(argv[i],"-ellipsoidal")==0) {
      if (Narg!=3) {
        printz("Illegal number of arguments (%d) to -ellipsoidal option (3 expected)\n",Narg);
	stop(1);
      }
      sscanf(argv[++i],"%lf",&ellipsX);
      sscanf(argv[++i],"%lf",&ellipsY);
      sscanf(argv[++i],"%lf",&ellipsZ);
      ellipsY/=ellipsX;
      ellipsZ/=ellipsX;
      ellipsX=1;
      shape=ELLIPSOIDAL;
      strcpy(shapename,"ellipsoidal");
    }
    else if (strcmp(argv[i],"-sphere")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -sphere option (0 expected)\n",Narg);
	stop(1);
      }
      shape=SPHERE;
      strcpy(shapename,"sphere");
    }
    else if (strcmp(argv[i],"-box")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -box option (0 expected)\n",Narg);
	stop(1);
      }
      shape=BOX;
      strcpy(shapename,"box");
    }
    else if (strcmp(argv[i],"-disk")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -disk option (1 expected)\n",Narg);
        stop(1);
      }
      shape=DISK;
      strcpy(shapename, "disk");
      sscanf(argv[++i],"%lf",&diskratio);
    }
/*    else if (strcmp(argv[i],"-rbc")==0) {
      shape=RBC;
      strcpy(shapename, "rbc");
    } */
    else if (strcmp(argv[i],"-read")==0) {
      shape=READ; strcpy(shapename,"read");
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -read option (1 expected)\n",Narg);
        stop(1);
      }
      strcpy(aggregate_file,argv[++i]);
    }
    else if (strcmp(argv[i],"-dir")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -dir option (1 expected)\n",Narg);
        stop(1);
      }
      strcpy(directory,argv[++i]);
    }
    else if (strcmp(argv[i],"-xz")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -xzplane option (0 expected)\n",Narg);
        stop(1);
      }
      xzplane = true;
    }
    else if (strcmp(argv[i],"-Romb_inp")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -Romb_inp option (1 expected)\n",Narg);
        stop(1);
      }
      strcpy(Romb_parms,argv[++i]);
    }
    else if (strcmp(argv[i],"-store_all_dir")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -store_all_dir option (0 expected)\n",Narg);
        stop(1);
      }
      store_all_dir = true;
    }
    else if (strcmp(argv[i],"-Cext")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -Cext option (0 expected)\n",Narg);
        stop(1);
      }
      calc_Cext = true;
    }
    else if (strcmp(argv[i],"-Cabs")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -Cabs option (0 expected)\n",Narg);
        stop(1);
      }
      calc_Cabs = true;
    }
    else if (strcmp(argv[i],"-Csca")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -Csca option (0 expected)\n",Narg);
        stop(1);
      }
      calc_Csca = true;
    }
    else if (strcmp(argv[i],"-vec")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -vec option (0 expected)\n",Narg);
        stop(1);
      }
      calc_vec = true;
    }
    else if (strcmp(argv[i],"-asym")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -asym option (0 expected)\n",Narg);
        stop(1);
      }
      calc_asym = true;
      calc_vec = true;
      calc_Csca = true;
    }
    else if (strcmp(argv[i],"-Cpr_mat")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -Cpr_mat option (0 expected)\n",Narg);
        stop(1);
      }
      calc_mat_force = true;
    }
    else if (strcmp(argv[i],"-store_force")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -store_force option (0 expected)\n",Narg);
        stop(1);
      }
      store_force = true;
    }
/*    else if (strcmp(argv[i],"-prisma")==0) {
      shape=PRISMA;
      strcpy(shapename,"prisma");
    }  */
    else if (strcmp(argv[i],"-line")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -line option (0 expected)\n",Narg);
        stop(1);
      }
      shape=LINE;
      strcpy(shapename,"line");
    }
    else if (strcmp(argv[i],"-store_int_field")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -store_int_field option (0 expected)\n",Narg);
        stop(1);
      }
      store_int_field=true;
    }
    else if (strcmp(argv[i],"-prognose")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -prognose option (0 expected)\n",Narg);
        stop(1);
      }
      prognose=true;
    }
    else if (strcmp(argv[i],"-nosym")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -nosym option (0 expected)\n",Narg);
        stop(1);
      }
      NoSymmetry=true;
    }
    else if (strcmp(argv[i],"-sym_enf")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -sym_enf option (0 expected)\n",Narg);
        stop(1);
      }
      symmetry_enforced=true;
    }
    else if (strcmp(argv[i],"-prop")==0) {
      if (Narg!=3) {
        printz("Illegal number of arguments (%d) to -prop option (3 expected)\n",Narg);
        stop(1);
      }
      sscanf(argv[++i],"%lf",prop_0);
      sscanf(argv[++i],"%lf",&prop_0[1]);
      sscanf(argv[++i],"%lf",&prop_0[2]);
      doubTmp=DotProd(prop_0,prop_0);
      if (doubTmp==0) {
        printz("Given propagation vector is null\n");
        stop(1);
      }
      doubTmp=1/sqrt(doubTmp);
      prop_0[0]*=doubTmp;
      prop_0[1]*=doubTmp;
      prop_0[2]*=doubTmp;
    }
    else if (strcmp(argv[i],"-beam")==0) {
      if (Narg==0) {
        printz("No arguments are given to -beam option (at least 1 expected)\n");
        stop(1);
      }
      i++;
      if (strcmp(argv[i],"plane")==0) {
        beamtype=PLANE;
        if (Narg!=1) {
          printz("Illegal number of arguments (%d) to '-beam plane' option (0 expected)\n",Narg-1);
          stop(1);
        }
      }
      else {
	if (strcmp(argv[i],"lminus")==0) beamtype=LMINUS;
	else if (strcmp(argv[i],"buggy")==0) beamtype=BUGGY;
	else if (strcmp(argv[i],"davis1")==0) beamtype=DAVIS1;
	else if (strcmp(argv[i],"davis3")==0) beamtype=DAVIS3;
	else if (strcmp(argv[i],"barton1")==0) beamtype=BARTON1;
	else if (strcmp(argv[i],"barton3")==0) beamtype=BARTON3;
	else if (strcmp(argv[i],"barton5")==0) beamtype=BARTON5;
	else {
	  printz("Beam type '%s' is not supported\n",argv[i]);
          stop(1);
        }
        if (Narg!=5) {
          printz("Illegal number of arguments (%d) to '-beam %s' option (4 expected)\n",Narg-1,argv[i]);
          stop(1);
        }
	sscanf(argv[++i],"%lf",&beam_w0);
	sscanf(argv[++i],"%lf",&beam_x0);
	sscanf(argv[++i],"%lf",&beam_y0);
	sscanf(argv[++i],"%lf",&beam_z0);
      }
    }
    else if (strcmp(argv[i],"-pol")==0) {
      if (Narg!=1 && Narg!=2) {
        printz("Illegal number of arguments (%d) to -pol option (1 or 2 expected)\n",Narg);
        stop(1);
      }
      i++;
      if (strcmp(argv[i],"cm")==0) PolRelation=CM;
      else if (strcmp(argv[i],"rrc")==0) PolRelation=RADCOR;
      else if (strcmp(argv[i],"ldr")==0) PolRelation=LDR;
      else if (strcmp(argv[i],"cldr")==0) PolRelation=CLDR;
      else if (strcmp(argv[i],"so")==0) PolRelation=SOrd;
      else {
        printz("Polarization Relation '%s' is not supported\n",argv[i]);
        stop(1);
      }
      if (Narg==2) {
        i++;
        if (strcmp(argv[i],"avgpol")==0) avg_inc_pol=true;
        else {
          printz("Unknown argument '%s' to '-pol %s' option\n",argv[i],argv[i-1]);
          stop(1);
        }
      }
    }
    else if (strcmp(argv[i],"-scat")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -scat option (1 expected)\n",Narg);
        stop(1);
      }
      i++;
      if (strcmp(argv[i],"dr")==0) ScatRelation=DRAINE;
      else if (strcmp(argv[i],"so")==0) ScatRelation=SOrd;
      else {
        printz("Scattering Quantities Relation '%s' is not supported\n",argv[i]);
        stop(1);
      }
    }
    else if (strcmp(argv[i],"-int")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -int option (1 expected)\n",Narg);
        stop(1);
      }
      i++;
      if (strcmp(argv[i],"poi")==0) IntRelation=POINT_DIP;
      else if (strcmp(argv[i],"so")==0) IntRelation=SOrd;
      else {
        printz("Interaction term prescription '%s' is not supported\n",argv[i]);
        stop(1);
      }
    }
    else if (strcmp(argv[i],"-orient")==0) {
      if (Narg==0) {
        printz("No arguments are given to -orient option (at least 1 expected)\n",Narg);
        stop(1);
      }
      i++;
      if (strcmp(argv[i],"avg")==0) {
        if (Narg>2) {
          printz("Illegal number of arguments (%d) to '-orient avg' option (0 or 1 expected)\n",Narg-1);
          stop(1);
        }
        orient_avg=true;
        if (Narg==2) strcpy(avg_parms,argv[++i]);
      }
      else {
        if (Narg!=3) {
          printz("Illegal number of numerical arguments (%d) to -orient option\n"\
                 "  or first argument (%s) is incorrect (can be 'avg')\n",Narg,argv[i]);
          stop(1);
        }
        sscanf(argv[i],"%lf",&alph_deg);
	sscanf(argv[++i],"%lf",&bet_deg);
	sscanf(argv[++i],"%lf",&gam_deg);
      }
    }
    else if (strcmp(argv[i],"-iter")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -iter option (1 expected)\n",Narg);
        stop(1);
      }
      i++;
      if (strcmp(argv[i],"cgnr")==0) IterMethod=IT_CGNR;
      else if (strcmp(argv[i],"bicgstab")==0) IterMethod=IT_BICGSTAB;
      else if (strcmp(argv[i],"bicg")==0) IterMethod=IT_BICG_CS;
      else if (strcmp(argv[i],"qmr")==0) IterMethod=IT_QMR_CS;
      else {
        printz("Iterative method '%s' is not supported\n",argv[i]);
        stop(1);
      }
    }
    else if (strcmp(argv[i],"-jagged")==0) {
      if (Narg!=1) {
        printz("Illegal number of arguments (%d) to -jagged option (1 expected)\n",Narg);
        stop(1);
      }
      sscanf(argv[++i],"%d",&jagged);
    }
    else if (strcmp(argv[i],"-no_vol_cor")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -no_vol_cor option (0 expected)\n",Narg);
        stop(1);
      }
      volcor=false;
    }
    else if (strcmp(argv[i],"-no_reduced_fft")==0) {
      if (Narg!=0) {
        printz("Illegal number of arguments (%d) to -no_reduced_FFT option (0 expected)\n",Narg);
        stop(1);
      }
      reduced_FFT=false;
    }
    else if (strcmp(argv[i],"-save_geom")==0) {
      if (Narg>1) {
        printz("Illegal number of arguments (%d) to -save_geom option (0 or 1 expected)\n",Narg);
        stop(1);
      }
      save_geom=true;
      if (Narg==1) sscanf(argv[++i],"%s",save_geom_fname);
    }
    else {
      printz("adda: illegal option \"%s\"\n"\
             "Usage: adda [-lambda %%f] [-grid %%i [%%i %%i]] [-m %%f %%f [...]]\n"\
             "    [-dpl %%f] [-size %%f] [-test] [-ntheta %%i] [-maxiter %%i] [-dir %%s] \n"\
             "    [-xz] [-Romb_inp %%s] [-store_all_dir] [-Cext] [-Cabs]\n"\
             "    [-Csca] [-vec] [-asym] [-Cpr_mat] [-store_force] [-jagged %%d]\n"\
             "    [-store_int_field] [-prognose] [-prop %%f %%f %%f] [-nosym] [-sym_enf] [-eps %%f]\n"\
             "    [-cylinder|-lymphocyte1|-lymphocyte2|-leucocyte2|-rbc_rot|-sdisk_rot|\n"\
             "     -spheroid_rot|-stick|-spherebox|-coated|-ellipsoidal|-sphere|-box|\n"\
             "     -disk|-rbc|-aggregate|-prisma|-line[...]|-read filename] \n"\
             "    [-beam plane|lminus|buggy|davis1|davis3|barton1|barton3|barton5 [...]]\n"\
             "    [-pol cm|rrc|ldr|cldr|so] [-scat dr|so] [-int poi|so] [-orient avg[ %%s]|%%f %%f %%f]\n"\
             "    [-iter cgnr|bicgstab|bicg|qmr]\n"\
             "    [-no_vol_cor] [-no_reduced_fft] [-save_geom [%%s]]\n"\
             "    \n",argv[i]);
      stop(1);
    }
    i++;
  } while(i<argc);  /* end of reading command line arguments */

  D("finished reading command line");
  /* parameter interconnections */
  if (prop_0[2]!=1 && orient_avg) {
    printz("-prop and '-orient avg' can not be used together\n");
    stop(1);
  }
  if (IntRelation==SOrd) reduced_FFT=false;
  /* scale boxes by jagged */
  if (jagged!=1) {
    if (boxX!=UNDEF) boxX*=jagged;
    if (boxY!=UNDEF) boxY*=jagged;
    if (boxZ!=UNDEF) boxZ*=jagged;
  }
  if (store_all_dir || calc_Csca || calc_vec) all_dir = true;

  /*determine two incident polarizations. Equivalent to rotation of X,Y,Z basis by
  angles Theta and Phi from (0,0,1) to given propagation vector */
  if (abs(prop_0[2])==1) {
    incPolX_0[0]=prop_0[2];
    incPolY_0[1]=1;
    incPolX_0[1]=incPolX_0[2]=incPolY_0[0]=incPolY_0[2]=0.0;
  }
  else {
    temp=sqrt(1-prop_0[2]*prop_0[2]);
    incPolX_0[0]=prop_0[0]*prop_0[2]/temp;
    incPolX_0[1]=prop_0[1]*prop_0[2]/temp;
    incPolX_0[2]=-temp;
    incPolY_0[0]=-prop_0[1]/temp;
    incPolY_0[1]=prop_0[0]/temp;
    incPolY_0[2]=0.0;
  }
  /* initialize averaging over orientation */
  if (orient_avg) {
    read_avg_parms(avg_parms);
    NoSymmetry=true;
    avg_inc_pol=true;
  }
  else {
    /* else - initialize rotation stuff */
    init_rotation();
    if (prop[2]!=1) NoSymmetry=true;
  }
  /* get number of dipoles */
  init_shape();          /* initialize symmetries and box's */
  nDip=boxX*boxY*boxZ;
  /* initialize FFT grid and its subdivision over processors */
  par_setup();
  /* make_particle; initialize dpl and nlocalRows */
  make_particle();
  D("finished make_particle");
  /* initialize maxiter; not very realistic */
  if (maxiter==UNDEF) maxiter=3*nDip;
  /* initialize nTheta */
  if (nTheta==UNDEF) {
    if (boxX*boxY*boxZ<1000) nTheta=91;
    else if (boxX*boxY*boxZ<10000) nTheta=181;
    else if (boxX*boxY*boxZ<100000) nTheta=361;
    else nTheta=721;
  }
  /* devise directory name (for output files) */
  if (prognose==false && directory[0]==0) {
    if ((Nexpfile=fopen("ExpCount","r"))!=NULL) {
      fscanf(Nexpfile,"%i",&Nexp);
      fclose(Nexpfile);
    }
    else Nexp=0;
    /* wait for all processors to finish reading Nexpfile */
    synchronize();
    /* put new number in Nexpfile */
    if (ringid==ROOT) {
      if ((Nexpfile=fopen("ExpCount","w"))==NULL)
        LogError(EC_ERROR,ONE,POSIT,"Failed to open file 'ExpCount'");
      fprintz(Nexpfile,"%i",Nexp+1);
      fclose(Nexpfile);
    }
    /* create directory name and save_geom file*/
    sprintf(directory,"%s%03i_%sg%id%.1f",name,Nexp,shapename,boxX,dpl);
    if (save_geom && save_geom_fname[0]==0)
      sprintf(save_geom_fname,"%s/%s.geom",directory,shapename);
  }
  else if (directory[0]==0 || prognose==true) sprintf(directory,"/tmp");
  /* make logname; do it for all processors to enable additional logging in LogError */
  strcpy(logname,directory);
  strcat(logname,"/log");

  if (ringid==ROOT) {
    /* print basic parameters */
    printf("all data is saved in '%s'\n",directory);
    printf("lambda:%.8g      m:%.8g%+.8gi     Dipoles/lambda:%.8g\n",lambda,ref_index[0][re],ref_index[0][im],dpl);
    printf("Required relative error:%.8g\n",eps);
    printf("Total number of occupied dipoles %d\n",nvoid_Ndip);
    /* make new directory */
    strcpy(sbuffer,"mkdir ");
    strcat(sbuffer,directory);
    if (prognose==false) system(sbuffer);
#if defined(PARALLEL) || defined(_WIN32)
    /* open logfille (write)*/
    if ((logfile=fopen(logname,"w"))==NULL)
      LogError(EC_ERROR,ONE,POSIT,"Failed to open file 'logfile'");
# ifdef PARALLEL
    /* write number of processors */
    fprintf(logfile,"The program was run on: %d processors\n",nprocs);
# else /* Windows, sequential */
    /* write computer name */
    GetComputerName(cname,&cname_size);
    fprintf(logfile,"The program was run on: %s\n",cname);
# endif
#else /* UNIX, sequential */
    /* put computer name into logfile */
    strcpy(sbuffer,"echo 'The program was run on:' $HOST >>");
    strcat(sbuffer,logname);
    system(sbuffer);
    /* open logfille (append)*/
    if ((logfile=fopen(logname,"a"))==NULL)
      LogError(EC_ERROR,ONE,POSIT,"Failed to open file 'logfile'");
#endif
    /* log command line */
    fprintf(logfile,"command: '");
    for(i=0;i<argc;i++) fprintf(logfile,"%s ",argv[i]);
    fprintf(logfile,"'\n");
    /* log basic parameters */
    fprintf(logfile,"lambda:%.8g\n",lambda);
    fprintf(logfile,"shape: '%s', size:%ix%ix%i\n",shapename,boxX,boxY,boxZ);
    fprintf(logfile,"m0:%.8g%+.8gi  m1:%.8g%+.8gi\n",ref_index[0][re],ref_index[0][im],ref_index[1][re],ref_index[1][im]);
    fprintf(logfile,"Dipoles/lambda:%.8g\n",dpl);
    if (realdips!=UNDEF) fprintf(logfile,"      (Volume correction used)\n");
    fprintf(logfile,"Required relative error:%.8g\n",eps);
    fprintf(logfile,"Total number of occupied dipoles %d\n",nvoid_Ndip);
    /* log incident polarization */
    fprintf(logfile,"\nIncident propagation vector:(%.8g,%.8g,%.8g)\n",prop_0[0],prop_0[1],prop_0[2]);
    fprintf(logfile,"Incident polarization Y(par):(%.8g,%.8g,%.8g)\n",incPolY_0[0],incPolY_0[1],incPolY_0[2]);
    fprintf(logfile,"Incident polarization X(per):(%.8g,%.8g,%.8g)\n\n",incPolX_0[0],incPolX_0[1],incPolX_0[2]);
    /* log particle orientation */
    if (orient_avg) fprintf(logfile,"Particle orientation - averaged\n%s\n",avg_string);
    else fprintf(logfile,"Particle orientation (deg): alpha=%g, beta=%g, gamma=%g\n\n",alph_deg,bet_deg,gam_deg);
    /* log incident polarization after transformation */
    if (alph_deg!=0 || bet_deg!=0 || gam_deg!=0) {
      fprintf(logfile,"After transformation to particle reference frame:\n");
      fprintf(logfile,"New incident propagation vector:(%.8g,%.8g,%.8g)\n",prop[0],prop[1],prop[2]);
      fprintf(logfile,"New incident polarization Y(par):(%.8g,%.8g,%.8g)\n",incPolY[0],incPolY[1],incPolY[2]);
      fprintf(logfile,"New incident polarization X(per):(%.8g,%.8g,%.8g)\n\n",incPolX[0],incPolX[1],incPolX[2]);
    }
    /* log Polarization relation */
    if (PolRelation==CM) fprintf(logfile,"Polarization relation: 'Clausius-Mossotti'\n");
    else if (PolRelation==RADCOR) fprintf(logfile,"Polarization relation: 'Radiative Reaction Correction'\n");
    else if (PolRelation==LDR) {
      fprintf(logfile,"Polarization relation: 'Lattice Dispersion Relation'");
      if (avg_inc_pol) fprintf(logfile," (averaged over incident polarization)");
      fprintf(logfile,"\n");
    }
    else if (PolRelation==CLDR) fprintf(logfile,"Polarization relation: 'Corrected Lattice Dispersion Relation'\n");
    else if (PolRelation==SOrd) fprintf(logfile,"Polarization relation: 'Second Order'\n");
    /* log Scattering Quantities formulae */
    if (ScatRelation==DRAINE) fprintf(logfile,"Scattering quantities formulae: 'by Draine'\n");
    else if (ScatRelation==SOrd) fprintf(logfile,"Scattering quantities formulae: 'Second Order'\n");
    /* log Interaction term prescription */
    if (IntRelation==POINT_DIP) fprintf(logfile,"Interaction term prescription: 'as Point dipoles'\n");
    else if (IntRelation==SOrd) fprintf(logfile,"Interaction term prescription: 'Second Order'\n");
    /* log FFT method */
#ifdef FFTW3
    fprintf(logfile,"FFT algorithm: FFTW3\n");
#else
# ifdef TEMPERTON
    fprintf(logfile,"FFT algorithm: by C.Temperton\n");
# endif
#endif
    /* log Iterative Method */
    if (IterMethod==IT_CGNR) fprintf(logfile,"Iterative Method: CGNR\n");
    else if (IterMethod==IT_BICGSTAB) fprintf(logfile,"Iterative Method: Bi-CG Stabilized\n");
    else if (IterMethod==IT_BICG_CS) fprintf(logfile,"Iterative Method: Bi-CG (complex symmetric)\n");
    else if (IterMethod==IT_QMR_CS) fprintf(logfile,"Iterative Method: QMR (complex symmetric)\n");
    /* log Symmetry options */
    if (symmetry_enforced) fprintf(logfile,"Symmetry is enforced by user (warning!)\n");
    else if (NoSymmetry) fprintf(logfile,"No symmetries are used\n");
  }
  /* initialize times and counters */
  TotalIter=TotalEval=TotalEFieldPlane=0;
  Timing_EField=Timing_FileIO=Timing_IntField=Timing_ScatQuan=0;
  Timing_Integration=0;
  /* Main calculation part */
  D("calculator started");
  Calculator();
  /* print final output and statistics */
  if (ringid==ROOT) {
    /* last time measurements */
    Timing_TotalTime = clock() - tstart_main;
    time(&end);
    /* log statistics */
    fprintf(logfile,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"\
                    "                Timing Results             \n"\
                    "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    if (orient_avg) fprintf(logfile,"Total number of single particle evaluations: %d\n",TotalEval);
    fprintf(logfile,
            "Total number of iterations: %d\n"\
            "Total planes of E field calculation (each %d points): %d\n\n"\
            "total time:          %4.4f\n"\
            "Wall time:           %.1f\n"\
            "Initialization time:   %4.4f\n"\
            "  init Dmatrix           %4.4f\n"\
            "  FFT setup:             %4.4f\n"\
            "  make particle:         %4.4f\n"\
            "Internal fields:       %4.4f\n"\
            "  one solution:          %4.4f\n"\
            "    init solver:           %4.4f\n"\
            "    one iteration:         %4.4f\n"\
            "      calculation:           %4.4f\n"\
            "      communication:         %4.4f\n"\
            "E field calculation:   %4.4f\n"\
            "  one plane:             %4.4f\n"\
            "    calculation:           %4.4f\n"\
            "    communication:         %4.4f\n"\
            "  one alldir:            %4.4f\n"\
            "    calculation:           %4.4f\n"\
            "    communication:         %4.4f\n"\
            "Other scat.quantities: %4.4f\n"\
            "file io:               %4.4f\n"\
            "Integration:           %4.4f\n",
            TotalIter,nTheta,TotalEFieldPlane,
            TO_SEC(Timing_TotalTime),difftime(end,start),
            TO_SEC(Timing_Init),TO_SEC(Timing_Dm_Init),TO_SEC(Timing_FFT_Init),TO_SEC(Timing_Particle),
            TO_SEC(Timing_IntField),TO_SEC(Timing_IntFieldOne),TO_SEC(Timing_InitIter),
            TO_SEC(Timing_OneIter),TO_SEC(Timing_OneIterCalc),TO_SEC(Timing_OneIterComm),
            TO_SEC(Timing_EField),
            TO_SEC(Timing_EFieldPlane),TO_SEC(Timing_calc_EField),TO_SEC(Timing_comm_EField),
            TO_SEC(Timing_EField_ad),TO_SEC(Timing_calc_EField_ad),TO_SEC(Timing_comm_EField_ad),
            TO_SEC(Timing_ScatQuan),TO_SEC(Timing_FileIO),TO_SEC(Timing_Integration));
    /* close logfile */
    fclose(logfile);
  }
  /* wait for all processes to exit simultaneously */
  synchronize();
  /* finish execution, normally */
  stop(0);
  /* never actually reached */
  return 0;
}

