#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/irenderable.h"
#include "../xrRender/FBasicVisual.h"

const	float	tweak_COP_initial_offs			= 1200.f	;
const	float	tweak_ortho_xform_initial_offs	= 1000.f	;	//. ?
const	float	tweak_guaranteed_range			= 20.f		;	//. ?

//float			OLES_SUN_LIMIT_27_01_07			= 180.f		;
float			OLES_SUN_LIMIT_27_01_07			= 100.f		;

//////////////////////////////////////////////////////////////////////////
// tables to calculate view-frustum bounds in world space
// note: OGL uses [-1..1] range for Z
static Fvector3		corners [8]			= {
	{ -1, -1, -1 },		{ -1, -1, +1},
	{ -1, +1, +1 },		{ -1, +1, -1},
	{ +1, +1, +1 },		{ +1, +1, -1},
	{ +1, -1, +1},		{ +1, -1, -1}
};
static int			facetable[6][4]		= {
	{ 0, 3, 5, 7 },		{ 1, 2, 3, 0 },
	{ 6, 7, 5, 4 },		{ 4, 2, 1, 6 },
	{ 3, 2, 4, 5 },		{ 1, 0, 7, 6 },
};
//////////////////////////////////////////////////////////////////////////
#define DW_AS_FLT(DW) (*(FLOAT*)&(DW))
#define FLT_AS_DW(F) (*(DWORD*)&(F))
#define FLT_SIGN(F) ((FLT_AS_DW(F) & 0x80000000L))
#define ALMOST_ZERO(F) ((FLT_AS_DW(F) & 0x7f800000L)==0)
#define IS_SPECIAL(F) ((FLT_AS_DW(F) & 0x7f800000L)==0x7f800000L)

//////////////////////////////////////////////////////////////////////////
struct Frustum
{
	Frustum();
	Frustum(const Fmatrix& matrix);

	Fplane		camPlanes[6];
	int			nVertexLUT[6];
	Fvector3	pntList[8];
};
struct BoundingBox
{
	Fvector3 minPt;
	Fvector3 maxPt;

	BoundingBox() { minPt.set(1e33f, 1e33f, 1e33f); maxPt.set(-1e33f, -1e33f, -1e33f); }
	BoundingBox(const BoundingBox& other) { minPt.set(other.minPt); maxPt.set(other.maxPt); }

	explicit BoundingBox( const Fvector3* points, UINT n)
	{
		minPt.set(1e33f, 1e33f, 1e33f); maxPt.set(-1e33f, -1e33f, -1e33f);
		for ( unsigned int i=0; i<n; i++ )
			Merge( points[i] );
	}

	explicit BoundingBox( const std::vector<Fvector3>& points)
	{
		minPt.set(1e33f, 1e33f, 1e33f); maxPt.set(-1e33f, -1e33f, -1e33f);
		for ( unsigned int i=0; i<points.size(); i++ )
			Merge( points[i] );
	}
	explicit BoundingBox( const std::vector<BoundingBox>& boxes )
	{
		minPt.set(1e33f, 1e33f, 1e33f); maxPt.set(-1e33f, -1e33f, -1e33f);
		for (unsigned int i=0; i<boxes.size(); i++) 
		{
			Merge( boxes[i].maxPt );
			Merge( boxes[i].minPt );
		}
	}
	void Centroid( Fvector3& vec) const
	{
		vec.add(minPt, maxPt);
		vec.mul(0.5f);
	}
	void Merge( const Fvector3& vec )
	{
		minPt.x = _min(minPt.x, vec.x);
		minPt.y = _min(minPt.y, vec.y);
		minPt.z = _min(minPt.z, vec.z);
		maxPt.x = _max(maxPt.x, vec.x);
		maxPt.y = _max(maxPt.y, vec.y);
		maxPt.z = _max(maxPt.z, vec.z);
	}
	Fvector3 Point(int i) const { Fvector3 ret; ret.set((i & 1) ? minPt.x : maxPt.x, (i & 2) ? minPt.y : maxPt.y, (i & 4) ? minPt.z : maxPt.z); return ret; }
};

///////////////////////////////////////////////////////////////////////////
//  PlaneIntersection
//    computes the point where three planes intersect
//    returns whether or not the point exists.
static inline BOOL PlaneIntersection(Fvector3& intersectPt, const Fplane p0, const Fplane p1, const Fplane p2)
{
	Fvector3 n0, n1, n2;
	n0.set(p0.n);
	n1.set(p1.n);
	n2.set(p2.n);

	Fvector3 n1_n2, n2_n0, n0_n1;

	n1_n2.crossproduct(n1, n2);
	n2_n0.crossproduct(n2, n0);
	n0_n1.crossproduct(n0, n1);

	float cosTheta = n0.dotproduct(n1_n2);

	if (ALMOST_ZERO(cosTheta) || IS_SPECIAL(cosTheta))
		return FALSE;

	float secTheta = 1.f / cosTheta;

	n1_n2.mul(p0.d);
	n2_n0.mul(p1.d);
	n0_n1.mul(p2.d);

	intersectPt.set(n1_n2);
	intersectPt.add(n2_n0);
	intersectPt.add(n0_n1);
	intersectPt.mul(-secTheta);
	return TRUE;
}

Frustum::Frustum()
{
	for (int i = 0; i<6; i++)
	{
		camPlanes[i].n.set(0.f, 0.f, 0.f);
		camPlanes[i].d = 0.f;
	}
}

