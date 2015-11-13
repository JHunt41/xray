#include "common.h"
#include "shared\waterconfig.h"
#include "shared\watermove.h"
#include "iostructs\v_water.h"

uniform float4x4	m_texgen;

vf _main (v_vert v)
{
	v.N		=	unpack_D3DCOLOR(v.N);
	v.T		=	unpack_D3DCOLOR(v.T);
	v.B		=	unpack_D3DCOLOR(v.B);
	v.color	=	unpack_D3DCOLOR(v.color);
	vf 		o;

	float4 	P 	= v.P;
	float3 	N 	= unpack_normal		(v.N);
		P 	= watermove		(P);

	o.tbase		= unpack_tc_base	(v.uv,v.T.w,v.B.w);		// copy tc
	o.tnorm0	= watermove_tc 		(o.tbase*W_DISTORT_BASE_TILE_0, P.xz, W_DISTORT_AMP_0);
	o.tnorm1	= watermove_tc 		(o.tbase*W_DISTORT_BASE_TILE_1, P.xz, W_DISTORT_AMP_1);
	o.hpos 		= mul			(m_VP, P);			// xform, input in world coords

//	Igor: for additional depth dest
#ifdef	USE_SOFT_WATER
#ifdef	NEED_SOFT_WATER
	o.tctexgen = mul( m_texgen, P);
	float3	Pe	= mul		(m_V,  P);
	o.tctexgen.z = Pe.z;
#endif	//	USE_SOFT_WATER
#endif	//	NEED_SOFT_WATER

	return o;
}
