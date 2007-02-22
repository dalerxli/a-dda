/* FILE: make_particlce.c
 * AUTH: Alfons Hoekstra
 * DESCR: This module initializes the dipole set, either using predefined shapes 
 *        or reding from a file.
 *
 *        rewriten,
 *        Michel Grimminck 1995
 *        -----------------------------------------------------------
 *        included ellipsoidal particle for work with Victor Babenko
 *        september 2002
 *        --------------------------------------------------------
 *        included many more new particles:
 *        leucocyte, stick, rotatable oblate spheroid, lymphocyte,
 *        rotatable RBC, etc etc etc
 *        December 2003 - February 2004, by Konstantin Semyanov
 *          (not used now)
 *
 *        Currently is developed by Maxim Yurkin
 *
 * Copyright (C) 2006 M.A. Yurkin and A.G. Hoekstra
 * This code is covered by the GNU General Public License.
 */
#include <stdlib.h>
#include <time.h>
#include "vars.h"
#include "const.h"
#include "cmplx.h"
#include "types.h"
#include "comm.h"
#include "debug.h"
#include "memory.h"
#include "io.h"
#include "param.h"

/* SEMI-GLOBAL VARIABLES */

/* defined and initialized in param.c */
extern const int shape,sh_Npars;
extern const double sh_pars[];
extern const int NoSymmetry,symmetry_enforced;
extern const double lambda;
extern double sizeX,dpl;
extern const int jagged;
extern const char aggregate_file[],shapename[];
extern char save_geom_fname[];
extern const int volcor,save_geom;
extern opt_index opt_sh;

/* defined and initialized in timing.c */
extern clock_t Timing_Particle;

/* used in param.c */
int volcor_used;                 /* volume correction was actually employed */
char sh_form_str[MAX_PARAGRAPH]; /* string for log file with shape parameters */

/* LOCAL VARIABLES */

static const char geom_format[] = "%d %d %d\n";         /* format of the geom file */
static const char geom_format_ext[] = "%d %d %d %d\n";  /* extended format of the geom file */
static double volume_ratio; /* ratio of scatterer volume to enclosing cube;
                               used for dpl correction */
static int Ndip;            /* total number of dipoles (in a circumscribing cube) */
/* shape parameters */
static double coat_ratio,coat_x,coat_y,coat_z,coat_r2;
static double diskratio,ellipsY,ellipsZ;
static double P,Q,R,S;         /* for RBC */
/* temporary arrays before their real counterparts are allocated */
static unsigned char *material_tmp;
static double *DipoleCoord_tmp;
static short int *position_tmp;

/*============================================================*/

static void SaveGeometry(void)
  /* saves dipole configuration to .geom file */
{
  char fname[MAX_FNAME];
  FILE *geom;
  int i,j;

  /* create save_geom_fname if not specified */
  if (save_geom_fname[0]==0)
    sprintf(save_geom_fname,"%s.geom",shapename);
  /* choose filename */
#ifdef PARALLEL
  sprintf(fname,"%s/" F_GEOM_TMP,directory,ringid);
#else
  sprintf(fname,"%s/%s",directory,save_geom_fname);
#endif
  geom=FOpenErr(fname,"w",ALL_POS);
  /* print head of file */
#ifdef PARALLEL
  if (ringid==0) {  /* this condition can be different from being ROOT */
#endif
    fprintf(geom,"#generated by ADDA v." ADDA_VERSION "\n"\
                 "#shape: '%s'\n"\
                 "#box size: %dx%dx%d\n",
                 shapename,boxX,boxY,boxZ);
    if (Nmat>1) fprintf(geom,"Nmat=%d\n",Nmat);
#ifdef PARALLEL
  }     /* end of if */
#endif
  /* save geometry */
  if (Nmat>1) for(i=0;i<local_nvoid_Ndip;i++) {
    j=3*i;
    fprintf(geom,geom_format_ext,position[j],position[j+1],position[j+2],material[i]+1);
  }
  else for(i=0;i<local_nvoid_Ndip;i++) {
    j=3*i;
    fprintf(geom,geom_format,position[j],position[j+1],position[j+2]);
  }
  FCloseErr(geom,fname,ALL_POS);
#ifdef PARALLEL
  /* wait for all processes to save their part of geometry */
  Synchronize();
  /* combine all files into one and clean */
  if (ringid==ROOT) CatNFiles(directory,F_GEOM_TMP,save_geom_fname);
#endif
  PRINTZ("Geometry saved to file\n");
}