//  build a frustum from a camera (projection, or viewProjection) matrix
Frustum::Frustum(const Fmatrix& matrix)
{
	//  build a view frustum based on the current view & projection matrices...
	Fvector4 column1, column2, column3, column4;
	column4.set(matrix._14, matrix._24, matrix._34, matrix._44);
	column1.set(matrix._11, matrix._21, matrix._31, matrix._41);
	column2.set(matrix._12, matrix._22, matrix._32, matrix._42);
	column3.set(matrix._13, matrix._23, matrix._33, matrix._43);

	Fvector4 left, right, bottom, top, nr, fr;
	left.sub	(column4, column1);  // left
	right.add	(column4, column1);  // right
	bottom.sub	(column4, column2);  // bottom
	top.add		(column4, column2);  // top
	nr.sub		(column4, column3);  // near
	fr.add		(column4, column3);  // far

	Fplane planes[6];
	planes[0].set(left);
	planes[1].set(right);
	planes[2].set(bottom);
	planes[3].set(top);
	planes[4].set(nr);
	planes[5].set(fr);
	// ignore near & far plane

	int p;

	for (p = 0; p<6; p++)  // normalize the planes
		planes[p].normalize();

	for (p = 0; p<6; p++)
		camPlanes[p].set(planes[p]);

	//  build a bit-field that will tell us the indices for the nearest and farthest vertices from each plane...
	for (int i = 0; i<6; i++)
		nVertexLUT[i] = ((planes[i].n.x<0.f) ? 1 : 0) | ((planes[i].n.y<0.f) ? 2 : 0) | ((planes[i].n.z<0.f) ? 4 : 0);

	for (int i = 0; i<8; i++)  // compute extrema
	{
		const Fplane& p0 = (i & 1) ? camPlanes[4] : camPlanes[5];
		const Fplane& p1 = (i & 2) ? camPlanes[3] : camPlanes[2];
		const Fplane& p2 = (i & 4) ? camPlanes[0] : camPlanes[1];
		PlaneIntersection(pntList[i], p0, p1, p2);
	}
}


//////////////////////////////////////////////////////////////////////////
// OLES: naive builder of infinite volume expanded from base frustum towards 
//		 light source. really slow, but it works for our simple usage :)
// note: normals points to 'outside'
//////////////////////////////////////////////////////////////////////////
template <bool _debug>
class	DumbConvexVolume
{
public:
	struct	_poly
	{
		xr_vector<int>	points;
		Fvector3		planeN;
		float			planeD;
		float			classify	(Fvector3& p)	{	return planeN.dotproduct(p)+planeD; 	}
	};
	struct	_edge
	{
		int				p0,p1;
		int				counter;
						_edge		(int _p0, int _p1, int m) : p0(_p0), p1(_p1), counter(m){ if (p0>p1)	swap(p0,p1); 	}
		bool			equal		(_edge& E)												{ return p0==E.p0 && p1==E.p1;	}
	};
public:
	xr_vector<Fvector3>		points;
	xr_vector<_poly>		polys;
	xr_vector<_edge>		edges;
public:
	void				compute_planes	()
	{
		for (int it=0; it<int(polys.size()); it++)
		{
			_poly&			P	=	polys[it];
			Fvector3		t1,t2;
			t1.sub					(points[P.points[0]], points[P.points[1]]);
			t2.sub					(points[P.points[0]], points[P.points[2]]);
			P.planeN.crossproduct	(t1,t2).normalize();
			P.planeD			= -	P.planeN.dotproduct(points[P.points[0]]);

			// verify
			if (_debug)
			{
				Fvector&		p0	= points[P.points[0]];
				Fvector&		p1	= points[P.points[1]];
				Fvector&		p2	= points[P.points[2]];
				Fvector&		p3	= points[P.points[3]];
				Fplane	p012;	p012.build(p0,p1,p2);
				Fplane	p123;	p123.build(p1,p2,p3);
				Fplane	p230;	p230.build(p2,p3,p0);
				Fplane	p301;	p301.build(p3,p0,p1);
				VERIFY	(p012.n.similar(p123.n) && p012.n.similar(p230.n) && p012.n.similar(p301.n));
			}
		}
	}
	void				compute_caster_model	(xr_vector<Fplane>& dest, Fvector3 direction)
	{
		CRenderTarget&	T	= *RImplementation.Target;

		// COG
		Fvector3	cog	= {0,0,0};
		for			(int it=0; it<int(points.size()); it++)	cog.add	(points[it]);
		cog.div		(float(points.size()));

		// planes
		compute_planes	();
		for (int it=0; it<int(polys.size()); it++)
		{
			_poly&	base				= polys	[it];
			if (base.classify(cog)>0)	std::reverse(base.points.begin(),base.points.end());
		}

		// remove faceforward polys, build list of edges -> find open ones
		compute_planes	();
		for (int it=0; it<int(polys.size()); it++)
		{
			_poly&	base		= polys	[it];
			VERIFY	(base.classify(cog)<0);								// debug

			int		marker		= (base.planeN.dotproduct(direction)<=0)?-1:1;

			// register edges
			xr_vector<int>&	plist		= polys[it].points;
			for (int p=0; p<int(plist.size()); p++)	{
				_edge	E		(plist[p],plist[ (p+1)%plist.size() ], marker);
				bool	found	= false;
				for (int e=0; e<int(edges.size()); e++)	
					if (edges[e].equal(E))	{ edges[e].counter += marker; found=true; break; }
				if		(!found)	{
					edges.push_back	(E);
					if	(_debug)	T.dbg_addline(points[E.p0],points[E.p1],color_rgba(255,0,0,255));
				}
			}

			// remove if unused
			if (marker<0)	{
				polys.erase	(polys.begin()+it);
				it--;
			}
		}

		// Extend model to infinity, the volume is not capped, so this is indeed up to infinity
		for (int e=0; e<int(edges.size()); e++)
		{
			if	(edges[e].counter != 0)	continue;
			_edge&		E		= edges[e];
			if		(_debug)	T.dbg_addline(points[E.p0],points[E.p1],color_rgba(255,255,255,255));
			Fvector3	point;
			points.push_back	(point.sub(points[E.p0],direction));
			points.push_back	(point.sub(points[E.p1],direction));
			polys.push_back		(_poly());
			_poly&		P		= polys.back();	
			int			pend	= int(points.size());
			P.points.push_back	(E.p0);
			P.points.push_back	(E.p1);
			P.points.push_back	(pend-1);	//p1 mod
			P.points.push_back	(pend-2);	//p0 mod
			if		(_debug)	T.dbg_addline(points[E.p0],point.mad(points[E.p0],direction,-1000),color_rgba(0,255,0,255));
			if		(_debug)	T.dbg_addline(points[E.p1],point.mad(points[E.p1],direction,-1000),color_rgba(0,255,0,255));
		}

		// Reorient planes (try to write more inefficient code :)
		compute_planes	();
		for (int it=0; it<int(polys.size()); it++)
		{
			_poly&	base				= polys	[it];
			if (base.classify(cog)>0)	std::reverse(base.points.begin(),base.points.end());
		}

		// Export
		compute_planes	();
		for (int it=0; it<int(polys.size()); it++)
		{
			_poly&			P	= polys[it];
			Fplane			pp	= {P.planeN,P.planeD};
			dest.push_back	(pp);
		}
	}
};

