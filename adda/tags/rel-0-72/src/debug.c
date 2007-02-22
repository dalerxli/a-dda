/* FILE: debug.c
 * AUTH: vesseur
 * DESCR: Functions for printing debugging information when compiling
 *        with option -DDEBUG
 *
 *        Currently is developed by Maxim Yurkin
 */
#include <stdio.h>
#include <stdarg.h>
#include "cmplx.h"
#include "comm.h"

/*============================================================*/

void DebugPrintf(char *fname, int line, char *fmt, ... )
{ 
  va_list args;
  char pr_line[255];
  extern int ringid;
  
  va_start(args, fmt);
  sprintf(pr_line, "%s:%d: ", fname, line);
  vsprintf(pr_line+strlen(pr_line), fmt, args);
  strcat(pr_line, "\n");
  printf("(ringID=%i) %s",ringid,pr_line);
  fflush(stdout);
  va_end(args);
}

/*=======================================================*/

void FieldPrint (doublecomplex *x) /* print current field at certain dipole
				      not used; left for deep debug */
{
  extern FILE *logfile;
  extern double *DipoleCoord;
  int i=9810;

  i*=3;
  fprintf(logfile,"Dipole coords = %.10E, %.10E, %.10E\n",
          DipoleCoord[i],DipoleCoord[i+1],DipoleCoord[i+2]);
  fprintf(logfile,"E = %.10E%+.10Ei,  %.10E%+.10Ei, %.10E%+.10Ei\n",
	x[i][RE],x[i][IM],x[i+1][RE],x[i+1][IM],x[i+2][RE],x[i+2][IM]);
}