/*===========================================================*/

static int SkipComments(FILE *file)
  /* skips comments (#...), all lines, starting from current position in a file
     returns number of lines skipped */
{
  int lines=0;
  char buf[BUF_LINE];

  while (fgetc(file)=='#') {
    do fgets(buf,BUF_LINE,file); while (strstr(buf,"\n")==NULL);
    lines++;
  }
  fseek(file,-1,SEEK_CUR);

  return lines;
}

/*===========================================================*/

static void InitDipFile(const char *fname, int *maxX, int *maxY, int *maxZ, int *Nm)
   /* read dipole file first to determine box sizes and Nmat */
{
  FILE *input;
  int x, y, z, ext=FALSE, cond, mat, line=0;
  char buf[BUF_LINE];

  input=FOpenErr(fname,"r",ALL_POS);

  line=SkipComments(input)+1;
  /* read Nmat if present */
  if (fscanf(input,"Nmat=%d\n",Nm)!=1) *Nm=1;
  else {
    ext=TRUE;
    if (*Nm<=0) LogError(EC_ERROR,ONE_POS,"Nmat is nonpositive, as given in %s",fname);
    if (*Nm==1) LogError(EC_INFO,ONE_POS,
                         "Nmat is given in dipole file - %s, but is trivial (=1)",fname);
    line++;
  }
  /* scan main part of the file */
  *maxX=*maxY=*maxZ=0;
     /* reading is performed in lines */
  while(fgets(buf,BUF_LINE,input)!=NULL) {
    if (strstr(buf,"\n")==NULL) LogError(EC_ERROR,ONE_POS,
      "Buffer overflow while scanning lines in file '%s' (size of line %d > %d)",
      fname,line,BUF_LINE-1);
    if (ext) cond=(sscanf(buf,geom_format_ext,&x,&y,&z,&mat)!=4);
    else cond=(sscanf(buf,geom_format,&x,&y,&z)!=3);
    if (cond)
      LogError(EC_ERROR,ONE_POS,"Could not scan from dipole file - %s - line %d",fname,line);
    /* check for errors in values */
    if (x<0)
      LogError(EC_ERROR,ONE_POS,"Negative coordinate - %s - line %d: x=%d",fname,line,x);
    if (y<0)
      LogError(EC_ERROR,ONE_POS,"Negative coordinate - %s - line %d: y=%d",fname,line,y);
    if (z<0)
      LogError(EC_ERROR,ONE_POS,"Negative coordinate - %s - line %d: z=%d",fname,line,z);
    if (ext) if (mat>*Nm) LogError(EC_ERROR,ONE_POS,
      "Given material number - %s - line %d: mat=%d is greater than provided Nmat (%d)",
      fname,line,mat,*Nm);
    /* update maximums */
    if (x>*maxX) *maxX=x;
    if (y>*maxY) *maxY=y;
    if (z>*maxZ) *maxZ=z;
    line++;
  }
  *maxX=jagged*(*maxX+1);
  *maxY=jagged*(*maxY+1);
  *maxZ=jagged*(*maxZ+1);
  FCloseErr(input,fname,ALL_POS);
}

/*===========================================================*/