//////////////////////////////////////////////////////////////////////////
Fvector3		wform	(Fmatrix& m, Fvector3& v)
{
	Fvector4	r;
	r.x			= v.x*m._11 + v.y*m._21 + v.z*m._31 + m._41;
	r.y			= v.x*m._12 + v.y*m._22 + v.z*m._32 + m._42;
	r.z			= v.x*m._13 + v.y*m._23 + v.z*m._33 + m._43;
	r.w			= v.x*m._14 + v.y*m._24 + v.z*m._34 + m._44;
	// VERIFY		(r.w>0.f);
	float invW = 1.0f/r.w;
	Fvector3	r3 = { r.x*invW, r.y*invW, r.z*invW };
	return		r3;
}

//////////////////////////////////////////////////////////////////////////
// OLES: naive 3D clipper - roubustness around 0, but works for this sample
// note: normals points to 'outside'
//////////////////////////////////////////////////////////////////////////
const	float	_eps	= 0.000001f;
struct	DumbClipper
{
	CFrustum				frustum;
	xr_vector<Fplane>		planes;
	BOOL					clip	(Fvector3& p0, Fvector3& p1)		// returns TRUE if result meaningfull
	{
		float		denum;
		Fvector3	D;
		for (int it=0; it<int(planes.size()); it++)
		{
			Fplane&	P			= planes[it];
			float		cls0	= P.classify(p0);
			float		cls1	= P.classify(p1);
			if (cls0>0 && cls1>0)	return			false;	// fully outside

			if (cls0>0)	{
				// clip p0
				D			= p1.sub(p0);
				denum		= P.classify(D) - P.d;
				if (denum!=0) p0.sub(D.mul(cls0 / denum));
			}
			if (cls1>0)	{
				// clip p1
				D			= p0.sub(p1);
				denum		= P.classify(D) - P.d;
				if (denum!=0) p1.sub(D.mul(cls1 / denum));
			}
		}
		return	true;
	}
	Fvector3			point(Fbox& bb, int i) const
	{
		Fvector3 ret;
		ret.set(
			(i & 1) ? bb.min.x : bb.max.x,
			(i & 2) ? bb.min.y : bb.max.y,
			(i & 4) ? bb.min.z : bb.max.z);
		return ret;
	}
	Fbox				clipped_AABB(xr_vector<Fbox,render_alloc<Fbox3> >& src, Fmatrix& xf)
	{
		Fbox3		result;		result.invalidate		();
		for (int it=0; it<int(src.size()); it++)		{
			Fbox&			bb		= src	[it];
			u32				mask	= frustum.getMask	();
			EFC_Visible		res		= frustum.testAABB	(&bb.min.x,mask);
			switch	(res)	
			{
			case fcvNone:	continue;
			case fcvFully:	
				for (int c=0; c<8; c++)
				{
					Fvector3		p0	= point		(bb,c);
					Fvector			x0	= wform		(xf,*((Fvector*)(&p0)));
					result.modify	(x0	);
				}
				break;
			case fcvPartial:
				for (int c0=0; c0<8; c0++)
				{
					for (int c1=0; c1<8; c1++)
					{
						if (c0==c1)			continue;
						Fvector3		p0	= point	(bb,c0);
						Fvector3		p1	= point	(bb,c1);
						if (!clip(p0,p1))	continue;
						Fvector			x0	= wform	(xf,*((Fvector*)(&p0)));
						Fvector			x1	= wform	(xf,*((Fvector*)(&p1)));
						result.modify	(x0	);
						result.modify	(x1	);
					}
				}
				break;
			};
		}
		return			result;
	}
};

template <class _Tp>
inline const _Tp& min(const _Tp& __a, const _Tp& __b) {
	return __b < __a ? __b : __a;
}
template <class _Tp>
inline const _Tp& max(const _Tp& __a, const _Tp& __b) {
	return  __a < __b ? __b : __a;
}

xr_vector<Fbox,render_alloc<Fbox> >	s_casters;

Fvector2 BuildTSMProjectionMatrix_caster_depth_bounds(Fmatrix& lightSpaceBasis)
{
	float		min_z = 1e32f,	max_z=-1e32f;
	Fmatrix	minmax_xf;
	minmax_xf.mul(Device.mView, lightSpaceBasis);
	Fmatrix&	minmax_xform = *((Fmatrix*)&minmax_xf);
	for		(u32 c=0; c<s_casters.size(); c++)
	{
		Fvector3	pt;
		for			(int e=0; e<8; e++)	{
			s_casters[c].getpoint	(e,pt);
			pt		= wform			(minmax_xform, pt);
			min_z	= _min			( min_z, pt.z );
			max_z	= _max			( max_z, pt.z );
		}
	}
	Fvector2 ret;
	ret.set(min_z, max_z);
	return ret;
}

void TransformCoordArray(const Fmatrix& m, Fvector3* pnts, u32 n)
{
	for (int i = 0; i<n; i++)
	{
		Fvector4 v;
		m.transform(v, pnts[i]);
		if (v.w != 0.f)
			pnts[i].set(v.x, v.y, v.z);
		else
			pnts[i].set(0.f, 0.f, 0.f);
	}
}

