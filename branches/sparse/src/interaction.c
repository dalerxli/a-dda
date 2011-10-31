#include "cmplx.h"
#include "const.h"
#include "interaction.h"
#include "io.h"
#include "types.h"
#include "vars.h"
#include "param.h"

// defined and initialized in calculator.c
extern const double * restrict tab1,* restrict tab2,* restrict tab3,* restrict tab4,* restrict tab5,
	* restrict tab6,* restrict tab7,* restrict tab8,* restrict tab9,* restrict tab10;
extern const int * restrict * restrict tab_index;
extern double gridspace;
extern double igt_lim, igt_eps;

#ifndef NO_FORTRAN
// fort/propaesplibreintadda.f
void propaespacelibreintadda_(const double *Rij,const double *ka,const double *arretecube,
	const double *relreq, double *result);
#endif

// sinint.c
void cisi(double x,double *ci,double *si);

//============================================================

INLINE bool TestTableSize(const double rn)
// tests if rn fits into the table; if not, returns false and produces info message
{
	static bool warned=false;

	if (rn>TAB_RMAX) {
		if (!warned) {
			warned=true;
			LogWarning(EC_INFO,ONE_POS,"Not enough table size (available only up to R/d=%d), "
				"so O(kd^2) accuracy of Green's function is not guaranteed",TAB_RMAX);
		}
		return false;
	}
	else return true;
}

//============================================================

void CalcInterTerm(const int i,const int j,const int k,doublecomplex * restrict result)
/* calculates interaction term between two dipoles; given integer distance vector {i,j,k}
 * (in units of d). All six components of the symmetric matrix are computed at once.
 * The elements in result are: [G11, G12, G13, G22, G23, G33]
 */
{
	double rr,qvec[3],q2[3],invr3,qavec[3],av[3];
	double kr,kr2,kr3,kd2,q4,rn,rn2;
	double temp,qmunu[6],qa,qamunu[6],invrn,invrn2,invrn3,invrn4;
	double dmunu[6]; // KroneckerDelta[mu,nu] - can serve both as multiplier, and as bool
	double kfr,ci,si,ci1,si1,ci2,si2,brd,cov,siv,g0,g2;
	doublecomplex expval,br,br1,m,m2,Gf1,Gm0,Gm1,Gc1,Gc2;
	int ind0,ind1,ind2,ind2m,ind3,ind4,indmunu,comp,mu,nu,mu1,nu1;
	int sigV[3],ic,sig,ivec[3],ord[3],invord[3];
	double t3q,t3a,t4q,t4a,t5tr,t5aa,t6tr,t6aa;
	const bool inter_avg=true; // temporary fixed option for SO formulation

// this is used for debugging, should be empty define, when not required
#define PRINT_GVAL /*printf("%d,%d,%d: %g%+gi, %g%+gi, %g%+gi,\n%g%+gi, %g%+gi, %g%+gi\n",\
	i,j,k,result[0][RE],result[0][IM],result[1][RE],result[1][IM],result[2][RE],result[2][IM],\
	result[3][RE],result[3][IM],result[4][RE],result[4][IM],result[5][RE],result[5][IM]);*/ 

	// self interaction; self term is computed in different subroutine
	if (i==0 && j==0 && k==0) for (comp=0;comp<NDCOMP;comp++) {
		result[comp][RE]=result[comp][IM]=0;
		return;
	}
	//====== calculate some basic constants ======
	qvec[0]=i; // qvec is normalized below (after IGT)
	qvec[1]=j;
	qvec[2]=k;
	rn2=DotProd(qvec,qvec);
	rn=sqrt(rn2); // normalized r
#ifndef NO_FORTRAN
	if (IntRelation==G_IGT && (igt_lim==UNDEF || rn<=igt_lim)) { // a special case
		double rtemp[3];
		vMultScal(gridspace,qvec,rtemp);
		propaespacelibreintadda_(rtemp,&WaveNum,&gridspace,&igt_eps,(double *)result);
		PRINT_GVAL;
		return;
	}