static void ReadDipFile(const char *fname)
   /* read dipole file;
       no consistency checks are made since they are made in InitDipFile */
{
  FILE *input;
  int x, y, z, x0, y0, z0, mat, ext=FALSE;
  int index;
  char buf[BUF_LINE];

  input=FOpenErr(fname,"r",ALL_POS);

  SkipComments(input);
  /* skip Nmat if present */
  if (fscanf(input,"Nmat=%d\n",&mat)==1) ext=TRUE;
  else mat=1;

  while(fgets(buf,BUF_LINE,input)!=NULL) {
    if (ext) sscanf(buf,geom_format_ext,&x0,&y0,&z0,&mat);
    else sscanf(buf,geom_format,&x0,&y0,&z0);

    for (z=jagged*z0;z<jagged*(z0+1);z++) if (z>=local_z0 && z<local_z1_coer)
      for (x=jagged*x0;x<jagged*(x0+1);x++) for (y=jagged*y0;y<jagged*(y0+1);y++) {
        index=(z-local_z0)*boxX*boxY+y*boxX+x;
        material_tmp[index]=(unsigned char)(mat-1);
    }
  }
  FCloseErr(input,fname,ALL_POS);
}

/*==========================================================*/

static int FitBox(const int box)
   /* finds the smallest value for which program would work
      (should be even and divide jagged) */
{
  if (jagged%2==0) return (jagged*((box+jagged-1)/jagged));
  else return (2*jagged*((box+2*jagged-1)/(2*jagged)));
}

/*==========================================================*/