void CRender::render_sun				()
{
	light*			fuckingsun			= (light*)Lights.sun_adapted._get()	;
	Fmatrix			m_LightViewProj		;

	// calculate view-frustum bounds in world space
	Fmatrix	ex_project, ex_full, ex_full_inverse;
	{
		float _far_	= min(OLES_SUN_LIMIT_27_01_07, g_pGamePersistent->Environment().CurrentEnv->far_plane);
		//ex_project.build_projection	(deg2rad(Device.fFOV/* *Device.fASPECT*/),Device.fASPECT,ps_r2_sun_near,_far_);
		ex_project.build_projection	(deg2rad(Device.fFOV/* *Device.fASPECT*/),Device.fASPECT,VIEWPORT_NEAR,_far_);
		ex_full.mul					(ex_project,Device.mView);
		ex_full_inverse.invert		(ex_full);
	}

	// Compute volume(s) - something like a frustum for infinite directional light
	// Also compute virtual light position and sector it is inside
	CFrustum					cull_frustum	;
	xr_vector<Fplane>			cull_planes		;
	Fvector3					cull_COP		;
	CSector*					cull_sector		;
	Fmatrix						cull_xform		;
	{
		FPU::m64r					();
		// Lets begin from base frustum
		Fmatrix		fullxform_inv	= ex_full_inverse;
		DumbConvexVolume<false>		hull;
		{
			hull.points.reserve		(8);
			for						(int p=0; p<8; p++)	{
				Fvector3				xf	= wform		(fullxform_inv,corners[p]);
				hull.points.push_back	(xf);
			}
			for (int plane=0; plane<6; plane++)	{
				hull.polys.push_back(DumbConvexVolume<false>::_poly());
				for (int pt=0; pt<4; pt++)	
					hull.polys.back().points.push_back(facetable[plane][pt]);
			}
		}
		hull.compute_caster_model	(cull_planes,fuckingsun->direction);

		// Search for default sector - assume "default" or "outdoor" sector is the largest one
		//. hack: need to know real outdoor sector
		CSector*	largest_sector		= 0;
		float		largest_sector_vol	= 0;
		for		(u32 s=0; s<Sectors.size(); s++)
		{
			CSector*			S		= (CSector*)Sectors[s]	;
			dxRender_Visual*		V		= S->root()				;
			float				vol		= V->vis.box.getvolume();
			if (vol>largest_sector_vol)	{
				largest_sector_vol		= vol;
				largest_sector			= S;
			}
		}
		cull_sector					= largest_sector;

		// COP - 100 km away
		cull_COP.mad				(Device.vCameraPosition, fuckingsun->direction, -tweak_COP_initial_offs	);

		// Create frustum for query
		cull_frustum._clear			();
		for (u32 p=0; p<cull_planes.size(); p++)
			cull_frustum._add		(cull_planes[p]);

		// Create approximate ortho-xform
		// view: auto find 'up' and 'right' vectors
		Fmatrix						mdir_View, mdir_Project;
		Fvector						L_dir,L_up,L_right,L_pos;
		L_pos.set					(fuckingsun->position);
		L_dir.set					(fuckingsun->direction).normalize	();
		L_up.set					(0,1,0);					if (_abs(L_up.dotproduct(L_dir))>.99f)	L_up.set(0,0,1);
		L_right.crossproduct		(L_up,L_dir).normalize		();
		L_up.crossproduct			(L_dir,L_right).normalize	();
		mdir_View.build_camera_dir	(L_pos,L_dir,L_up);

		// projection: box
		Fbox	frustum_bb;			frustum_bb.invalidate();
		for (int it=0; it<8; it++)	{
			Fvector	xf	= wform		(mdir_View,hull.points[it]);
			frustum_bb.modify		(xf);
		}
		Fbox&	bb					= frustum_bb;
				bb.grow				(EPS);
		mdir_Project.build_projection_ortho(bb.max.x-bb.min.x, bb.max.y-bb.min.y, bb.min.z - tweak_ortho_xform_initial_offs, bb.max.z);

		// full-xform
		cull_xform.mul				(mdir_Project,mdir_View);
		FPU::m24r					();
	}

	// Begin SMAP-render
	{
		HOM.Disable								();
		phase									= PHASE_SMAP;
		if (RImplementation.o.Tshadows)	r_pmask	(true,true	);
		else							r_pmask	(true,false	);
//		fuckingsun->svis.begin					();
	}

	// Fill the database
	xr_vector<Fbox3,render_alloc<Fbox3> >		&s_receivers = main_coarse_structure;
	s_casters.reserve							(s_receivers.size());
	set_Recorder								(&s_casters);
	r_dsgraph_render_subspace					(cull_sector, &cull_frustum, cull_xform, cull_COP, TRUE);

	// IGNORE PORTALS
	if	(ps_r2_ls_flags.test(R2FLAG_SUN_IGNORE_PORTALS))
	{
		for		(u32 s=0; s<Sectors.size(); s++)
		{
			CSector*			S		= (CSector*)Sectors[s]	;
			dxRender_Visual*		root	= S->root()				;

			set_Frustum			(&cull_frustum);
			add_Geometry		(root);
		}
	}
	set_Recorder						(NULL);

	//	Prepare to interact with D3DX code
	const Fmatrix&	m_View			= Device.mView;
	const Fmatrix&	m_Projection	= ex_project;
	Fvector3		m_lightDir;
	m_lightDir.set(-fuckingsun->direction.x, -fuckingsun->direction.y, -fuckingsun->direction.z);

	//  these are the limits specified by the physical camera
	//  gamma is the "tilt angle" between the light and the view direction.
	float m_fCosGamma = m_lightDir.x * m_View._13	+
						m_lightDir.y * m_View._23	+
						m_lightDir.z * m_View._33	;
	float m_fTSM_Delta= ps_r2_sun_tsm_projection;

	// Compute REAL sheared xform based on receivers/casters information
	FPU::m64r			();
	if	( _abs(m_fCosGamma) < 0.99f && ps_r2_ls_flags.test(R2FLAG_SUN_TSM))
	{
		//  get the near and the far plane (points) in eye space.
		Fvector3 frustumPnts[8];

		Frustum eyeFrustum(m_Projection);  // autocomputes all the extrema points

		for ( int i=0; i<4; i++ )
		{
			frustumPnts[i]   = eyeFrustum.pntList[(i<<1)];       // far plane
			frustumPnts[i+4] = eyeFrustum.pntList[(i<<1) | 0x1]; // near plane
		}

		//   we need to transform the eye into the light's post-projective space.
		//   however, the sun is a directional light, so we first need to find an appropriate
		//   rotate/translate matrix, before constructing an ortho projection.
		//   this matrix is a variant of "light space" from LSPSMs, with the Y and Z axes permuted

		Fvector3			leftVector, upVector, viewVector, eyeVector;
		eyeVector.set( 0.f, 0.f, -1.f );  //  eye is always -Z in eye space

		//  code copied straight from BuildLSPSMProjectionMatrix
		m_View.transform(upVector, m_lightDir);  // lightDir is defined in eye space, so xform it
		leftVector.crossproduct(upVector, eyeVector);
		leftVector.normalize();
		viewVector.crossproduct(upVector, leftVector);

		Fmatrix lightSpaceBasis;  
		lightSpaceBasis._11 = leftVector.x; lightSpaceBasis._12 = viewVector.x; lightSpaceBasis._13 = -upVector.x; lightSpaceBasis._14 = 0.f;
		lightSpaceBasis._21 = leftVector.y; lightSpaceBasis._22 = viewVector.y; lightSpaceBasis._23 = -upVector.y; lightSpaceBasis._24 = 0.f;
		lightSpaceBasis._31 = leftVector.z; lightSpaceBasis._32 = viewVector.z; lightSpaceBasis._33 = -upVector.z; lightSpaceBasis._34 = 0.f;
		lightSpaceBasis._41 = 0.f;          lightSpaceBasis._42 = 0.f;          lightSpaceBasis._43 = 0.f;        lightSpaceBasis._44 = 1.f;

		//  rotate the view frustum into light space
		TransformCoordArray(lightSpaceBasis, frustumPnts, 8);

		//  build an off-center ortho projection that translates and scales the eye frustum's 3D AABB to the unit cube
		BoundingBox frustumBox	( frustumPnts, 8 );

		//  also - transform the shadow caster bounding boxes into light projective space.  we want to translate along the Z axis so that
		//  all shadow casters are in front of the near plane.
		Fvector2	depthbounds = BuildTSMProjectionMatrix_caster_depth_bounds	(lightSpaceBasis);

		float min_z = min( depthbounds.x, frustumBox.minPt.z );
		float max_z = max( depthbounds.y, frustumBox.maxPt.z );

		if ( min_z <= 1.f )	//?
		{
			Fmatrix lightSpaceTranslate;
			lightSpaceTranslate.translate(0.f, 0.f, -min_z + 1.f);
			max_z = -min_z + max_z + 1.f;
			min_z = 1.f;
			lightSpaceBasis.mul(lightSpaceBasis, lightSpaceTranslate);
			TransformCoordArray(lightSpaceTranslate, frustumPnts, 8);
			frustumBox = BoundingBox( frustumPnts, 8 );
		}

		Fmatrix lightSpaceOrtho;
		lightSpaceOrtho.build_projection_ortho(frustumBox.maxPt.x - frustumBox.minPt.x, frustumBox.maxPt.y - frustumBox.minPt.y, min_z, max_z);

		//  transform the view frustum by the new matrix
		TransformCoordArray(lightSpaceOrtho, frustumPnts, 8);

		Fvector2 centerPts	[2];
		//  near plane
		centerPts[0].x = 0.25f * (frustumPnts[4].x + frustumPnts[5].x + frustumPnts[6].x + frustumPnts[7].x);
		centerPts[0].y = 0.25f * (frustumPnts[4].y + frustumPnts[5].y + frustumPnts[6].y + frustumPnts[7].y);
		//  far plane
		centerPts[1].x = 0.25f * (frustumPnts[0].x + frustumPnts[1].x + frustumPnts[2].x + frustumPnts[3].x);
		centerPts[1].y = 0.25f * (frustumPnts[0].y + frustumPnts[1].y + frustumPnts[2].y + frustumPnts[3].y);

		Fvector2 centerOrig;
		centerOrig.add(centerPts[0], centerPts[1]);
		centerOrig.mul(0.5f);

		Fmatrix trapezoid_space;

		Fmatrix xlate_center;
		xlate_center.translate(-centerOrig.x, -centerOrig.y, 0.f );

		Fvector2	center_dirl;
		center_dirl.sub(centerPts[1], centerOrig);
		float half_center_len = center_dirl.magnitude();
		float x_len = centerPts[1].x - centerOrig.x;
		float y_len = centerPts[1].y - centerOrig.y;

		float cos_theta = x_len / half_center_len;
		float sin_theta = y_len / half_center_len;

		Fmatrix rot_center = {
			cos_theta, -sin_theta, 0.f, 0.f,
			sin_theta, cos_theta, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f };

		//  this matrix transforms the center line to y=0.
		//  since Top and Base are orthogonal to Center, we can skip computing the convex hull, and instead
		//  just find the view frustum X-axis extrema.  The most negative is Top, the most positive is Base
		//  Point Q (trapezoid projection point) will be a point on the y=0 line.
		trapezoid_space.mul(xlate_center, rot_center);
		TransformCoordArray(trapezoid_space, frustumPnts, 8);

		BoundingBox frustumAABB2D( frustumPnts, 8 );

		float x_scale = max( _abs(frustumAABB2D.maxPt.x), _abs(frustumAABB2D.minPt.x) );
		float y_scale = max( _abs(frustumAABB2D.maxPt.y), _abs(frustumAABB2D.minPt.y) );
		x_scale = 1.f/x_scale;
		y_scale = 1.f/y_scale;

		//  maximize the area occupied by the bounding box
		Fmatrix scale_center = {
			x_scale, 0.f, 0.f, 0.f,
			0.f, y_scale, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f };

		trapezoid_space.mul(trapezoid_space, scale_center);

		//  scale the frustum AABB up by these amounts (keep all values in the same space)
		frustumAABB2D.minPt.x *= x_scale;
		frustumAABB2D.maxPt.x *= x_scale;
		frustumAABB2D.minPt.y *= y_scale;
		frustumAABB2D.maxPt.y *= y_scale;

		//  compute eta.
		float lambda		= frustumAABB2D.maxPt.x - frustumAABB2D.minPt.x;
		float delta_proj	= m_fTSM_Delta * lambda;	//focusPt.x - frustumAABB2D.minPt.x;
		const float xi		= -0.6f;					// - 0.6f;  // 80% line
		float		eta		= (lambda*delta_proj*(1.f+xi)) / (lambda*(1.f-xi)-2.f*delta_proj);

		//  compute the projection point a distance eta from the top line.  this point is on the center line, y=0
		Fvector2 projectionPtQ;
		projectionPtQ.set(frustumAABB2D.maxPt.x + eta, 0.f);

		//  find the maximum slope from the projection point to any point in the frustum.  this will be the
		//  projection field-of-view
		float max_slope = -1e32f;
		float min_slope =  1e32f;

		for ( int i=0; i < 8; i++ )
		{
			Fvector2 tmp;
			tmp.set(frustumPnts[i].x*x_scale, frustumPnts[i].y*y_scale);
			float x_dist = tmp.x - projectionPtQ.x;
			if ( !(ALMOST_ZERO(tmp.y) || ALMOST_ZERO(x_dist)))
			{
				max_slope = max(max_slope, tmp.y/x_dist);
				min_slope = min(min_slope, tmp.y/x_dist);
			}
		}

		float xn = eta;
		float xf = lambda + eta;

		Fmatrix ptQ_xlate = {
			-1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			projectionPtQ.x, 0.f, 0.f, 1.f };
		trapezoid_space.mul(trapezoid_space, ptQ_xlate);

		//  this shear balances the "trapezoid" around the y=0 axis (no change to the projection pt position)
		//  since we are redistributing the trapezoid, this affects the projection field of view (shear_amt)
		float shear_amt = (max_slope + _abs(min_slope))*0.5f - max_slope;
		max_slope = max_slope + shear_amt;

		Fmatrix trapezoid_shear = {
			1.f, shear_amt, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f };
		trapezoid_space.mul(trapezoid_space, trapezoid_shear);


		float z_aspect = (frustumBox.maxPt.z-frustumBox.minPt.z) / (frustumAABB2D.maxPt.y-frustumAABB2D.minPt.y);

		//  perform a 2DH projection to 'unsqueeze' the top line.
		Fmatrix trapezoid_projection = {
			xf / (xf - xn), 0.f, 0.f, 1.f,
			0.f, 1.f / max_slope, 0.f, 0.f,
			0.f, 0.f, 1.f / (z_aspect*max_slope), 0.f,
			-xn*xf / (xf - xn), 0.f, 0.f, 0.f };

		trapezoid_space.mul(trapezoid_space, trapezoid_projection);

		//  the x axis is compressed to [0..1] as a result of the projection, so expand it to [-1,1]
		Fmatrix biasedScaleX = {
			2.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			-1.f, 0.f, 0.f, 1.f };
		trapezoid_space.mul(trapezoid_space, biasedScaleX);

		m_LightViewProj.mul(m_View,				lightSpaceBasis);
		m_LightViewProj.mul(m_LightViewProj,	lightSpaceOrtho);
		m_LightViewProj.mul(m_LightViewProj,	trapezoid_space);
	} else {
		m_LightViewProj				= cull_xform;
	}
	FPU::m24r				();

	// perform "refit" or "focusing" on relevant
	if	(ps_r2_ls_flags.test(R2FLAG_SUN_FOCUS))
	{
		FPU::m64r				();

		// create clipper
		DumbClipper	view_clipper;
		Fmatrix&	xform		= m_LightViewProj;
		view_clipper.frustum.CreateFromMatrix(ex_full,FRUSTUM_P_ALL);
		for		(int p=0; p<view_clipper.frustum.p_count; p++)
		{
			Fplane&		P	= view_clipper.frustum.planes	[p];
			view_clipper.planes.push_back(P);
		}

		// 
		Fbox3		b_casters, b_receivers;
		Fvector3	pt			;

		// casters
		b_casters.invalidate	();
		for		(u32 c=0; c<s_casters.size(); c++)		{
			for		(int e=0; e<8; e++)
			{
				s_casters[c].getpoint	(e,pt);
				pt				= wform	(xform,pt);
				b_casters.modify		(pt);
			}
		}

		// receivers
		b_receivers.invalidate	();
		b_receivers		= view_clipper.clipped_AABB	(s_receivers,xform);
		Fmatrix	x_project, x_full, x_full_inverse;
		{
			x_project.build_projection	(deg2rad(Device.fFOV/* *Device.fASPECT*/),Device.fASPECT,ps_r2_sun_near,ps_r2_sun_near+tweak_guaranteed_range);
			x_project.build_projection	(deg2rad(Device.fFOV/* *Device.fASPECT*/),Device.fASPECT,VIEWPORT_NEAR,ps_r2_sun_near+tweak_guaranteed_range);
			x_full.mul					(x_project,Device.mView);
			x_full_inverse.invert		(x_full);
		}
		for		(int e=0; e<8; e++)
		{
			pt				= wform	(x_full_inverse,corners[e]);	// world space
			pt				= wform	(xform,pt);						// trapezoid space
			b_receivers.modify		(pt);
		}

		// some tweaking
		b_casters.grow				(EPS);
		b_receivers.grow			(EPS);

		// because caster points are from coarse representation only allow to "shrink" box, not grow
		// that is the same as if we first clip casters by frustum
		if (b_receivers.min.x<-1)	b_receivers.min.x	=-1;
		if (b_receivers.min.y<-1)	b_receivers.min.y	=-1;
		if (b_casters.min.z<0)		b_casters.min.z		=0;
		if (b_receivers.max.x>+1)	b_receivers.max.x	=+1;
		if (b_receivers.max.y>+1)	b_receivers.max.y	=+1;
		if (b_casters.max.z>+1)		b_casters.max.z		=+1;

		// refit?
		/*
		const float EPS				= 0.001f;
		D3DXMATRIX					refit;
		D3DXMatrixOrthoOffCenterLH	( &refit, b_receivers.min.x, b_receivers.max.x, b_receivers.min.y, b_receivers.max.y, b_casters.min.z-EPS, b_casters.max.z+EPS );
		D3DXMatrixMultiply			( &m_LightViewProj, &m_LightViewProj, &refit);
		*/

		float boxWidth  = b_receivers.max.x - b_receivers.min.x;
		float boxHeight = b_receivers.max.y - b_receivers.min.y;
		//  the divide by two's cancel out in the translation, but included for clarity
		float boxX		= (b_receivers.max.x+b_receivers.min.x) / 2.f;
		float boxY		= (b_receivers.max.y+b_receivers.min.y) / 2.f;
		Fmatrix trapezoidUnitCube	=	{ 2.f/boxWidth,			0.f,					0.f, 0.f,
													0.f,		2.f/boxHeight,			0.f, 0.f,
													0.f,		0.f,					1.f, 0.f,
										-2.f*boxX/boxWidth,		-2.f*boxY/boxHeight,	0.f, 1.f };
		m_LightViewProj.mul(m_LightViewProj, trapezoidUnitCube);
		//D3DXMatrixMultiply( &trapezoid_space, &trapezoid_space, &trapezoidUnitCube );
		FPU::m24r					();
	}

	// Finalize & Cleanup
	fuckingsun->X.D.combine			= *((Fmatrix*)&m_LightViewProj);
	s_receivers.clear				();
	s_casters.clear					();

	// Render shadow-map
	//. !!! We should clip based on shrinked frustum (again)
	{
		bool	bNormal							= mapNormalPasses[0][0].size() || mapMatrixPasses[0][0].size();
		bool	bSpecial						= mapNormalPasses[1][0].size() || mapMatrixPasses[1][0].size() || mapSorted.size();
		if ( bNormal || bSpecial)	{
			Target->phase_smap_direct			(fuckingsun, SE_SUN_FAR		);
			RCache.set_xform_world				(Fidentity					);
			RCache.set_xform_view				(Fidentity					);
			RCache.set_xform_project			(fuckingsun->X.D.combine	);	
			r_dsgraph_render_graph				(0);
			fuckingsun->X.D.transluent			= FALSE;
			if (bSpecial)						{
				fuckingsun->X.D.transluent			= TRUE;
				Target->phase_smap_direct_tsh		(fuckingsun, SE_SUN_FAR	);
				r_dsgraph_render_graph				(1);			// normal level, secondary priority
				r_dsgraph_render_sorted				( );			// strict-sorted geoms
			}
		}
	}

	// End SMAP-render
	{
//		fuckingsun->svis.end					();
		r_pmask									(true,false);
	}

	// Accumulate
	Target->phase_accumulator	();
	Target->accum_direct		(SE_SUN_FAR);

	// Restore XForms
	RCache.set_xform_world		(Fidentity			);
	RCache.set_xform_view		(Device.mView		);
	RCache.set_xform_project	(Device.mProject	);
}