#endif
	// a common part of the code ((up to FCD...), which effectively implements G_POINT_DIP
	invrn=1/rn;
	vMultScalSelf(invrn,qvec); // finalize qvec
	rr=rn*gridspace;
	invr3=1/(rr*rr*rr);
	kr=WaveNum*rr;
	kr2=kr*kr;
	kfr=PI*rn; // k_F*r, for FCD
	// cov=cos(kr); siv=sin(kr); expval=Exp(ikr)/r^3
	imExp(kr,expval);
	cov=expval[RE];
	siv=expval[IM];
	cMultReal(invr3,expval,expval);
	//====== calculate Gp ========
	for (mu=0,comp=0;mu<3;mu++) for (nu=mu;nu<3;nu++,comp++) {
		dmunu[comp]= mu==nu ? 1 : 0;
		qmunu[comp]=qvec[mu]*qvec[nu];
		// br=delta[mu,nu]*(-1+ikr+kr^2)-qmunu*(-3+3ikr+kr^2)
		br[RE]=(3-kr2)*qmunu[comp];
		br[IM]=-3*kr*qmunu[comp];
		if(dmunu[comp]) {
			br[RE]+=kr2-1;
			br[IM]+=kr;
		}
		// result=Gp=expval*br
		cMult(br,expval,result[comp]);
	}
	//====== FCD (static and full) ========
	/* speed of FCD can be improved by using faster version of sici routine, using predefined
	 * tables, etc (e.g. as is done in GSL library). But currently extra time for this computation
	 * is already smaller than one main iteration.
	 */
	if (IntRelation==G_FCD_ST) {
		/* FCD is based on Gay-Balmaz P., Martin O.J.F. "A library for computing the filtered and
		 * non-filtered 3D Green's tensor associated with infinite homogeneous space and surfaces",
		 * Comp. Phys. Comm. 144:111-120 (2002), and Piller N.B. "Increasing the performance of the
		 * coupled-dipole approximation: A spectral approach", IEEE Trans.Ant.Propag. 46(8):
		 * 1126-1137. Here it differs by a factor of 4*pi*k^2.
		 */
		// result = Gp*[3*Si(k_F*r)+k_F*r*cos(k_F*r)-4*sin(k_F*r)]*2/(3*pi)
		cisi(kfr,&ci,&si);
		brd=TWO_OVER_PI*ONE_THIRD*(3*si+kfr*cos(kfr)-4*sin(kfr));
		for (comp=0;comp<NDCOMP;comp++) cMultReal(brd,result[comp],result[comp]);
	}
	else if (IntRelation==G_FCD) {
		// ci,si_1,2 = ci,si_+,- = Ci,Si((k_F +,- k)r)
		cisi(kfr+kr,&ci1,&si1);
		cisi(kfr-kr,&ci2,&si2);
		// ci=ci1-ci2; si=pi-si1-si2
		ci=ci1-ci2;
		si=PI-si1-si2;
		g0=INV_PI*(siv*ci+cov*si);
		g2=INV_PI*(kr*(cov*ci-siv*si)+2*ONE_THIRD*(kfr*cos(kfr)-4*sin(kfr)))-g0;
		temp=g0*kr2;
		for (comp=0;comp<NDCOMP;comp++) {
			// brd=(delta[mu,nu]*(-g0*kr^2-g2)+qmunu*(g0*kr^2+3g2))/r^3
			brd=qmunu[comp]*(temp+3*g2);
			if (dmunu[comp]) brd-=temp+g2;
			brd*=invr3;
			// result=Gp+brd
			result[comp][RE]+=brd;
		}
	}
	//======= second order corrections ========
	else if (IntRelation==G_IGT_SO) {
		/* There is still some space for speed optimization here (e.g. move mu,nu-independent
		 * operations out of the cycles over components).
		 */
		kd2=kd*kd;
		if (kr*rn < G_BOUND_CLOSE && TestTableSize(rn)) {
			//====== G close for IGT =============
			ivec[0]=i;
			ivec[1]=j;
			ivec[2]=k;
			// transformation of negative coordinates
			for (ic=0;ic<3;ic++) {
				if (ivec[ic]<0) {
					sigV[ic]=-1;
					qvec[ic]*=-1;
					ivec[ic]*=-1;
				}
				else sigV[ic]=1;
			}
			// transformation to case i>=j>=k>=0
			// building of ord; ord[x] is x-th largest coordinate (0-th - the largest)
			if (ivec[0]>=ivec[1]) { // i>=j
				if (ivec[0]>=ivec[2]) { // i>=k
					ord[0]=0;
					if (ivec[1]>=ivec[2]) { // j>=k
						ord[1]=1;
						ord[2]=2;
					}
					else {
						ord[1]=2;
						ord[2]=1;
					}
				}
				else {
					ord[0]=2;
					ord[1]=0;
					ord[2]=1;
				}
			}
			else {
				if (ivec[0]>=ivec[2]) { // i>=k
					ord[0]=1;
					ord[1]=0;
					ord[2]=2;
				}
				else {
					ord[2]=0;
					if (ivec[1]>=ivec[2]) { // j>=k
						ord[0]=1;
						ord[1]=2;
					}
					else {
						ord[0]=2;
						ord[1]=1;
					}
				}
			}
			// change parameters according to coordinate transforms
			Permutate(qvec,ord);
			Permutate_i(ivec,ord);
			// compute inverse permutation
			memcpy(invord,ord,3*sizeof(int));
			Permutate_i(invord,ord);
			if (invord[0]==0 && invord[1]==1 && invord[2]==2) memcpy(invord,ord,3*sizeof(int));
			// temp = kr/24; and set some indices
			temp=kr/24;
			ind0=tab_index[ivec[0]][ivec[1]]+ivec[2];
			ind1=3*ind0;
			ind2m=6*ind0;
			// cycle over tensor components
			for (mu=0,comp=0;mu<3;mu++) for (nu=mu;nu<3;nu++,comp++) {
				sig=sigV[mu]*sigV[nu]; // sign of some terms below
				/* indexes for tables of different dimensions based on transformed indices mu and nu
				 * '...munu' variables are invariant to permutations because both constituent
				 * vectors and indices are permutated. So this variables can be used, as precomputed
				 * above.
				 */
				mu1=invord[mu];
				nu1=invord[nu];
				/* indmunu is a number of component[mu,nu] in a symmetric matrix, but counted
				 * differently than comp. This is {{0,1,3},{1,2,4},{3,4,5}}
				 */
				indmunu=mu1+nu1;
				if (mu1==2 || nu1==2) indmunu++;
				ind2=ind2m+indmunu;
				ind3=3*ind2;
				ind4=6*ind2;
				// computing several quantities with table integrals
				t3q=DotProd(qvec,tab3+ind1);
				t4q=DotProd(qvec,tab4+ind3);
				t5tr=TrSym(tab5+ind2m);
				t6tr=TrSym(tab6+ind4);
				//====== computing Gc0 =====
				/* br = delta[mu,nu]*(-I7-I9/2-kr*(i+kr)/24+2*t3q+t5tr)
				 *    - (-3I8[mu,nu]-3I10[mu,nu]/2-qmunu*kr*(i+kr)/24+2*t4q+t6tr)
				 */
				br[RE]=sig*(3*(tab10[ind2]/2+tab8[ind2])-2*t4q-t6tr)+temp*qmunu[comp]*kr;
				br[IM]=3*temp*qmunu[comp];
				if (dmunu[comp]) {
					br[RE]+=2*t3q+t5tr-temp*kr-tab9[ind0]/2-tab7[ind0];
					br[IM]-=temp;
				}
				// br*=kd^2
				cMultReal(kd2,br,br);
				// br+=I1*delta[mu,nu]*(-1+ikr+kr^2)-sig*I2[mu,nu]*(-3+3ikr+kr^2)
				br[RE]+=sig*tab2[ind2]*(3-kr2);
				br[IM]-=sig*tab2[ind2]*3*kr;
				if (dmunu[comp]) {
					br[RE]+=tab1[ind0]*(kr2-1);
					br[IM]+=tab1[ind0]*kr;
				}
				// Gc0=expval*br
				cMult(expval,br,result[comp]);
			}
		}
		else {
			//====== Gfar (and part of Gmedian) for IGT =======
			// Gf0 = Gp*(1-kd^2/24)
			for (comp=0;comp<NDCOMP;comp++) cMultReal(1-kd2/24,result[comp],result[comp]);
			if (kr < G_BOUND_MEDIAN) {
				//===== G median for IGT ========
				vSquare(qvec,q2);
				q4=DotProd(q2,q2);
				invrn2=invrn*invrn;
				invrn3=invrn2*invrn;
				invrn4=invrn2*invrn2;
				for (mu=0,comp=0;mu<3;mu++) for (nu=mu;nu<3;nu++,comp++) {
					// Gm0=expval*br*temp; temp is set anew
					temp=qmunu[comp]*(33*q4-7-12*(q2[mu]+q2[nu]));
					if (mu == nu) temp+=(1-3*q4+4*q2[mu]);
					temp*=7*invrn4/64;
					br[RE]=-1;
					br[IM]=kr;
					cMultReal(temp,br,Gm0);
					cMultSelf(Gm0,expval);
					// result = Gf + Gm0 + [ Gm1 ]
					cAdd(Gm0,result[comp],result[comp]);
				}
			}
		}
	}
	else if (IntRelation==G_SO) {
		/* There is still some space for speed optimization here (e.g. move mu,nu-independent
		 * operations out of the cycles over components). But now extra time is equivalent to 2-3
		 * main iterations. So first priority is to make something useful out of SO.
		 */
		// next line should never happen
		if (anisotropy) LogError(ONE_POS,"Incompatibility error in CalcInterTerm");
		kd2=kd*kd;
		kr3=kr2*kr;
		// only one refractive index can be used for FFT-compatible algorithm
		cEqual(ref_index[0],m);
		cSquare(m,m2);
		if (!inter_avg) {
			qa=DotProd(qvec,prop);
			for (mu=0,comp=0;mu<3;mu++) for (nu=mu;nu<3;nu++,comp++) {
				// qamunu=qvec[mu]*prop[nu] + qvec[nu]*prop[mu]
				qamunu[comp]=qvec[mu]*prop[nu];
				if (dmunu[comp]) qamunu[comp]*=2;
				else qamunu[comp]+=qvec[nu]*prop[mu];
			}
		}
		if (kr*rn < G_BOUND_CLOSE && TestTableSize(rn)) {
			//====== G close =============
			// av is copy of propagation vector
			if (!inter_avg) memcpy(av,prop,3*sizeof(double));
			ivec[0]=i;
			ivec[1]=j;
			ivec[2]=k;
			// transformation of negative coordinates
			for (ic=0;ic<3;ic++) {
				if (ivec[ic]<0) {
					sigV[ic]=-1;
					av[ic]*=-1;
					qvec[ic]*=-1;
					ivec[ic]*=-1;
				}
				else sigV[ic]=1;
			}
			// transformation to case i>=j>=k>=0
			// building of ord; ord[x] is x-th largest coordinate (0-th - the largest)
			if (ivec[0]>=ivec[1]) { // i>=j
				if (ivec[0]>=ivec[2]) { // i>=k
					ord[0]=0;
					if (ivec[1]>=ivec[2]) { // j>=k
						ord[1]=1;
						ord[2]=2;
					}
					else {
						ord[1]=2;
						ord[2]=1;
					}
				}
				else {
					ord[0]=2;
					ord[1]=0;
					ord[2]=1;
				}
			}
			else {
				if (ivec[0]>=ivec[2]) { // i>=k
					ord[0]=1;
					ord[1]=0;
					ord[2]=2;
				}
				else {
					ord[2]=0;
					if (ivec[1]>=ivec[2]) { // j>=k
						ord[0]=1;
						ord[1]=2;
					}
					else {
						ord[0]=2;
						ord[1]=1;
					}
				}
			}
			// change parameters according to coordinate transforms
			Permutate(qvec,ord);
			if (!inter_avg) Permutate(av,ord);
			Permutate_i(ivec,ord);
			// compute inverse permutation
			memcpy(invord,ord,3*sizeof(int));
			Permutate_i(invord,ord);
			if (invord[0]==0 && invord[1]==1 && invord[2]==2) memcpy(invord,ord,3*sizeof(int));
			// temp = kr/24; and set some indices
			temp=kr/24;
			ind0=tab_index[ivec[0]][ivec[1]]+ivec[2];
			ind1=3*ind0;
			ind2m=6*ind0;			// cycle over tensor components
			for (mu=0,comp=0;mu<3;mu++) for (nu=mu;nu<3;nu++,comp++) {
				sig=sigV[mu]*sigV[nu]; // sign of some terms below
				/* indexes for tables of different dimensions based on transformed indices mu and nu
				 * '...munu' variables are invariant to permutations because both constituent
				 * vectors and indices are permutated. So this variables can be used, as precomputed
				 * above.
				 */
				mu1=invord[mu];
				nu1=invord[nu];
				/* indmunu is a number of component[mu,nu] in a symmetric matrix, but counted
				 * differently than comp. This is {{0,1,3},{1,2,4},{3,4,5}}
				 */
				indmunu=mu1+nu1;
				if (mu1==2 || nu1==2) indmunu++;
				ind2=ind2m+indmunu;
				ind3=3*ind2;
				ind4=6*ind2;
				// computing several quantities with table integrals
				t3q=DotProd(qvec,tab3+ind1);
				t4q=DotProd(qvec,tab4+ind3);
				t5tr=TrSym(tab5+ind2m);
				t6tr=TrSym(tab6+ind4);
				if (inter_avg) {
					// <a[mu]*a[nu]>=1/3*delta[mu,nu]
					t5aa=ONE_THIRD*t5tr;
					t6aa=ONE_THIRD*t6tr;
				}
				else {
					t3a=DotProd(av,tab3+ind1);
					t4a=DotProd(av,tab4+ind3);
					t5aa=QuadForm(tab5+ind2m,av);
					t6aa=QuadForm(tab6+ind4,av);
				}
				//====== computing Gc0 =====
				/* br = delta[mu,nu]*(-I7-I9/2-kr*(i+kr)/24+2*t3q+t5tr)
				 *    - (-3I8[mu,nu]-3I10[mu,nu]/2-qmunu*kr*(i+kr)/24+2*t4q+t6tr)
				 */
				br[RE]=sig*(3*(tab10[ind2]/2+tab8[ind2])-2*t4q-t6tr)+temp*qmunu[comp]*kr;
				br[IM]=3*temp*qmunu[comp];
				if (dmunu[comp]) {
					br[RE]+=2*t3q+t5tr-temp*kr-tab9[ind0]/2-tab7[ind0];
					br[IM]-=temp;
				}
				// br*=kd^2
				cMultReal(kd2,br,br);
				// br+=I1*delta[mu,nu]*(-1+ikr+kr^2)-sig*I2[mu,nu]*(-3+3ikr+kr^2)
				br[RE]+=sig*tab2[ind2]*(3-kr2);
				br[IM]-=sig*tab2[ind2]*3*kr;
				if (dmunu[comp]) {
					br[RE]+=tab1[ind0]*(kr2-1);
					br[IM]+=tab1[ind0]*kr;
				}
				// Gc0=expval*br
				cMult(expval,br,result[comp]);
				//==== computing Gc1 ======
				if (!inter_avg) {
					// br=(kd*kr/24)*(qa*(delta[mu,nu]*(-2+ikr)-qmunu*(-6+ikr))-qamunu)
					br[RE]=6*qmunu[comp];
					br[IM]=-kr*qmunu[comp];
					if (dmunu[comp]) {
						br[RE]-=2;
						br[IM]+=kr;
					}
					cMultReal(qa,br,br);
					br[RE]-=qamunu[comp];
					cMultReal(2*temp*kd,br,br);
					//  br1=(d/r)*(delta[mu,nu]*t3h*(-1+ikr)-sig*t4h*(-3+3ikr))
					br1[RE]=3*sig*t4a;
					br1[IM]=-kr*br1[RE];
					if (dmunu[comp]) {
						br1[RE]-=t3a;
						br1[IM]+=t3a*kr;
					}
					cMultReal(1/rn,br1,br1);
					// Gc1=expval*i*m*kd*(br1+br)
					cAdd(br,br1,Gc1);
					cMultSelf(Gc1,m);
					cMultReal(kd,Gc1,Gc1);
					cMultSelf(Gc1,expval);
					cMult_i(Gc1);
				}
				//==== computing Gc2 ======
				// br=delta[mu,nu]*t5aa-3*sig*t6aa-(kr/12)*(delta[mu,nu]*(i+kr)-qmunu*(3i+kr))
				br[RE]=-kr*qmunu[comp];
				br[IM]=-3*qmunu[comp];
				if (dmunu[comp]) {
					br[RE]+=kr;
					br[IM]+=1;
				}
				cMultReal(-2*temp,br,br);
				br[RE]-=3*sig*t6aa;
				if (dmunu[comp]) br[RE]+=t5aa;
				// Gc2=expval*(kd^2/2)*m^2*br
				cMult(m2,br,Gc2);
				cMultReal(kd2/2,Gc2,Gc2);
				cMultSelf(Gc2,expval);
				// result = Gc0 + [ Gc1 ] + Gc2
				if (!inter_avg) cAdd(Gc2,Gc1,Gc2);
				cAdd(Gc2,result[comp],result[comp]);
			}
		}
		else {
			//====== Gfar (and part of Gmedian) =======
			// temp=kd^2/24
			temp=kd2/24;
			// br=1-(1+m^2)*kd^2/24
			br[RE]=1-(1+m2[RE])*temp;
			br[IM]=-m2[IM]*temp;
			// Gf0 + Gf2 = Gp*br
			for (comp=0;comp<NDCOMP;comp++) cMultSelf(result[comp],br);
			//==== compute and add Gf1 ===
			if (!inter_avg) for (comp=0;comp<NDCOMP;comp++) {
				/* br = {delta[mu,nu]*(3-3ikr-2kr^2+ikr^3)-qmunu*(15-15ikr-6kr^2+ikr^3)}*qa
				 *    + qamunu*(3-3ikr-kr^2)
				 */
				br[RE]=(6*kr2-15)*qmunu[comp];
				br[IM]=(15*kr-kr3)*qmunu[comp];
				if(dmunu[comp]) {
					br[RE]+=3-2*kr2;
					br[IM]+=kr3-3*kr;
				}
				cMultReal(qa,br,br);
				br[RE]+=(3-kr2)*qamunu[comp];
				br[IM]-=3*kr*qamunu[comp];
				// Gf1=expval*i*m*temp*br
				cMult(m,br,Gf1);
				cMultReal(temp*2/kr,Gf1,Gf1);
				cMultSelf(Gf1,expval);
				cMult_i(Gf1);
				// result = Gf
				cAdd(Gf1,result[comp],result[comp]);
			}
			if (kr < G_BOUND_MEDIAN) {
				//===== G median ========
				vSquare(qvec,q2);
				q4=DotProd(q2,q2);
				invrn2=invrn*invrn;
				invrn3=invrn2*invrn;
				invrn4=invrn2*invrn2;
				for (mu=0,comp=0;mu<3;mu++) for (nu=mu;nu<3;nu++,comp++) {
					// Gm0=expval*br*temp; temp is set anew
					temp=qmunu[comp]*(33*q4-7-12*(q2[mu]+q2[nu]));
					if (mu == nu) temp+=(1-3*q4+4*q2[mu]);
					temp*=7*invrn4/64;
					br[RE]=-1;
					br[IM]=kr;
					cMultReal(temp,br,Gm0);
					cMultSelf(Gm0,expval);
					if (!inter_avg) {
						// Gm1=expval*i*m*temp; temp is set anew
						vMult(qvec,prop,qavec);
						temp = 3*qa*(dmunu[comp]-7*qmunu[comp]) + 6*dmunu[comp]*qvec[mu]*prop[mu]
							 - 7*(dmunu[comp]-9*qmunu[comp])*DotProd(qavec,q2)
							 + 3*(prop[mu]*qvec[nu]*(1-7*q2[mu])+prop[nu]*qvec[mu]*(1-7*q2[nu]));
						temp*=kd*invrn3/48;
						cMultReal(temp,m,Gm1);
						cMult_i(Gm1);
						cMultSelf(Gm1,expval);
						// add Gm1 to Gm0
						cAdd(Gm0,Gm1,Gm0);
					}
					// result = Gf + Gm0 + [ Gm1 ]
					cAdd(Gm0,result[comp],result[comp]);
				}
			}
		}
	}
	PRINT_GVAL;
}

#undef PRINT_GVAL

//============================================================