void InitShape(void)
   /* perform of initialization of symmetries and boxY, boxZ
    * Estimate the volume of the particle, when not discretisized.
    * Check whether enough refractive indices are specified
    */
{
  int n_boxX, n_boxYi, n_boxZi, temp;   /* new values for dimensions */
  double h_d,b_d,c_d,h2,b2,c2;
  double n_boxY, n_boxZ;
  clock_t tstart;
  int Nmat_need;

  tstart=clock();
  /* for some shapes volume_ratio is initialized below;
     if not, volume correction is not used */
  volume_ratio=UNDEF;
  /* if box is not defined by command line or read from file;
        if size is defined, dpl is initialized to default, and grid is calculated
                (dpl is slightly corrected aferwards)
        else grid is initialized to default
     then (in make_particle() ) the dpl is determined from size (if it is defined)
         or set by default (after it size is determined */
  if (boxX==UNDEF) {
    if (shape!=SH_READ) {
      if (sizeX!=UNDEF) {
        if (dpl==UNDEF) dpl=DEF_DPL; /* default value of dpl */
        boxX=FitBox((int)ceil(sizeX*dpl/lambda));
        dpl=UNDEF;     /* dpl is given correct value in make_particle() */
      }
      else boxX=DEF_GRID; /* default value for boxX */
    }
  }
  else {
    temp=boxX;
    if ((boxX=FitBox(boxX))!=temp)
      LogError(EC_WARN,ONE_POS,"boxX has been adjusted from %i to %i",temp,boxX);
  }
  n_boxX=boxX;

  /* initialization of global option index for error messages */
  opt=opt_sh;
  /* shape initialization */
  if (shape==SH_BOX) {
    strcpy(sh_form_str,"cube; size of edge:%g");
    symX=symY=symZ=TRUE;
    if (boxY==UNDEF || boxX==boxY) symR=TRUE;
    else symR=FALSE;
    n_boxY=n_boxZ=boxX;
    Nmat_need=1;
  }
  else if (shape==SH_COATED) {
    coat_ratio=sh_pars[0];
    TestRange(coat_ratio,"innner/outer diameter ratio",0,1);
    sprintf(sh_form_str,"coated sphere; diameter(d):%%g, inner diameter d_in/d=%g",coat_ratio);
    if (sh_Npars==4) {
      coat_x=sh_pars[1];
      coat_y=sh_pars[2];
      coat_z=sh_pars[3];
      if (coat_x*coat_x+coat_y*coat_y+coat_z*coat_z>(1-coat_ratio)*(1-coat_ratio))
        PrintErrorHelp("Inner sphere is not fully inside the outer");
      sprintf(sh_form_str+strlen(sh_form_str),
              "\n       position of inner sphere center r/d={%g, %g, %g}",coat_x,coat_y,coat_z);
    }
    else coat_x=coat_y=coat_z=0; /* initialize default values */
    coat_r2=0.25*coat_ratio*coat_ratio;
    symX=symY=symZ=symR=TRUE;
    volume_ratio=PI/6;
    if (coat_x!=0) {symX=FALSE; symR=FALSE;}
    if (coat_y!=0) {symY=FALSE; symR=FALSE;}
    if (coat_z!=0) symZ=FALSE;
    n_boxY=n_boxZ=boxX;
    Nmat_need=2;
  }
  else if(shape==SH_CYLINDER) {
    diskratio=sh_pars[0];
    TestPositive(diskratio,"height to diameter ratio");
    sprintf(sh_form_str,"cylinder; diameter(d):%%g, height h/d=%g",diskratio);
    symX=symY=symZ=symR=TRUE;
    volume_ratio=PI/4*diskratio;
    n_boxY=boxX;
    n_boxZ=diskratio*boxX;
    Nmat_need=1;
  }
  else if (shape==SH_ELLIPSOID) {
    ellipsY=sh_pars[0];
    TestPositive(ellipsY,"aspect ratio y/x");
    ellipsZ=sh_pars[1];
    TestPositive(ellipsZ,"aspect ratio z/x");
    sprintf(sh_form_str,"ellipsoid; size along X:%%g, aspect ratios y/x=%g, z/x=%g",
            ellipsY,ellipsZ);
    symX=symY=symZ=TRUE;
    if (1==ellipsY) symR=TRUE; else symR=FALSE;
    volume_ratio=PI/6*ellipsY*ellipsZ;
    n_boxY=ellipsY*boxX;
    n_boxZ=ellipsZ*boxX;
    Nmat_need=1;
  }
  else if (shape==SH_LINE) {
    strcpy(sh_form_str,"line; legth:%g");
    symX=TRUE;
    symY=symZ=symR=FALSE;
    n_boxY=n_boxZ=jagged;
    Nmat_need=1;
  }
  else if(shape==SH_RBC) {
    /* three-parameter shape; developed by K.A.Semyanov,P.A.Tarasov,P.A.Avrorov
       based on work by P.W.Kuchel and E.D.Fackerell, "Parametric-equation representation
       of biconcave erythrocytes," Bulletin of Mathematical Biology 61, 209-220 (1999). */
    h_d=sh_pars[0];
    TestPositive(h_d,"ratio of maximum width to diameter");
    b_d=sh_pars[1];
    TestPositive(b_d,"ratio of minimum width to diameter");
    if (h_d <= b_d) PrintErrorHelp("given RBC is not biconcave; maximum width is in the center");
    c_d=sh_pars[2];
    TestRange(c_d,"relative diameter of maximum width",0,1);
    sprintf(sh_form_str,
      "red blood cell; diameter(d):%%g, maximum and minimum width h/d=%g, b/d=%g\n"\
      "       diameter of maximum width c/d=%g",h_d,b_d,c_d);
    /* calculate shape parameters */
    h2=h_d*h_d;
    b2=b_d*b_d;
    c2=c_d*c_d;
    P=(b2*((c2*c2/(h2-b2))-h2)-1)/4;
    R=-(P+0.25)/4;
    Q=((P+0.25)/b2)-(b2/4);
    S=-(2*P+c2)/h2;

    symX=symY=symZ=symR=TRUE;
    n_boxY=boxX;
    n_boxZ=h_d*boxX;
    volume_ratio=UNDEF;
    Nmat_need=1;
  }
  else if (shape==SH_READ) {
    sprintf(sh_form_str,"specified by file %s; size along X:%%g",aggregate_file);
    symX=symY=symZ=symR=FALSE; /* input file is assumed assymetric */
    InitDipFile(aggregate_file,&n_boxX,&n_boxYi,&n_boxZi,&Nmat_need);
    n_boxY=n_boxYi;
    n_boxZ=n_boxZi;
  }
  else if (shape==SH_SPHERE) {
    strcpy(sh_form_str,"sphere; diameter:%g");
    symX=symY=symZ=symR=TRUE;
    volume_ratio=PI/6;
    n_boxY=n_boxZ=boxX;
    Nmat_need=1;
  }
  else if (shape==SH_SPHEREBOX) {
    coat_ratio=sh_pars[0];
    TestRange(coat_ratio,"sphere diameter/cube edge ratio",0,1);
    sprintf(sh_form_str,
      "sphere in cube; size of cube edge(a):%%g, diameter of sphere d/a=%g",coat_ratio);
    coat_r2=0.25*coat_ratio*coat_ratio;
    symX=symY=symZ=TRUE;
    if (boxY==UNDEF || boxX==boxY) symR=TRUE;
    else symR=FALSE;
    n_boxY=n_boxZ=boxX;
    Nmat_need=2;
  }
/*  else if(shape==SH_SDISK_ROT) {
    symX=symY=symZ=FALSE;
    symR=FALSE;
    volume_ratio=boxX*boxX*boxX;
  }
  else if (shape==SH_PRISMA) {
    symX=TRUE;
    symY=symZ=FALSE;
    symR=FALSE;
    volume_ratio=.5*boxX*boxY*boxZ;
  }  */

  /* check if enough refr. indices or extra*/
  if (Nmat<Nmat_need) LogError(EC_ERROR,ONE_POS,
                      "Only %d refractive indices is given. %d is required",Nmat,Nmat_need);
  else if (Nmat>Nmat_need) {
    LogError(EC_INFO,ONE_POS,
             "More refractive indices is given (%d) than actually used (%d)",Nmat,Nmat_need);
    Nmat=Nmat_need;
  }

  if (symmetry_enforced) symX=symY=symZ=symR=TRUE;
  else if (NoSymmetry) symX=symY=symZ=symR=FALSE;

  if (boxX==UNDEF) boxX=FitBox(n_boxX);
  else if (n_boxX>boxX) LogError(EC_ERROR,ONE_POS,
                        "Particle (boxX=%d) does not fit into specified boxX=%d", n_boxX, boxX);

  n_boxYi=(int)ceil(n_boxY);
  n_boxZi=(int)ceil(n_boxZ);
  if (boxY==UNDEF) { /* assumed that boxY and boxZ are either both defined or both not defined */
    boxY=FitBox(n_boxYi);
    boxZ=FitBox(n_boxZi);
  }
  else {
    temp=boxY;
    if ((boxY=FitBox(boxY))!=temp)
      LogError(EC_WARN,ONE_POS,"boxY has been adjusted from %i to %i",temp,boxY);
    temp=boxZ;
    if ((boxZ=FitBox(boxZ))!=temp)
      LogError(EC_WARN,ONE_POS,"boxZ has been adjusted from %i to %i",temp,boxZ);
    /* this error is not duplicated in the logfile since it does not yet exist */
    if (n_boxYi>boxY || n_boxZi>boxZ) LogError(EC_ERROR,ONE_POS,
      "Particle (boxY,Z={%d,%d}) does not fit into specified boxY,Z={%d,%d}",
      n_boxYi,n_boxZi,boxY,boxZ);
  }
  /* initialize number of dipoles */
  Ndip=boxX*boxY*boxZ;
  /* initialize maxiter; not very realistic */
  if (maxiter==UNDEF) maxiter=3*Ndip;
  /* initialize nTheta */
  if (nTheta==UNDEF) {
    if (Ndip<1000) nTheta=91;
    else if (Ndip<10000) nTheta=181;
    else if (Ndip<100000) nTheta=361;
    else nTheta=721;
  }
  Timing_Particle = clock() - tstart;
}