void CRender::render_sun_near	()
{
	light*			fuckingsun			= (light*)Lights.sun_adapted._get()	;
	Fmatrix			m_LightViewProj		;

	// calculate view-frustum bounds in world space
	Fmatrix	ex_project, ex_full, ex_full_inverse;
	{
		ex_project.build_projection	(deg2rad(Device.fFOV/* *Device.fASPECT*/),Device.fASPECT,VIEWPORT_NEAR,ps_r2_sun_near); 
		ex_full.mul					(ex_project,Device.mView);
		ex_full_inverse.invert		(ex_full);
	}

	// Compute volume(s) - something like a frustum for infinite directional light
	// Also compute virtual light position and sector it is inside
	CFrustum					cull_frustum;
	xr_vector<Fplane>			cull_planes;
	Fvector3					cull_COP;
	CSector*					cull_sector;
	Fmatrix						cull_xform;
	{
		FPU::m64r					();
		// Lets begin from base frustum
		Fmatrix		fullxform_inv	= ex_full_inverse;
#ifdef	_DEBUG
		typedef		DumbConvexVolume<true>	t_volume;
#else
		typedef		DumbConvexVolume<false>	t_volume;
#endif
		t_volume					hull;
		{
			hull.points.reserve		(9);
			for	(int p=0; p<8; p++)	{
				Fvector3				xf	= wform		(fullxform_inv,corners[p]);
				hull.points.push_back	(xf);
			}
			for (int plane=0; plane<6; plane++)	{
				hull.polys.push_back(t_volume::_poly());
				for (int pt=0; pt<4; pt++)	
					hull.polys.back().points.push_back(facetable[plane][pt]);
			}
		}
		hull.compute_caster_model	(cull_planes,fuckingsun->direction);
#ifdef	_DEBUG
		for (u32 it=0; it<cull_planes.size(); it++)
			RImplementation.Target->dbg_addplane(cull_planes[it],0xffffffff);
#endif

		// Search for default sector - assume "default" or "outdoor" sector is the largest one
		//. hack: need to know real outdoor sector
		CSector*	largest_sector		= 0;
		float		largest_sector_vol	= 0;
		for		(u32 s=0; s<Sectors.size(); s++)
		{
			CSector*			S		= (CSector*)Sectors[s]	;
			dxRender_Visual*		V		= S->root()				;
			float				vol		= V->vis.box.getvolume();
			if (vol>largest_sector_vol)	{
				largest_sector_vol		= vol;
				largest_sector			= S;
			}
		}
		cull_sector	= largest_sector;

		// COP - 100 km away
		cull_COP.mad				(Device.vCameraPosition, fuckingsun->direction, -tweak_COP_initial_offs	);

		// Create frustum for query
		cull_frustum._clear			();
		for (u32 p=0; p<cull_planes.size(); p++)
			cull_frustum._add		(cull_planes[p]);

		// Create approximate ortho-xform
		// view: auto find 'up' and 'right' vectors
		Fmatrix						mdir_View, mdir_Project;
		Fvector						L_dir,L_up,L_right,L_pos;
		L_pos.set					(fuckingsun->position);
		L_dir.set					(fuckingsun->direction).normalize	();
		L_right.set					(1,0,0);					if (_abs(L_right.dotproduct(L_dir))>.99f)	L_right.set(0,0,1);
		L_up.crossproduct			(L_dir,L_right).normalize	();
		L_right.crossproduct		(L_up,L_dir).normalize		();
		mdir_View.build_camera_dir	(L_pos,L_dir,L_up);

		// projection: box
		/*
		//	Original
		float	_D					= ps_r2_sun_near;
		float	a0					= deg2rad(Device.fFOV*Device.fASPECT)/2.f;
		float	a1					= deg2rad(Device.fFOV)/2.f;
		float	c0					= _D/_cos(a0);
		float	c1					= _D/_cos(a1);
		float	k0					= 2.f*c0*_sin(a0);
		float	k1					= 2.f*c1*_sin(a1);
		float	borderalpha			= (Device.fFOV-10) / (90-10);

		float	nearborder			= 1*borderalpha + 1.136363636364f*(1-borderalpha);
		float	spherical_range		= ps_r2_sun_near_border * nearborder * _max(_max(c0,c1), _max(k0,k1)*1.414213562373f );
		Fbox	frustum_bb;			frustum_bb.invalidate	();
		hull.points.push_back		(Device.vCameraPosition);
		for (int it=0; it<9; it++)	{
		Fvector	xf	= wform		(mdir_View,hull.points[it]);
		frustum_bb.modify		(xf);
		}
		float	size_x				= frustum_bb.max.x - frustum_bb.min.x;
		float	size_y				= frustum_bb.max.y - frustum_bb.min.y;
		float	diff_x				= (spherical_range - size_x)/2.f;	//VERIFY(diff_x>=0);
		float	diff_y				= (spherical_range - size_y)/2.f;	//VERIFY(diff_y>=0);
		frustum_bb.min.x -= diff_x; frustum_bb.max.x += diff_x;
		frustum_bb.min.y -= diff_y; frustum_bb.max.y += diff_y;
		Fbox&	bb					= frustum_bb;
		D3DXMatrixOrthoOffCenterLH	((D3DXMATRIX*)&mdir_Project,bb.min.x,bb.max.x,  bb.min.y,bb.max.y,  bb.min.z-tweak_ortho_xform_initial_offs,bb.max.z);
		/**/

		//	Simple
		Fbox	frustum_bb;			frustum_bb.invalidate();
		for (int it=0; it<8; it++)	{
			//for (int it=0; it<9; it++)	{
			Fvector	xf	= wform		(mdir_View,hull.points[it]);
			frustum_bb.modify		(xf);
		}
		Fbox&	bb					= frustum_bb;
		bb.grow				(EPS);
		mdir_Project.build_projection_ortho(bb.max.x-bb.min.x, bb.max.y-bb.min.y, bb.min.z-tweak_ortho_xform_initial_offs,bb.max.z);
		/**/

		// build viewport xform
		float	view_dim			= float(RImplementation.o.smapsize);
		Fmatrix	m_viewport			= {
			view_dim/2.f,	0.0f,				0.0f,		0.0f,
			0.0f,			-view_dim/2.f,		0.0f,		0.0f,
			0.0f,			0.0f,				1.0f,		0.0f,
			view_dim/2.f,	view_dim/2.f,		0.0f,		1.0f
		};
		Fmatrix				m_viewport_inv;
		m_viewport_inv.invert(m_viewport);

		// snap view-position to pixel
		cull_xform.mul		(mdir_Project,mdir_View	);
		Fvector cam_proj	= wform		(cull_xform,Device.vCameraPosition	);
		Fvector	cam_pixel	= wform		(m_viewport,cam_proj				);
		cam_pixel.x			= floorf	(cam_pixel.x);
		cam_pixel.y			= floorf	(cam_pixel.y);
		Fvector cam_snapped	= wform		(m_viewport_inv,cam_pixel);
		Fvector diff;		diff.sub	(cam_snapped,cam_proj				);
		Fmatrix adjust;		adjust.translate(diff);
		cull_xform.mulA_44	(adjust);

		// calculate scissor
		Fbox		scissor				;	scissor.invalidate();
		Fmatrix		scissor_xf			;
					scissor_xf.mul		(m_viewport,cull_xform);
		for (int it=0; it<9; it++)	{
			Fvector	xf	= wform		(scissor_xf,hull.points[it]);
			scissor.modify			(xf);
		}
		s32		limit					= RImplementation.o.smapsize-1;
		fuckingsun->X.D.minX			= clampr	(iFloor	(scissor.min.x), 0, limit);
		fuckingsun->X.D.maxX			= clampr	(iCeil	(scissor.max.x), 0, limit);
		fuckingsun->X.D.minY			= clampr	(iFloor	(scissor.min.y), 0, limit);
		fuckingsun->X.D.maxY			= clampr	(iCeil	(scissor.max.y), 0, limit);

		// full-xform
		FPU::m24r			();
	}

	// Begin SMAP-render
	{
		bool	bSpecialFull					= mapNormalPasses[1][0].size() || mapMatrixPasses[1][0].size() || mapSorted.size();
		VERIFY									(!bSpecialFull);
		HOM.Disable								();
		phase									= PHASE_SMAP;
		if (RImplementation.o.Tshadows)	r_pmask	(true,true	);
		else							r_pmask	(true,false	);
//		fuckingsun->svis.begin					();
	}

	// Fill the database
	r_dsgraph_render_subspace				(cull_sector, &cull_frustum, cull_xform, cull_COP, TRUE);

	// Finalize & Cleanup
	fuckingsun->X.D.combine					= cull_xform;	//*((Fmatrix*)&m_LightViewProj);

	// Render shadow-map
	//. !!! We should clip based on shrinked frustum (again)
	{
		bool	bNormal							= mapNormalPasses[0][0].size() || mapMatrixPasses[0][0].size();
		bool	bSpecial						= mapNormalPasses[1][0].size() || mapMatrixPasses[1][0].size() || mapSorted.size();
		if ( bNormal || bSpecial)	{
			Target->phase_smap_direct			(fuckingsun	, SE_SUN_NEAR	);
			RCache.set_xform_world				(Fidentity					);
			RCache.set_xform_view				(Fidentity					);
			RCache.set_xform_project			(fuckingsun->X.D.combine	);	
			r_dsgraph_render_graph				(0)	;
			if (ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS))	
				Details->Render					()	;
			fuckingsun->X.D.transluent			= FALSE;
			if (bSpecial)						{
				fuckingsun->X.D.transluent			= TRUE;
				Target->phase_smap_direct_tsh		(fuckingsun, SE_SUN_NEAR);
				r_dsgraph_render_graph				(1);			// normal level, secondary priority
				r_dsgraph_render_sorted				( );			// strict-sorted geoms
			}
		}
	}

	// End SMAP-render
	{
//		fuckingsun->svis.end					();
		r_pmask									(true,false);
	}

	// Accumulate
	Target->phase_accumulator	();
	Target->accum_direct		(SE_SUN_NEAR);

	// Restore XForms
	RCache.set_xform_world		(Fidentity			);
	RCache.set_xform_view		(Device.mView		);
	RCache.set_xform_project	(Device.mProject	);
}

void CRender::render_sun_filtered	()
{
	if (!RImplementation.o.sunfilter)	return;
	Target->phase_accumulator			();
	Target->accum_direct				(SE_SUN_LUMINANCE);
}