/*==========================================================*/

void MakeParticle(void)
  /* creates a particle; initializes all dipoles counts, dpl, gridspace */
{
  int i, j, k, index;
  double x, y, z;
  double a_eq;
  double xr,yr,zr,xcoat,ycoat,zcoat,r2,z2;
  double centreX,centreY,centreZ;
  int xj,yj,zj;
  int mat;
  int mat_count[MAX_NMAT+1];   /* number of dipoles in a material */
  clock_t tstart;

  tstart=clock();

  index=0;
  /* assumed that box's are even */
  centreX=centreY=centreZ=jagged/2.0;
  /* allocate temporary memory; even if prognose, since they are needed for exact estimation
     they will be reallocated afterwards (when nlocalRows is known) */
  if ((material_tmp = (unsigned char *) malloc(local_Ndip*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL_POS,"Could not malloc material_tmp");
  if ((DipoleCoord_tmp = dVector(0,3*local_Ndip-1)) == NULL)
    LogError(EC_ERROR,ALL_POS,"Could not malloc DipoleCoord_tmp");
  if ((position_tmp = (short int *) malloc(3*sizeof(short int)*local_Ndip)) == NULL)
    LogError(EC_ERROR,ALL_POS,"Could not malloc position_tmp");

  for(k=local_z0-boxZ/2;k<local_z1_coer-boxZ/2;k++)
    for(j=-boxY/2;j<boxY/2;j++)
      for(i=-boxX/2;i<boxX/2;i++) {
        x=(i+centreX);
        y=(j+centreY);
        z=(k+centreZ);

        xj=jagged*((i+boxX/2)/jagged)-boxX/2;
        yj=jagged*((j+boxY/2)/jagged)-boxY/2;
        zj=jagged*((k+boxZ/2)/jagged)-boxZ/2;

        xr=(xj+centreX)/(boxX);
        yr=(yj+centreY)/(boxX);
        zr=(zj+centreZ)/(boxX);

        mat=Nmat;  /* corresponds to void */

        if (shape==SH_BOX) mat=0;
        else if (shape==SH_COATED) {
          xcoat=xr-coat_x;
          ycoat=yr-coat_y;
          zcoat=zr-coat_z;
          if (xcoat*xcoat+ycoat*ycoat+zcoat*zcoat <= coat_r2) mat=1;
          else if (xr*xr+yr*yr+zr*zr <= 0.25) mat=0;
        }
        else if (shape==SH_CYLINDER) {
          if((fabs(zr) <= diskratio/2) && (xr*xr + yr*yr <= 0.25)) mat = 0;
        }
        else if (shape==SH_ELLIPSOID) {
          if (xr*xr + yr*yr/(ellipsY*ellipsY) + zr*zr/(ellipsZ*ellipsZ) <= 0.25) mat = 0;
        }
        else if (shape==SH_LINE) {
          if (yr>=0 && yr<=jagged/2.0 && zr>=0 && zr<=jagged/2.0) mat=0;
        }
        else if (shape==SH_SPHERE) {
          if (xr*xr+yr*yr+zr*zr <= 0.25) mat=0;
        }
        else if (shape==SH_SPHEREBOX) {
          if (xr*xr+yr*yr+zr*zr <= coat_r2) mat=1;
          else mat=0;
        }
        else if (shape==SH_RBC) {
          r2=xr*xr+yr*yr;
          z2=zr*zr;
          if (r2*r2+2*S*r2*z2+z2*z2+P*r2+Q*z2+R <= 0) mat=0;
        }
/*        else if  (shape==SH_SDISK_ROT) {
          xr= (i+centreX)/(boxX);
          yr= (j+centreY)/(boxY);
          zr= (k+centreZ)/(boxZ);
          CY=cos(PI/180*betaY);  SY=sin(PI/180*betaY);
          CZ=cos(PI/180*betaZ);  SZ=sin(PI/180*betaZ);
          xr_=xr*CY*CZ-yr*SZ-zr*SY*CZ;
          yr_=xr*CY*SZ+yr*CZ-zr*SY*SZ;
          zr_=xr*SY+zr*CY;
          cthick=(aspect_r)/2.0;
          radius=(xr_*xr_+yr_*yr_+zr_*zr_)/0.25;
          if (radius <1.0000001 && xr_<cthick && xr_>-cthick) mat=0;
        }
        else if (shape==SH_PRISMA && y*boxZ>=-z*boxY) mat=0;*/

        position_tmp[3*index]=(short int)(i+boxX/2);
        position_tmp[3*index+1]=(short int)(j+boxY/2);
        position_tmp[3*index+2]=(short int)(k+boxZ/2);
        /* afterwards multiplied by gridspace */
        DipoleCoord_tmp[3*index] = x;
        DipoleCoord_tmp[3*index+1] = y;
        DipoleCoord_tmp[3*index+2] = z;
        material_tmp[index]=(unsigned char)mat;
        index++;
  } /* End box loop */
  if (shape==SH_READ) ReadDipFile(aggregate_file);

  /* initialization of mat_count and dipoles counts */
  for(i=0;i<=Nmat;i++) mat_count[i]=0;
  for(i=0;i<local_Ndip;i++) mat_count[material_tmp[i]]++;
  local_nvoid_Ndip=local_Ndip-mat_count[Nmat];
  MyInnerProduct(mat_count,int_type,Nmat+1);
  if ((nvoid_Ndip=Ndip-mat_count[Nmat])==0)
    LogError(EC_ERROR,ONE_POS,"All dipoles of the scatterer are void");
  nlocalRows=3*local_nvoid_Ndip;
  /* initialize dpl and gridspace */
  volcor_used=(volcor && (volume_ratio!=UNDEF));
  if (sizeX==UNDEF) {
    if (dpl==UNDEF) dpl=DEF_DPL; /* default value of dpl */
    /* sizeX is determined to give correct volume */
    if (volcor_used) sizeX=lambda*pow(nvoid_Ndip/volume_ratio,ONE_THIRD)/dpl;
    else sizeX=lambda*boxX/dpl;
  }
  else {
    if (dpl!=UNDEF)
      LogError(EC_ERROR,ONE_POS,"Extra information (all of dpl,grid,size) are given");
    /* dpl is determined to give correct volume */
    if (volcor_used) dpl=lambda*pow(nvoid_Ndip/volume_ratio,ONE_THIRD)/sizeX;
    else dpl=lambda*boxX/sizeX;
  }
  gridspace=lambda/dpl;
  /* initialize equivalent size parameter and cross section */
  WaveNum  = TWO_PI/lambda;
  kd = TWO_PI/dpl;
  a_eq = pow((0.75/PI)*nvoid_Ndip,ONE_THIRD)*gridspace;
  ka_eq = WaveNum*a_eq;
  inv_G = 1/(PI*a_eq*a_eq);
  /* allocate main particle arrays, using precise nlocalRows
     even when prognose to enable save_geom afterwards */
  if ((material = (unsigned char *) malloc(local_nvoid_Ndip*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL_POS,"Could not malloc material");
  if ((DipoleCoord = dVector(0,nlocalRows-1)) == NULL)
    LogError(EC_ERROR,ALL_POS,"Could not malloc DipoleCoord");
  if ((position = (short int *) malloc(nlocalRows*sizeof(short int))) == NULL)
    LogError(EC_ERROR,ALL_POS,"Could not malloc position");
  memory+=(3*(sizeof(short int)+sizeof(double))+sizeof(char))*local_nvoid_Ndip;
  /* copy nontrivial part of arrays */
  index=0;
  for (i=0;i<local_Ndip;i++) if (material_tmp[i]<Nmat) {
    material[index]=material_tmp[i];
     /* DipoleCoord=gridspace*DipoleCoord_tmp */
    MultScal(gridspace,DipoleCoord_tmp+3*i,DipoleCoord+3*index);
    memcpy(position+3*index,position_tmp+3*i,3*sizeof(short int));
    index++;
  }
  /* free temporary memory */
  free(material_tmp);
  Free_dVector(DipoleCoord_tmp,0);
  free(position_tmp);

  /* save geometry */
  if (save_geom) SaveGeometry();

  Timing_Particle += clock() - tstart;
}

