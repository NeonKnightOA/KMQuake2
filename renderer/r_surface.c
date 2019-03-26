/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// r_surface.c: surface-related rendering code

#include <assert.h>

#include "r_local.h"

static vec3_t	modelorg;		// relative to viewpoint

msurface_t	*r_alpha_surfaces;

int		c_visible_lightmaps;
int		c_visible_textures;


gllightmapstate_t gl_lms;

static void RB_DrawEnvMap (void);
static void RB_DrawTexGlow (image_t *glowImage);
static void RB_DrawCaustics (msurface_t *surf);
static void R_DrawLightmappedSurface (msurface_t *surf, qboolean render);

static void		LM_InitBlock (void);
static void		LM_UploadBlock (qboolean dynamic);
static qboolean	LM_AllocBlock (int w, int h, int *x, int *y);

extern void R_SetCacheState( msurface_t *surf );
extern void R_BuildLightMap (msurface_t *surf, byte *dest, int stride);

void R_BuildVertexLight (msurface_t *surf);

#ifdef BATCH_LM_UPDATES
void R_UpdateSurfaceLightmap (msurface_t *surf);
void R_RebuildLightmaps (void);
#endif

// render lightmapped surfaces from texture chains
#define MULTITEXTURE_CHAINS

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
===============
R_GetTextureAnimationFrame

Returns the proper texture for a given time.
Uses msurface_t entity pointer, since currententity is not valid for the alpha surface pass.
===============
*/
mtexinfo_t *R_GetTextureAnimationFrame(msurface_t *surf) //mxd
{
	mtexinfo_t *tex = surf->texinfo;

	if (!tex->next)
		return tex;

	int frame;
	if (tex->flags & (SURF_TRANS33 | SURF_TRANS66))
		frame = (surf->entity ? surf->entity->frame : r_worldframe); // use worldspawn frame when no entity is set
	else
		frame = currententity->frame;

	for (int c = frame % tex->numframes; c > 0; c--)
		tex = tex->next;

	return tex;
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time.
===============
*/
image_t *R_TextureAnimation (msurface_t *surf)
{
	mtexinfo_t *tex = R_GetTextureAnimationFrame(surf); //mxd
	return tex->image;
}


/*
===============
R_TextureAnimationGlow

Returns the proper glow texture for a given time
===============
*/
image_t *R_TextureAnimationGlow (msurface_t *surf)
{
	mtexinfo_t *tex = R_GetTextureAnimationFrame(surf); //mxd
	return tex->glow;
}


/*
===============
R_SetLightingMode
===============
*/
void R_SetLightingMode (int renderflags)
{
	GL_SelectTexture(0);

	if (!glConfig.mtexcombine)// || (renderflags & RF_TRANSLUCENT)) 
	{
		GL_SelectTexture(0);
		GL_TexEnv(GL_REPLACE);
		GL_SelectTexture(1);
		GL_TexEnv(r_lightmap->value ? GL_REPLACE : GL_MODULATE);
	}
	else 
	{
		GL_SelectTexture(0);
		GL_TexEnv(GL_COMBINE_ARB);

		const GLint texenvmode = (renderflags & RF_TRANSLUCENT ? GL_MODULATE : GL_REPLACE); //mxd
		qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, texenvmode);
		qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
		qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, texenvmode);
		qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);

		GL_SelectTexture(1);
		GL_TexEnv(GL_COMBINE_ARB);
		if (r_lightmap->value) 
		{
			qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_REPLACE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);
		} 
		else 
		{
			qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB);

			qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB);
		}

		if (r_rgbscale->value)
			qglTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, r_rgbscale->value);
	}
}


/*
================
SurfAlphaCalc
================
*/
float SurfAlphaCalc (int flags)
{
	if ((flags & SURF_TRANS33) && (flags & SURF_TRANS66) && r_solidalpha->value)
		return 1.0;

	if (flags & SURF_TRANS33)
		return 0.33333;

	if (flags & SURF_TRANS66)
		return 0.66666;

	return 1.0;
}

/*
================
R_SurfIsDynamic
================
*/
qboolean R_SurfIsDynamic(msurface_t *surf, int *mapNum)
{
	int			map;
	qboolean	is_dynamic = false;

	if (!surf || r_fullbright->value != 0)
		return false;

	qboolean cached_dlight = false; //mxd
#ifdef BATCH_LM_UPDATES
	cached_dlight = surf->cached_dlight;
#endif

	for (map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
	{
		if (r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map])
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if (surf->dlightframe == r_framecount || cached_dlight)
	{
	dynamic:
		if (r_dynamic->value || cached_dlight)
		{
			if (!(surf->texinfo->flags & (SURF_SKY | SURF_WARP | SURF_NOLIGHTENV)))
				is_dynamic = true;
		}
	}

	if (mapNum)
		*mapNum = map;

	return is_dynamic;
}


/*
================
R_SurfIsLit
================
*/
qboolean R_SurfIsLit (msurface_t *s)
{
	if (!s || !s->texinfo || r_fullbright->value)
		return false;

	const qboolean istranslucent = (s->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66)) && !(s->texinfo->flags & SURF_NOLIGHTENV); //mxd
	return istranslucent && (s->flags & SURF_DRAWTURB ? r_warp_lighting->value : r_trans_lighting->value);
}

/*
================
R_SurfHasEnvMap
================
*/
qboolean R_SurfHasEnvMap (msurface_t *s)
{
	if (!s || !s->texinfo)
		return false;

	const qboolean solidAlpha = ((s->texinfo->flags & SURF_TRANS33) && (s->texinfo->flags & SURF_TRANS66) && r_solidalpha->value);
	return (s->flags & SURF_ENVMAP) && r_glass_envmaps->value && !solidAlpha;
}


/*
================
RB_RenderGLPoly

backend for R_DrawGLPoly
================
*/
void RB_RenderGLPoly (msurface_t *surf, qboolean light)
{
	image_t		*image = R_TextureAnimation(surf);
	image_t		*glow = R_TextureAnimationGlow(surf);
	const float	alpha = colorArray[0][3];

	if (rb_vertex == 0 || rb_index == 0) // nothing to render
		return;

	const qboolean glowPass = (r_glows->value && (glow != glMedia.notexture) && glConfig.multitexture && light);
	const qboolean envMap = R_SurfHasEnvMap(surf);
	const qboolean causticPass = (r_caustics->value && (surf->flags & SURF_MASK_CAUSTIC) && glConfig.multitexture && light);

	c_brush_calls++;

	GL_Bind(image->texnum);

	if (light)
	{
		R_SetVertexRGBScale(true);
		GL_ShadeModel(GL_SMOOTH);
	}

	RB_DrawArrays();

	if (glowPass)
	{
		// just redraw with existing arrays for glow
		qglDisableClientState(GL_COLOR_ARRAY);
		qglColor4f(1.0, 1.0, 1.0, alpha);
		RB_DrawTexGlow(glow);
		qglColor4f(1.0, 1.0, 1.0, 1.0);
		qglEnableClientState(GL_COLOR_ARRAY);
	}

	if (envMap && !causticPass)
	{
		// vertex-lit trans surfaces have more solid envmapping
		const float envAlpha = (r_trans_lighting->value && !(surf->texinfo->flags & SURF_NOLIGHTENV) ? 0.15 : 0.1);
		for (int i = 0; i < rb_vertex; i++) 
			colorArray[i][3] = envAlpha;

		RB_DrawEnvMap();

		for (int i = 0; i < rb_vertex; i++) 
			colorArray[i][3] = alpha;
	}

	if (causticPass) // Barnes caustics
		RB_DrawCaustics(surf);

	if (light)
	{
		R_SetVertexRGBScale(false);
		GL_ShadeModel(GL_FLAT);
	}

	RB_DrawMeshTris();
	rb_vertex = rb_index = 0;
}


/*
================
R_DrawGLPoly
modified to handle scrolling textures
================
*/
void R_DrawGLPoly (msurface_t *surf, qboolean render)
{
	const float alpha = SurfAlphaCalc(surf->texinfo->flags);
	const qboolean light = R_SurfIsLit(surf);

	c_brush_surfs++;

	float scroll;
	if (surf->texinfo->flags & SURF_FLOWING)
	{
		scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
		if (scroll == 0.0)	scroll = -64.0;
	}
	else
	{
		scroll = 0.0;
	}

//	rb_vertex = rb_index = 0;
	for (glpoly_t *p = surf->polys; p; p = p->chain)
	{
		const int nv = p->numverts;
		c_brush_polys += nv - 2;
		float *v = p->verts[0];

		if (RB_CheckArrayOverflow(nv, (nv - 2) * 3))
			RB_RenderGLPoly(surf, light);

		for (int i = 0; i < nv - 2; i++)
		{
			indexArray[rb_index++] = rb_vertex;
			indexArray[rb_index++] = rb_vertex + i + 1;
			indexArray[rb_index++] = rb_vertex + i + 2;
		}

		for (int i = 0; i < nv; i++, v += VERTEXSIZE)
		{
			if (light && p->vertexlight && p->vertexlightset)
				VA_SetElem4(colorArray[rb_vertex],
					(float)(p->vertexlight[i * 3 + 0] * DIV255),
					(float)(p->vertexlight[i * 3 + 1] * DIV255),
					(float)(p->vertexlight[i * 3 + 2] * DIV255),
					alpha);
			else
				VA_SetElem4(colorArray[rb_vertex], glState.inverse_intensity, glState.inverse_intensity, glState.inverse_intensity, alpha);

			VA_SetElem2(texCoordArray[0][rb_vertex], v[3] + scroll, v[4]);
			VA_SetElem3(vertexArray[rb_vertex], v[0], v[1], v[2]);
			rb_vertex++;
		}
	}

	if (render)
		RB_RenderGLPoly(surf, light);
}

/*
================
R_DrawWarpPoly
================
*/
void R_DrawWarpPoly (msurface_t *surf)
{
	if (!(surf->flags & SURF_DRAWTURB))
		return;

	R_BuildVertexLight(surf);

	// warp texture, no lightmaps
	GL_EnableMultitexture(false);
	R_DrawWarpSurface(surf, 1.0, true);
	GL_EnableMultitexture(true);
}

/*
================
R_DrawTriangleOutlines
================
*/
void R_DrawTriangleOutlines (void)
{
	// not used in multitexture mode
	if (glConfig.multitexture || !r_showtris->value)
		return;

	if (r_showtris->value == 1)
		GL_Disable(GL_DEPTH_TEST);

	GL_DisableTexture (0);
	qglPolygonMode (GL_FRONT_AND_BACK, GL_LINE);

	rb_vertex = rb_index = 0;
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		for (msurface_t *surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain)
		{
			for (glpoly_t *p = surf->polys; p; p = p->chain)
			{
				float *v = p->verts[0];
				const int nv = p->numverts;

				if (RB_CheckArrayOverflow(nv, (nv - 2) * 3))
					RB_RenderMeshGeneric(false);

				for (int j = 0; j < nv - 2; j++)
				{
					indexArray[rb_index++] = rb_vertex;
					indexArray[rb_index++] = rb_vertex + j + 1;
					indexArray[rb_index++] = rb_vertex + j + 2;
				}

				for (int j = 0; j < nv; j++, v += VERTEXSIZE)
				{
					VA_SetElem3(vertexArray[rb_vertex], v[0], v[1], v[2]);
					VA_SetElem4(colorArray[rb_vertex], 1, 1, 1, 1);
					rb_vertex++;
				}
			}
		}
	}
//	RB_DrawArrays ();
	RB_RenderMeshGeneric(false);

	qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	GL_EnableTexture(0);

	if (r_showtris->value == 1)
		GL_Enable(GL_DEPTH_TEST);
}


/*
================
R_DrawGLPolyChain
================
*/
void R_DrawGLPolyChain (glpoly_t *p, float soffset, float toffset)
{
	rb_vertex = rb_index = 0;
	for ( ; p != 0; p = p->chain)
	{
		float *v = p->verts[0];
		const int nv = p->numverts;

		if (RB_CheckArrayOverflow(nv, (nv - 2) * 3))
			RB_RenderMeshGeneric(false);

		for (int j = 0; j < nv - 2; j++)
		{
			indexArray[rb_index++] = rb_vertex;
			indexArray[rb_index++] = rb_vertex + j + 1;
			indexArray[rb_index++] = rb_vertex + j + 2;
		}

		for (int j = 0; j < nv; j++, v += VERTEXSIZE)
		{
			VA_SetElem2(texCoordArray[0][rb_vertex], v[5] - soffset, v[6] - toffset);
			VA_SetElem3(vertexArray[rb_vertex], v[0], v[1], v[2]);
			VA_SetElem4(colorArray[rb_vertex], 1, 1, 1, 1);
			rb_vertex++;
		}
	}
//	RB_DrawArrays ();
	RB_RenderMeshGeneric(false);
}


/*
================
R_BlendLightMaps

This routine takes all the given light mapped surfaces in the world and blends them into the framebuffer.
================
*/
void R_BlendLightmaps (void)
{
	// not used in multitexture mode || fullbright || no lightdata
	if (glConfig.multitexture || r_fullbright->integer || !r_worldmodel->lightdata)
		return;

	// don't bother writing Z
	GL_DepthMask(false);

	// set the appropriate blending mode unless we're only looking at the lightmaps.
	if (!r_lightmap->integer)
	{
		GL_Enable(GL_BLEND);

		if (r_saturatelighting->integer)
			GL_BlendFunc(GL_ONE, GL_ONE);
		else
			GL_BlendFunc(GL_ZERO, GL_SRC_COLOR);
	}

	if (currentmodel == r_worldmodel)
		c_visible_lightmaps = 0;

	// render static lightmaps first
	for (int i = 1; i < MAX_LIGHTMAPS; i++)
	{
		if (gl_lms.lightmap_surfaces[i])
		{
			if (currentmodel == r_worldmodel)
				c_visible_lightmaps++;

			GL_Bind(glState.lightmap_textures + i);

			for (msurface_t *surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain)
			{
				if (surf->polys)
					R_DrawGLPolyChain(surf->polys, 0, 0);
			}
		}
	}

	// render dynamic lightmaps
	if (r_dynamic->integer)
	{
		LM_InitBlock();

		GL_Bind(glState.lightmap_textures);

		if (currentmodel == r_worldmodel)
			c_visible_lightmaps++;

		msurface_t *newdrawsurf = gl_lms.lightmap_surfaces[0];

		for (msurface_t *surf = gl_lms.lightmap_surfaces[0]; surf != 0; surf = surf->lightmapchain)
		{
			if (LM_AllocBlock(surf->light_smax, surf->light_tmax, &surf->dlight_s, &surf->dlight_t))
			{
				unsigned *base = gl_lms.lightmap_buffer;
				base += (surf->dlight_t * LM_BLOCK_WIDTH + surf->dlight_s);		// * LIGHTMAP_BYTES

				R_BuildLightMap(surf, (void *)base, LM_BLOCK_WIDTH * LIGHTMAP_BYTES);
			}
			else
			{
				// upload what we have so far
				LM_UploadBlock(true);

				// draw all surfaces that use this lightmap
				msurface_t *drawsurf;
				for (drawsurf = newdrawsurf; drawsurf != surf; drawsurf = drawsurf->lightmapchain)
				{
					if (drawsurf->polys)
						R_DrawGLPolyChain(drawsurf->polys, 
										 (drawsurf->light_s - drawsurf->dlight_s) * (1.0 / 128.0), 
										 (drawsurf->light_t - drawsurf->dlight_t) * (1.0 / 128.0));
				}

				newdrawsurf = drawsurf;

				// clear the block
				LM_InitBlock();

				// try uploading the block now
				if (!LM_AllocBlock(surf->light_smax, surf->light_tmax, &surf->dlight_s, &surf->dlight_t))
					VID_Error(ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d, %d) failed (dynamic)\n", surf->light_smax, surf->light_tmax);

				unsigned *base = gl_lms.lightmap_buffer;
				base += (surf->dlight_t * LM_BLOCK_WIDTH + surf->dlight_s);		// * LIGHTMAP_BYTES

				R_BuildLightMap(surf, (void *)base, LM_BLOCK_WIDTH * LIGHTMAP_BYTES);
			}
		}

		// draw remainder of dynamic lightmaps that haven't been uploaded yet
		if (newdrawsurf)
			LM_UploadBlock(true);

		for (msurface_t *surf = newdrawsurf; surf != 0; surf = surf->lightmapchain)
		{
			if (surf->polys)
				R_DrawGLPolyChain(surf->polys, 
								 (surf->light_s - surf->dlight_s) * (1.0 / 128.0),
								 (surf->light_t - surf->dlight_t) * (1.0 / 128.0));
		}
	}

	// restore state
	GL_Disable(GL_BLEND);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_DepthMask(true);
}


/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (msurface_t *fa)
{
	int maps;

	if (fa->flags & SURF_DRAWTURB)
	{	
		R_BuildVertexLight(fa);

		// warp texture, no lightmaps
		GL_TexEnv(GL_MODULATE);
		R_DrawWarpSurface(fa, 1.0, true);
		GL_TexEnv(GL_REPLACE);

		return;
	}

	GL_TexEnv(GL_REPLACE);
	R_DrawGLPoly(fa, true);

	if (R_SurfIsDynamic(fa, &maps))
	{
		if ( (fa->styles[maps] >= 32 || fa->styles[maps] == 0) && fa->dlightframe != r_framecount )
		{
			unsigned *temp = malloc(fa->light_smax * fa->light_tmax); //mxd. Was 34 * 34

			R_BuildLightMap(fa, (void *)temp, fa->light_smax * 4);
			R_SetCacheState(fa);

			GL_Bind(glState.lightmap_textures + fa->lightmaptexturenum);

			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  fa->light_s, fa->light_t, 
							  fa->light_smax, fa->light_tmax,
			//				  GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE,
							  gl_lms.format, gl_lms.type,
							  temp);

			fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		}
		else
		{
			fa->lightmapchain = gl_lms.lightmap_surfaces[0];
			gl_lms.lightmap_surfaces[0] = fa;
		}
	}
	else
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}


/*
================
R_SurfsAreBatchable
================
*/
qboolean R_SurfsAreBatchable (msurface_t *s1, msurface_t *s2)
{
	if (!s1 || !s2)
		return false;

	if (s1->entity != s2->entity)
		return false;

	if ((s1->flags & SURF_DRAWTURB) != (s2->flags & SURF_DRAWTURB))
		return false;

	if ( ((s1->texinfo->flags & (SURF_TRANS33|SURF_TRANS66)) != 0) != ((s2->texinfo->flags & (SURF_TRANS33|SURF_TRANS66)) != 0) )
		return false;

	if (R_TextureAnimation(s1) != R_TextureAnimation(s2))
		return false;

	if ( (s1->flags & SURF_DRAWTURB) && (s2->flags & SURF_DRAWTURB) )
		return R_SurfIsLit(s1) == R_SurfIsLit(s2);

	if ( (s1->texinfo->flags & (SURF_TRANS33|SURF_TRANS66)) && (s2->texinfo->flags & (SURF_TRANS33|SURF_TRANS66)) )
	{
		if (r_trans_lighting->value == 2
			&& ((R_SurfIsLit(s1) && s1->lightmaptexturenum) || (R_SurfIsLit(s2) && s2->lightmaptexturenum)))
			return false;

		if (R_SurfIsLit(s1) != R_SurfIsLit(s2))
			return false;

		// must be single pass to be batchable
		if ( r_glows->value	&& ((R_TextureAnimationGlow(s1) != glMedia.notexture) || (R_TextureAnimationGlow(s2) != glMedia.notexture)) )
			return false;

		if (R_SurfHasEnvMap(s1) || R_SurfHasEnvMap(s2))
			return false;

		if ( r_caustics->value && ((s1->flags & SURF_MASK_CAUSTIC) || (s2->flags & SURF_MASK_CAUSTIC)) )
			return false;

		return true;
	}

	if ( !(s1->texinfo->flags & (SURF_DRAWTURB|SURF_TRANS33|SURF_TRANS66)) && !(s2->texinfo->flags & (SURF_DRAWTURB|SURF_TRANS33|SURF_TRANS66)) )	// lightmapped surfaces
	{
		if (s1->lightmaptexturenum != s2->lightmaptexturenum)	// lightmap image must be same
			return false;

#ifndef BATCH_LM_UPDATES
		if (R_SurfIsDynamic(s1, NULL) || R_SurfIsDynamic(s2, NULL)) // can't be dynamically list
			return false;
#endif	// BATCH_LM_UPDATES

		if ((s1->texinfo->flags & SURF_ALPHATEST) != (s2->texinfo->flags & SURF_ALPHATEST))
			return false;

		if (R_TextureAnimationGlow(s1) != R_TextureAnimationGlow(s2))
			return false;

		if (R_SurfHasEnvMap(s1) != R_SurfHasEnvMap(s2))
			return false;

		if ((s1->flags & SURF_MASK_CAUSTIC) != (s2->flags & SURF_MASK_CAUSTIC))
			return false;

		return true;
	}

	return false;
}


/*
================
R_DrawAlphaSurfaces

Draw trans water surfaces and windows.
The BSP tree is waled front to back, so unwinding the chain of alpha_surfaces will draw back to front, giving proper ordering.
================
*/
void R_DrawAlphaSurfaces (void)
{
	// the textures are prescaled up for a better lighting range, so scale it back down
	rb_vertex = rb_index = 0;
	for (msurface_t *s = r_alpha_surfaces; s; s = s->texturechain)
	{
		// go back to the world matrix
		qglLoadMatrixf(r_world_matrix);

		R_BuildVertexLight(s);
		GL_Enable(GL_BLEND);
		GL_TexEnv(GL_MODULATE);
		GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// disable depth testing for all bmodel surfs except solid alphas
		const qboolean solidAlpha = (s->entity && !((s->flags & SURF_TRANS33) && (s->flags & SURF_TRANS66))); //mxd
		GL_DepthMask(!solidAlpha);

		// moving trans brushes - spaz
		if (s->entity)
			R_RotateForEntity(s->entity, true);

		const qboolean light = R_SurfIsLit(s);

		if (s->flags & SURF_DRAWTURB)
		{
			R_DrawWarpSurface(s, SurfAlphaCalc(s->texinfo->flags), !R_SurfsAreBatchable(s, s->texturechain));
		}
		else if (r_trans_lighting->value == 2 && glConfig.multitexture && light && s->lightmaptexturenum)
		{
			GL_EnableMultitexture(true);
			R_SetLightingMode(RF_TRANSLUCENT);
			R_DrawLightmappedSurface(s, true);
			GL_EnableMultitexture(false);
		}
		else
		{
			R_DrawGLPoly(s, !R_SurfsAreBatchable(s, s->texturechain));
		}
	}

	// go back to the world matrix after shifting trans faces
	qglLoadMatrixf(r_world_matrix);

	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv(GL_REPLACE);
	qglColor4f(1, 1, 1, 1);
	GL_Disable(GL_BLEND);
	GL_DepthMask(true);

	r_alpha_surfaces = NULL;
}


#ifdef BATCH_LM_UPDATES
/*
=============
R_UpdateSurfaceLightmap

Based on code from MH's experimental Q2 engine
=============
*/
void R_UpdateSurfaceLightmap (msurface_t *surf)
{
	int map;

	if (R_SurfIsDynamic(surf, &map))
	{
		unsigned	*base = gl_lms.lightmap_update[surf->lightmaptexturenum];
		rect_t		*rect = &gl_lms.lightrect[surf->lightmaptexturenum];

		base += (surf->light_t * LM_BLOCK_WIDTH) + surf->light_s;
		R_BuildLightMap(surf, (void *)base, LM_BLOCK_WIDTH * LIGHTMAP_BYTES);
		R_SetCacheState(surf);
		gl_lms.modified[surf->lightmaptexturenum] = true;

		if (surf->light_s < rect->left)
			rect->left = surf->light_s;

		if ((surf->light_s + surf->light_smax) > rect->right)
			rect->right = surf->light_s + surf->light_smax;

		if (surf->light_t < rect->top)
			rect->top = surf->light_t;

		if ((surf->light_t + surf->light_tmax) > rect->bottom)
			rect->bottom = surf->light_t + surf->light_tmax;
	}
}


/*
=============
R_RebuildLightmaps

Based on code from MH's experimental Q2 engine
=============
*/
void R_RebuildLightmaps (void)
{
	qboolean storeSet = false;

	for (int i = 1; i < gl_lms.current_lightmap_texture; i++)
	{
		if (!gl_lms.modified[i])
			continue;

		if (!storeSet)
		{
			qglPixelStorei(GL_UNPACK_ROW_LENGTH, LM_BLOCK_WIDTH);
			storeSet = true;
		}

		GL_MBind(1, glState.lightmap_textures + i);
		qglTexSubImage2D(GL_TEXTURE_2D, 0,
				gl_lms.lightrect[i].left, gl_lms.lightrect[i].top, 
				(gl_lms.lightrect[i].right - gl_lms.lightrect[i].left), (gl_lms.lightrect[i].bottom - gl_lms.lightrect[i].top), 
		//		GL_LIGHTMAP_FORMAT, GL_LIGHTMAP_TYPE,
				gl_lms.format, gl_lms.type,
				gl_lms.lightmap_update[i] + (gl_lms.lightrect[i].top * LM_BLOCK_WIDTH) + gl_lms.lightrect[i].left);
			
		gl_lms.modified[i] = false;
		gl_lms.lightrect[i].left = LM_BLOCK_WIDTH;
		gl_lms.lightrect[i].right = 0;
		gl_lms.lightrect[i].top = LM_BLOCK_HEIGHT;
		gl_lms.lightrect[i].bottom = 0;
	}

	if (storeSet)
		qglPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}
#endif // BATCH_LM_UPDATES


/*
================
R_DrawMultiTextureChains

Draws solid warp surfaces im multitexture mode
================
*/
void R_DrawMultiTextureChains (void)
{
	int			i;
	msurface_t	*s;
	image_t		*image;

	c_visible_textures = 0;

#ifdef MULTITEXTURE_CHAINS
	GL_EnableMultitexture(true);
	R_SetLightingMode(0);

#ifdef BATCH_LM_UPDATES
	R_RebuildLightmaps();
#endif

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (!image->registration_sequence || !image->texturechain)
			continue;

		rb_vertex = rb_index = 0;
		for (s = image->texturechain; s; s = s->texturechain)
			R_DrawLightmappedSurface (s, !R_SurfsAreBatchable(s, s->texturechain));

		image->texturechain = NULL;
	}

	GL_EnableMultitexture(false);
#endif // MULTITEXTURE_CHAINS

	GL_TexEnv(GL_MODULATE); // warp textures, no lightmaps
	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (!image->registration_sequence || !image->warp_texturechain)
			continue;

		rb_vertex = rb_index = 0;
		for (s = image->warp_texturechain; s; s = s->texturechain)
		{
			R_BuildVertexLight(s);
			R_DrawWarpSurface(s, 1.0, !R_SurfsAreBatchable(s, s->texturechain)); 
		}

		image->warp_texturechain = NULL;
	}

	GL_TexEnv(GL_REPLACE);
}


/*
================
R_DrawTextureChains

Draws all solid textures in 2-pass mode
================
*/
void R_DrawTextureChains (void)
{
	int			i;
	msurface_t	*s;
	image_t		*image;

	c_visible_textures = 0;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (!image->registration_sequence || !image->texturechain)
			continue;

		c_visible_textures++;
		rb_vertex = rb_index = 0;
		for (s = image->texturechain; s; s = s->texturechain)
			R_RenderBrushPoly(s);

		image->texturechain = NULL;
	}

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (!image->registration_sequence || !image->warp_texturechain)
			continue;

		rb_vertex = rb_index = 0;
		for (s = image->warp_texturechain; s; s = s->texturechain)
			R_RenderBrushPoly(s);

		image->warp_texturechain = NULL;
	}

	GL_TexEnv(GL_REPLACE);
}


/*
===========================================
RB_DrawEnvMap
===========================================
*/
static void RB_DrawEnvMap (void)
{
	qboolean previousBlend = false;

	GL_MBind(0, glMedia.envmappic->texnum);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (!glState.blend)
		GL_Enable(GL_BLEND);
	else
		previousBlend = true;

	qglTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
	qglTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

	qglEnable(GL_TEXTURE_GEN_S);
	qglEnable(GL_TEXTURE_GEN_T);

	RB_DrawArrays();
	
	qglDisable(GL_TEXTURE_GEN_S);
	qglDisable(GL_TEXTURE_GEN_T);

	if (!previousBlend) // restore state
		GL_Disable(GL_BLEND);
}

/*
===========================================
RB_DrawTexGlow
===========================================
*/
static void RB_DrawTexGlow (image_t *glowImage)
{
	qboolean previousBlend = false;

	GL_MBind(0, glowImage->texnum);
	GL_BlendFunc(GL_ONE, GL_ONE);

	if (!glState.blend)
		GL_Enable(GL_BLEND);
	else
		previousBlend = true;

	RB_DrawArrays();

	if (!previousBlend) // restore state
		GL_Disable(GL_BLEND);

	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


/*
===========================================
RB_CausticForSurface
===========================================
*/
image_t *RB_CausticForSurface (msurface_t *surf)
{
	if (surf->flags & SURF_UNDERLAVA)
		return glMedia.causticlavapic;

	if (surf->flags & SURF_UNDERSLIME)
		return glMedia.causticslimepic;

	return glMedia.causticwaterpic;
}


/*
===========================================
RB_DrawCaustics
Underwater caustic effect based on code by Kirk Barnes
===========================================
*/
extern unsigned int dst_texture_ARB;
static void RB_DrawCaustics (msurface_t *surf)
{
	image_t		*causticpic = RB_CausticForSurface (surf);
	qboolean	previousBlend = false;
	const qboolean	fragmentWarp = glConfig.multitexture && glConfig.arb_fragment_program && (r_caustics->value > 1.0);
	
	// adjustment for texture size and caustic image
	const float scaleh = surf->texinfo->texWidth / (causticpic->width * 0.5);
	const float scalev = surf->texinfo->texHeight / (causticpic->height * 0.5);

	// sin and cos circular drifting
	const float scrollh = sin(r_newrefdef.time * 0.08 * M_PI) * 0.45;
	const float scrollv = cos(r_newrefdef.time * 0.08 * M_PI) * 0.45;
	const float dstscroll = -1.0 * ( (r_newrefdef.time * 0.15) - (int)(r_newrefdef.time * 0.15) );

	GL_MBind (0, causticpic->texnum);
	if (fragmentWarp)
	{
		GL_EnableTexture(1);
		GL_MBind(1, dst_texture_ARB);
		GL_Enable(GL_FRAGMENT_PROGRAM_ARB);
		qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, fragment_programs[F_PROG_WARP]);
		qglProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0, 1.0, 1.0, 1.0, 1.0);
	}

	GL_BlendFunc (GL_DST_COLOR, GL_ONE);
	if (!glState.blend)
		GL_Enable (GL_BLEND);
	else
		previousBlend = true;

	// just reuse verts, color, and index from previous pass
	for (int i = 0; i < rb_vertex; i++)
	{
		VA_SetElem2(texCoordArray[0][i], (inTexCoordArray[i][0] * scaleh) + scrollh,   (inTexCoordArray[i][1] * scalev) + scrollv);
		VA_SetElem2(texCoordArray[1][i], (inTexCoordArray[i][0] * scaleh) + dstscroll, (inTexCoordArray[i][1] * scalev));
	}

	RB_DrawArrays();

	if (fragmentWarp)
	{
		GL_Disable (GL_FRAGMENT_PROGRAM_ARB);
		GL_DisableTexture(1);
		GL_SelectTexture(0);
	}

	if (!previousBlend) // restore state
		GL_Disable(GL_BLEND);

	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


/*
===========================================
RB_RenderLightmappedSurface

backend for R_DrawLightmappedSurface
===========================================
*/
static void RB_RenderLightmappedSurface (msurface_t *surf)
{
	if (rb_vertex == 0 || rb_index == 0) // nothing to render
		return;
	
	image_t *image = R_TextureAnimation(surf);
	image_t *glow = R_TextureAnimationGlow(surf);
	const float	alpha = colorArray[0][3];
	const unsigned lmtex = surf->lightmaptexturenum;

	const qboolean glowLayer = (r_glows->value && (glow != glMedia.notexture) && glConfig.max_texunits > 2);
	const qboolean glowPass =  (r_glows->value && (glow != glMedia.notexture) && !glowLayer);
	const qboolean envMap = R_SurfHasEnvMap(surf);
	const qboolean causticPass = (r_caustics->value && !(surf->texinfo->flags & SURF_ALPHATEST) && (surf->flags & SURF_MASK_CAUSTIC));

	c_brush_calls++;

#ifndef BATCH_LM_UPDATES
	int map;
	if (R_SurfIsDynamic(surf, &map))
	{
		unsigned temp[LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT];

		const int smax = (surf->extents[0] >> gl_lms.lmshift) + 1;
		const int tmax = (surf->extents[1] >> gl_lms.lmshift) + 1;

		R_BuildLightMap(surf, (void *)temp, smax * 4);

		if ((surf->styles[map] >= 32 || surf->styles[map] == 0) && surf->dlightframe != r_framecount)
		{
			R_SetCacheState(surf);
			GL_MBind(1, glState.lightmap_textures + surf->lightmaptexturenum);
			lmtex = surf->lightmaptexturenum;
		}
		else
		{
			GL_MBind(1, glState.lightmap_textures + 0);
			lmtex = 0;
		}

		qglTexSubImage2D (GL_TEXTURE_2D, 0,
						  surf->light_s, surf->light_t, 
						  smax, tmax, 
		//				  GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE,
						  gl_lms.format, gl_lms.type,
						  temp);
	}
#endif // BATCH_LM_UPDATES

	// Alpha test flag
	if (surf->texinfo->flags & SURF_ALPHATEST)
		GL_Enable(GL_ALPHA_TEST);

	GL_MBind(0, image->texnum);
	if (r_fullbright->value != 0 || surf->texinfo->flags & SURF_NOLIGHTENV)
		GL_MBind(1, glMedia.whitetexture->texnum);
	else
		GL_MBind(1, glState.lightmap_textures + lmtex);
	
	if (glowLayer) 
	{
		for (int i = 0; i < rb_vertex; i++) // copy texture coords
			VA_SetElem2(texCoordArray[2][i], texCoordArray[0][i][0], texCoordArray[0][i][1]);

		GL_EnableTexture(2);
		GL_MBind(2, glow->texnum);

		if (!glConfig.mtexcombine) // if we've got > 2 TMUs, this can't be the case, right?
		{
			GL_TexEnv(GL_ADD);
		}
		else
		{
			qglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
			qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_ADD);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB);
		//	qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_CONSTANT_ARB);
			qglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
			qglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
		//	qglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_SRC_ALPHA);
			qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, (alpha < 1.0f ? GL_MODULATE : GL_ADD));
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);
			qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB);
		//	qglTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_ALPHA_ARB, GL_CONSTANT_ARB);
			qglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);
			qglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA);
		//	qglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA_ARB, GL_SRC_ALPHA);
		}
	}

	RB_DrawArrays();

	GL_Disable(GL_ALPHA_TEST); // Alpha test flag

	if (glowLayer) 
		GL_DisableTexture(2);

	if (glowPass || envMap || causticPass)
		GL_DisableTexture(1);

	if (glowPass) // just redraw with existing arrays for glow
		RB_DrawTexGlow(glow);

	if (envMap && !causticPass)
	{
		for (int i = 0; i < rb_vertex; i++) 
			colorArray[i][3] = alpha * 0.2;

		RB_DrawEnvMap();

		for (int i = 0; i < rb_vertex; i++) 
			colorArray[i][3] = alpha;
	}

	if (causticPass) // Barnes caustics
		RB_DrawCaustics(surf);

	if (envMap || glowPass || causticPass)
		GL_EnableTexture(1);

	RB_DrawMeshTris();
	rb_vertex = rb_index = 0;
}


/*
===========================================
R_DrawLightmappedSurface
===========================================
*/
void R_DrawLightmappedSurface (msurface_t *surf, qboolean render)
{
	float scroll, alpha;

	c_brush_surfs++;

	if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		alpha = (surf->entity && (surf->entity->flags & RF_TRANSLUCENT)) ? surf->entity->alpha : 1.0;
	else
		alpha = (currententity && (currententity->flags & RF_TRANSLUCENT)) ? currententity->alpha : 1.0;

	alpha *= SurfAlphaCalc(surf->texinfo->flags);

	if (surf->texinfo->flags & SURF_FLOWING)
	{
		scroll = -64 * ((r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0));
		if (scroll == 0.0) scroll = -64.0;
	}
	else
	{
		scroll = 0.0;
	}

//	rb_vertex = rb_index = 0;
	for (glpoly_t *p = surf->polys; p; p = p->chain)
	{
		const int nv = p->numverts;
		c_brush_polys += (nv - 2);
		float *v = p->verts[0];

		if (RB_CheckArrayOverflow(nv, (nv - 2) * 3))
			RB_RenderLightmappedSurface(surf);

		for (int i = 0; i < nv - 2; i++)
		{
			indexArray[rb_index++] = rb_vertex;
			indexArray[rb_index++] = rb_vertex + i + 1;
			indexArray[rb_index++] = rb_vertex + i + 2;
		}

		for (int i = 0; i < nv; i++, v += VERTEXSIZE)
		{
			VA_SetElem2(inTexCoordArray[rb_vertex], v[3], v[4]);
			VA_SetElem2(texCoordArray[0][rb_vertex], (v[3] + scroll), v[4]);
			VA_SetElem2(texCoordArray[1][rb_vertex], v[5], v[6]);
			VA_SetElem3(vertexArray[rb_vertex], v[0], v[1], v[2]);
			VA_SetElem4(colorArray[rb_vertex], 1, 1, 1, alpha);
			rb_vertex++;
		}
	}

	if (render)
		RB_RenderLightmappedSurface(surf);
}


#if 0
/*
=================
SurfInFront
Returns true if surf1 is in front of surf2

FIXME- need to find a better way to sort trans surfaces
like an algorithm that uses psurf->extents and psurf->plane->normal
relative to vieworigin and takes into account e's offset and angles
=================
*/
qboolean SurfInFront (msurface_t *surf1, msurface_t *surf2)
{
	float dist1, dist2;
	vec3_t org1, org2;

	if (!r_trans_surf_sorting->value) // check if sorting disabled
		return true;

	if (!surf1->plane || !surf2->plane)
		return false;

	if (surf1->entity)
		VectorSubtract(r_newrefdef.vieworg, surf1->entity->origin, org1);
	else
		VectorCopy(r_newrefdef.vieworg, org1);

	if (surf2->entity)
		VectorSubtract(r_newrefdef.vieworg, surf2->entity->origin, org2);
	else
		VectorCopy(r_newrefdef.vieworg, org2);

	dist1 = DotProduct(org1, surf1->plane->normal) - surf1->plane->dist;
	dist2 = DotProduct(org2, surf2->plane->normal) - surf2->plane->dist;
		
	if (dist1 < dist2)
		return true;
	else
		return false;
	//return (surf2->plane->dist > surf1->plane->dist);
}
#endif

/*
=================
R_DrawInlineBModel
=================
*/
void R_DrawInlineBModel (entity_t *e, int causticflag)
{
	cplane_t	*pplane;
	float		dot;
	qboolean	duplicate;
	image_t		*image;

	msurface_t *psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	for (int i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
	{
		// find which side of the face we are on
		pplane = psurf->plane;

		if (pplane->type < 3)
			dot = modelorg[pplane->type] - pplane->dist;
		else
			dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		// cull the polygon
		if (dot > BACKFACE_EPSILON)
			psurf->visframe = r_framecount;
	}

	// calculate dynamic lighting for bmodel
	if (!r_flashblend->value)
	{
		dlight_t *lt = r_newrefdef.dlights;
		if (currententity->angles[0] || currententity->angles[1] || currententity->angles[2])
		{
			vec3_t temp;
			vec3_t forward, right, up;
			AngleVectors(currententity->angles, forward, right, up);

			for (int k = 0; k < r_newrefdef.num_dlights; k++, lt++)
			{
				VectorSubtract(lt->origin, currententity->origin, temp);
				lt->origin[0] = DotProduct(temp, forward);
				lt->origin[1] = -DotProduct(temp, right);
				lt->origin[2] = DotProduct(temp, up);
				R_MarkLights(lt, k, currentmodel->nodes + currentmodel->firstnode);
				VectorAdd(temp, currententity->origin, lt->origin);
			}
		} 
		else
		{
			for (int k = 0; k < r_newrefdef.num_dlights; k++, lt++)
			{
				VectorSubtract(lt->origin, currententity->origin, lt->origin);
				R_MarkLights(lt, k, currentmodel->nodes + currentmodel->firstnode);
				VectorAdd(lt->origin, currententity->origin, lt->origin);
			}
		}
	}

#ifdef MULTITEXTURE_CHAINS
	if (!glConfig.multitexture)
#endif	// MULTITEXTURE_CHAINS
	{
		if (currententity->flags & RF_TRANSLUCENT)
		{
			GL_DepthMask(false);
			GL_TexEnv(GL_MODULATE);
			GL_Enable(GL_BLEND);
			GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}	

	//
	// draw standard surfaces
	//
	R_SetLightingMode(e->flags); // set up texture combiners

	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	for (int i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
	{
		// find which side of the node we are on
		pplane = psurf->plane;
		dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		// draw the polygon
		if ((psurf->flags & SURF_PLANEBACK && dot < -BACKFACE_EPSILON) ||
			(!(psurf->flags & SURF_PLANEBACK) && dot > BACKFACE_EPSILON))
		{
#ifdef BATCH_LM_UPDATES
			if( glConfig.multitexture && !(psurf->texinfo->flags & (SURF_SKY | SURF_DRAWTURB)))
				R_UpdateSurfaceLightmap(psurf);
#endif
			psurf->entity = NULL;
			psurf->flags &= ~SURF_MASK_CAUSTIC; // clear old caustics

			if ( psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66) )
			{
				// add to the translucent chain
				// if bmodel is used by multiple entities, adding surface to linked list more than once would result in an infinite loop
				duplicate = false;
				for (msurface_t *s = r_alpha_surfaces; s; s = s->texturechain)
				{
					if (s == psurf)
					{
						duplicate = true;
						break;
					}
				}

				if (!duplicate) // Don't allow surface to be added twice (fixes hang)
				{
					psurf->flags |= causticflag; // set caustics
					psurf->texturechain = r_alpha_surfaces;
					r_alpha_surfaces = psurf;
					psurf->entity = e; // entity pointer to support movement
				}
			}
			else
			{
				image = R_TextureAnimation(psurf);
				if (glConfig.multitexture && !(psurf->flags & SURF_DRAWTURB))
				{
					psurf->flags |= causticflag; // set caustics
			#ifdef MULTITEXTURE_CHAINS
					psurf->texturechain = image->texturechain;
					image->texturechain = psurf;
			#else
					R_DrawLightmappedSurface (psurf, true);
			#endif	// MULTITEXTURE_CHAINS
				}
				else if (glConfig.multitexture && (psurf->flags & SURF_DRAWTURB))	// warp surface
				{ 
			#ifdef MULTITEXTURE_CHAINS
					psurf->texturechain = image->warp_texturechain;
					image->warp_texturechain = psurf;
			#else
					continue;
			#endif	// MULTITEXTURE_CHAINS
				}
				else // 2-pass mode
				{
					GL_EnableMultitexture(false);
					R_RenderBrushPoly(psurf);
					GL_EnableMultitexture(true);

					// 2-pass mode-specific stuff
					R_BlendLightmaps();
					R_DrawTriangleOutlines();
				}
			}

		}
	}

#ifndef MULTITEXTURE_CHAINS
	//
	// draw warp surfaces
	//
	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	for (i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
	{
		// find which side of the node we are on
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

		// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if ( psurf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) )
				continue;
			else if (!(psurf->flags & SURF_DRAWTURB)) // non-warp surface
				continue;
			else // warp surface
				R_DrawWarpPoly (psurf);
		}
	}
#else	// MULTITEXTURE_CHAINS
	if (glConfig.multitexture)
	{
		if (currententity->flags & RF_TRANSLUCENT)
		{
			GL_DepthMask(false);
			GL_TexEnv(GL_MODULATE);
			GL_Enable(GL_BLEND);
			GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		R_DrawMultiTextureChains();
	}
#endif	// MULTITEXTURE_CHAINS

	if (currententity->flags & RF_TRANSLUCENT)
	{
		GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GL_Disable(GL_BLEND);
		GL_TexEnv(GL_REPLACE);
		GL_DepthMask(true);
	}
}

/*
=================
R_DrawBrushModel
=================
*/
int CL_PMpointcontents (vec3_t point);
int CL_PMpointcontents2 (vec3_t point, model_t *ignore);
void R_DrawBrushModel (entity_t *e)
{
	vec3_t		mins, maxs, org;
	int			contents[9], causticflag = 0;
	qboolean	rotated;

	if (currentmodel->nummodelsurfaces == 0)
		return;

	currententity = e;
	glState.currenttextures[0] = glState.currenttextures[1] = -1;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (int i = 0; i < 3; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd(e->origin, currentmodel->mins, mins);
		VectorAdd(e->origin, currentmodel->maxs, maxs);
	}

	if (R_CullBox(mins, maxs))
		return;

	qglColor3f (1, 1, 1);
	memset(gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	VectorSubtract(r_newrefdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy(modelorg, temp);
		AngleVectors(e->angles, forward, right, up);
		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	// check for caustics, based on code by Berserker
	if (r_caustics->value)
	{
		VectorSet(org, mins[0], mins[1], mins[2]);
	//	contents[0] = Mod_PointInLeaf(org, r_worldmodel)->contents;
		contents[0] = CL_PMpointcontents2(org, currentmodel);

		VectorSet(org, maxs[0], mins[1], mins[2]);
		contents[1] = CL_PMpointcontents2(org, currentmodel);

		VectorSet(org, mins[0], maxs[1], mins[2]);
		contents[2] = CL_PMpointcontents2(org, currentmodel);

		VectorSet(org, maxs[0], maxs[1], mins[2]);
		contents[3] = CL_PMpointcontents2(org, currentmodel);

		VectorSet(org, mins[0], mins[1], maxs[2]);
		contents[4] = CL_PMpointcontents2(org, currentmodel);

		VectorSet(org, maxs[0], mins[1], maxs[2]);
		contents[5] = CL_PMpointcontents2(org, currentmodel);

		VectorSet(org, mins[0], maxs[1], maxs[2]);
		contents[6] = CL_PMpointcontents2(org, currentmodel);

		VectorSet(org, maxs[0], maxs[1], maxs[2]);
		contents[7] = CL_PMpointcontents2(org, currentmodel);

		for(int i = 0; i < 3; i++)
			org[i] = (mins[i] + maxs[i]) * 0.5;

		contents[8] = CL_PMpointcontents2(org, currentmodel);

		const int contentsAND = (contents[0] & contents[1] & contents[2] & contents[3] & contents[4] & contents[5] & contents[6] & contents[7] & contents[8]);
		const int contentsOR =  (contents[0] | contents[1] | contents[2] | contents[3] | contents[4] | contents[5] | contents[6] | contents[7] | contents[8]);
	//	viewInWater = (Mod_PointInLeaf(r_newrefdef.vieworg, r_worldmodel)->contents & MASK_WATER);
		const qboolean viewInWater = (CL_PMpointcontents(r_newrefdef.vieworg) & MASK_WATER);
		
		if ( (contentsAND & MASK_WATER) || ((contentsOR & MASK_WATER) && viewInWater) )
		{
			if (contentsOR & CONTENTS_LAVA)
				causticflag = SURF_UNDERLAVA;
			else if (contentsOR & CONTENTS_SLIME)
				causticflag = SURF_UNDERSLIME;
			else
				causticflag = SURF_UNDERWATER;
		}
	}

    qglPushMatrix ();
	R_RotateForEntity(e, true);

	GL_EnableMultitexture(true);
	R_DrawInlineBModel(e, causticflag);
	GL_EnableMultitexture(false);

	qglPopMatrix ();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int			c, side, sidebit;
	msurface_t	*surf;
	float		dot;
	image_t		*image;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (node->visframe != r_visframecount)
		return;

	if (R_CullBox(node->minmaxs, node->minmaxs + 3))
		return;
	
	// if a leaf node, draw stuff
	if (node->contents != -1)
	{
		mleaf_t *pleaf = (mleaf_t *)node;

		// check for door connected areas
		if (r_newrefdef.areabits)
		{
			if ( !(r_newrefdef.areabits[pleaf->area >> 3] & (1 << (pleaf->area & 7))) )
				return; // not visible
		}

		msurface_t **mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	cplane_t *plane = node->plane;

	switch (plane->type)
	{
		case PLANE_X: dot = modelorg[0] - plane->dist; break;
		case PLANE_Y: dot = modelorg[1] - plane->dist; break;
		case PLANE_Z: dot = modelorg[2] - plane->dist; break;
		default:      dot = DotProduct(modelorg, plane->normal) - plane->dist; break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

	// recurse down the children, front side first
	R_RecursiveWorldNode(node->children[side]);

	// draw stuff
	for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

		if ((surf->flags & SURF_PLANEBACK) != sidebit)
			continue; // wrong side

		surf->entity = NULL;

#ifdef BATCH_LM_UPDATES
		if ( glConfig.multitexture && !(surf->texinfo->flags & (SURF_SKY | SURF_DRAWTURB)) )
			R_UpdateSurfaceLightmap(surf);
#endif

		if (surf->texinfo->flags & SURF_SKY)
		{
			// just adds to visible sky bounds
			R_AddSkySurface(surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66))
		{
			// add to the translucent chain
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
		}
#ifndef MULTITEXTURE_CHAINS
		else if (glConfig.multitexture && !(surf->flags & SURF_DRAWTURB))
		{	
			R_DrawLightmappedSurface(surf, true);
		}
#endif // MULTITEXTURE_CHAINS
		else
		{
			// the polygon is visible, so add it to the texture chain
			image = R_TextureAnimation(surf);
			if ( !(surf->flags & SURF_DRAWTURB) )
			{
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
			else
			{
				surf->texturechain = image->warp_texturechain;
				image->warp_texturechain = surf;
			}
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode(node->children[!side]);
}


/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	if (!r_drawworld->value || r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		return;

	currentmodel = r_worldmodel;

	VectorCopy(r_newrefdef.vieworg, modelorg);

	// auto cycle the world frame for texture animation
	entity_t ent;
	memset(&ent, 0, sizeof(ent));
	// Knightmare added r_worldframe for trans animations
	ent.frame = r_worldframe = (int)(r_newrefdef.time * 2); 
	currententity = &ent;

	glState.currenttextures[0] = glState.currenttextures[1] = -1;

	qglColor3f(1, 1, 1);
	memset(gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	R_ClearSkyBox();

	if (glConfig.multitexture)
	{
#ifndef MULTITEXTURE_CHAINS
		GL_EnableMultitexture (true);
		R_SetLightingMode (0);
#endif // MULTITEXTURE_CHAINS

		R_RecursiveWorldNode(r_worldmodel->nodes);

#ifndef MULTITEXTURE_CHAINS
		GL_EnableMultitexture (false);
#endif // MULTITEXTURE_CHAINS

		R_DrawMultiTextureChains();	// draw solid warp surfaces
	}
	else
	{
		// add surfaces to texture chains for 2-pass rendering
		R_RecursiveWorldNode(r_worldmodel->nodes);
		R_DrawTextureChains();
		R_BlendLightmaps();
		R_DrawTriangleOutlines();
	}

	R_DrawSkyBox();
}


/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
void R_MarkLeaves (void)
{
	byte	fatvis[MAX_MAP_LEAFS / 8];
	mleaf_t	*leaf;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !r_novis->value && r_viewcluster != -1)
		return;

	// development aid to let you run around and see exactly where the pvs ends
	if (r_lockpvs->value)
		return;

	if (!r_worldmodel)	// Knightmare- potential crash fix
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || r_viewcluster == -1 || !r_worldmodel->vis)
	{
		// mark everything
		for (int i = 0; i < r_worldmodel->numleafs; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;

		for (int i = 0; i < r_worldmodel->numnodes; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;

		return;
	}

	byte *vis = Mod_ClusterPVS(r_viewcluster, r_worldmodel);

	// may have to combine two clusters because of solid water boundaries
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy(fatvis, vis, (r_worldmodel->numleafs + 7) / 8);
		vis = Mod_ClusterPVS(r_viewcluster2, r_worldmodel);
		const int c = (r_worldmodel->numleafs + 31) / 32;

		for (int i = 0; i < c; i++)
			((int *)fatvis)[i] |= ((int *)vis)[i];

		vis = fatvis;
	}
	
	int counter;
	for (counter = 0, leaf = r_worldmodel->leafs; counter < r_worldmodel->numleafs; counter++, leaf++)
	{
		const int cluster = leaf->cluster;
		if (cluster == -1)
			continue;

		if (vis[cluster >> 3] & 1 << (cluster & 7))
		{
			mnode_t *node = (mnode_t *)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;

				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}


/*
=======================================================================

	Quake2Max vertex lighting code

=======================================================================
*/

/*
=================
R_BuildVertexLightBase
=================
*/
void R_SurfLightPoint (msurface_t *surf, vec3_t p, vec3_t color, qboolean baselight);

void R_BuildVertexLightBase (msurface_t *surf, glpoly_t *poly)
{
	float *v = poly->verts[0];
	for (int i = 0; i < poly->numverts; i++, v += VERTEXSIZE)
	{
		vec3_t color, point;
		VectorCopy(v, point); // lerp outward away from plane to avoid dark spots?
		VectorClear(color); //mxd

		R_SurfLightPoint(surf, point, color, true);
		R_MaxColorVec(color);

		for (int c = 0; c < 3; c++) //mxd
			poly->vertexlightbase[i * 3 + c] = (byte)(color[c] * 255.0);
	}
}


/*
=================
R_ResetVertextLight
=================
*/
void R_ResetVertextLight (msurface_t *surf)
{
	if (!surf->polys)
		return;

	for (glpoly_t *poly = surf->polys; poly; poly = poly->next)
		poly->vertexlightset = false;
}


/*
=================
R_BuildVertexLight
=================
*/
void R_BuildVertexLight (msurface_t *surf)
{
	if (surf->flags & SURF_DRAWTURB)
	{
		if (!r_warp_lighting->value) return;
	}
	else
	{
		if (!r_trans_lighting->value) return;
	}

	if (!surf->polys)
		return;

	for (glpoly_t *poly = surf->polys; poly; poly = poly->next)
	{
		if (!poly->vertexlight || !poly->vertexlightbase)
			continue;

		if (!poly->vertexlightset)
		{	
			R_BuildVertexLightBase(surf, poly);
			poly->vertexlightset = true;
		}

		float *v = poly->verts[0];
		for (int i = 0; i < poly->numverts; i++, v += VERTEXSIZE)
		{
			vec3_t color, point;
			VectorCopy(v, point); // lerp outward away from plane to avoid dark spots?
			VectorClear(color); //mxd

			R_SurfLightPoint(surf, point, color, false);

			for (int c = 0; c < 3; c++) //mxd
				color[c] += (float)poly->vertexlightbase[i * 3 + c] / 255.0;
				
			R_MaxColorVec(color);

			for (int c = 0; c < 3; c++) //mxd
				poly->vertexlight[i * 3 + c] = (byte)(color[c] * 255.0);
		}
	}
}

/*
=======================================================================
	end Quake2Max vertex lighting code
=======================================================================
*/


/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

/*
================
LM_InitBlock
================
*/
static void LM_InitBlock (void)
{
	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));

#ifdef BATCH_LM_UPDATES
	// alloc lightmap update buffer if needed
	if (!gl_lms.lightmap_update[gl_lms.current_lightmap_texture])
		gl_lms.lightmap_update[gl_lms.current_lightmap_texture] = Z_Malloc(LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT * LIGHTMAP_BYTES);
#endif	// BATCH_LM_UPDATES
}


/*
================
LM_UploadBlock
================
*/
static void LM_UploadBlock (qboolean dynamic)
{
	const int texture = (dynamic ? 0 : gl_lms.current_lightmap_texture);
	const int filter = (gl_lms.lmshift == 0 && !strncmp(r_texturemode->string, "GL_NEAREST", 10) ? GL_NEAREST : GL_LINEAR); //mxd
	GL_Bind(glState.lightmap_textures + texture);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

	if (dynamic)
	{
		int height = 0;
		for (int i = 0; i < LM_BLOCK_WIDTH; i++)
		{
			if (gl_lms.allocated[i] > height)
				height = gl_lms.allocated[i];
		}

		qglTexSubImage2D( GL_TEXTURE_2D, 
						  0,
						  0, 0,
						  LM_BLOCK_WIDTH, height,
						  gl_lms.format,
						  gl_lms.type,
	//					  GL_LIGHTMAP_FORMAT,
	//					  GL_UNSIGNED_BYTE,
						  gl_lms.lightmap_buffer );
	}
	else
	{
		qglTexImage2D( GL_TEXTURE_2D, 
					   0, 
					   gl_lms.internal_format,
					   LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 
					   0, 
					   gl_lms.format,
					   gl_lms.type,
	//				   GL_LIGHTMAP_FORMAT, 
	//				   GL_UNSIGNED_BYTE, 
#ifdef BATCH_LM_UPDATES
					   gl_lms.lightmap_update[gl_lms.current_lightmap_texture] );
#else
					   gl_lms.lightmap_buffer );
#endif	// BATCH_LM_UPDATES

		if (++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS)
			VID_Error(ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n");
	}
}


/*
================
LM_AllocBlock
returns a texture number and the position inside it
================
*/
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int best = LM_BLOCK_HEIGHT;

	for (int i = 0; i < LM_BLOCK_WIDTH - w; i++)
	{
		int best2 = 0;
		int j;

		for (j = 0; j < w; j++)
		{
			if (gl_lms.allocated[i + j] >= best)
				break;

			if (gl_lms.allocated[i + j] > best2)
				best2 = gl_lms.allocated[i + j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > LM_BLOCK_HEIGHT)
		return false;

	for (int i = 0; i < w; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}


/*
================
R_BuildPolygonFromSurface
================
*/
void R_BuildPolygonFromSurface (msurface_t *fa)
{
	if (fa->texinfo->flags & SURF_WARP) //mxd
		return;
	
	// reconstruct the polygon
	medge_t *pedges = currentmodel->edges;
	const int lnumverts = fa->numedges;

	//
	// draw texture
	//
	glpoly_t *poly = ModChunk_Alloc(sizeof(glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	// alloc vertex light fields
	if (fa->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
	{
		const int size = lnumverts * 3 * sizeof(byte);
		poly->vertexlight = ModChunk_Alloc(size);
		poly->vertexlightbase = ModChunk_Alloc(size);
		memset(poly->vertexlight, 0, size);
		memset(poly->vertexlightbase, 0, size);
		poly->vertexlightset = false;
	}

	vec3_t total;
	VectorClear(total);

	for (int i = 0; i < lnumverts; i++)
	{
		medge_t *r_pedge;
		float *vec;
		const int lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}

		//
		// texture coordinates
		//
		float s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->texWidth; //fa->texinfo->image->width; changed to Q2E hack

		float t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->texHeight; //fa->texinfo->image->height; changed to Q2E hack
		
		VectorAdd(total, vec, total);
		VectorCopy(vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s * gl_lms.lmscale; //mxd. 16 -> lmscale
		if(gl_lms.lmscale > 1) //mxd. Don't add half-pixel offset when using per-pixel lightmaps
			s += gl_lms.lmscale * 0.5f; //mxd. Was 8
		s /= LM_BLOCK_WIDTH * gl_lms.lmscale; //mxd. 16 -> lmscale //fa->texinfo->texture->width;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t * gl_lms.lmscale; //mxd. 16 -> lmscale
		if (gl_lms.lmscale > 1) //mxd. Don't add half-pixel offset when using per-pixel lightmaps
			t += gl_lms.lmscale * 0.5f; //mxd. Was 8
		t /= LM_BLOCK_HEIGHT * gl_lms.lmscale; //mxd. 16 -> lmscale //fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}
	VectorScale(total, 1.0 / (float)lnumverts, poly->center); // for vertex lighting

	poly->numverts = lnumverts;
}


/*
========================
R_SetupLightmapPoints (mxd)
========================
*/
static int fix_coord(int in, const int width)
{
	in %= width;
	return (in < 0 ? in + width : in);
}

void R_SetupLightmapPoints(msurface_t *surf)
{
	// Setup world matrix
	vec3_t facenormal;
	VectorCopy(surf->plane->normal, facenormal);
	float facedist = -surf->plane->dist;

	if (surf->flags & SURF_PLANEBACK)
	{
		VectorScale(facenormal, -1, facenormal);
		facedist *= -1;
	}

	mtexinfo_t *tex = surf->texinfo;
	float texSpaceToWorld[16] = 
	{
		tex->vecs[0][0], tex->vecs[1][0], facenormal[0], 0,
		tex->vecs[0][1], tex->vecs[1][1], facenormal[1], 0,
		tex->vecs[0][2], tex->vecs[1][2], facenormal[2], 0,
		tex->vecs[0][3], tex->vecs[1][3], facedist, 1
	};

	Matrix4Invert(texSpaceToWorld);

	// Setup tanget-bitanget-normal matrix...
	const qboolean calculatenormalmap = (gl_lms.lmscale == 1 && tex->nmapvectors);
	float tbn[9] =
	{
		tex->vecs[0][0], tex->vecs[0][1], tex->vecs[0][2], // snormal
		tex->vecs[1][0], tex->vecs[1][1], tex->vecs[1][2], // tnormal
		facenormal[0], facenormal[1], facenormal[2] // face normal
	};

	// Calculate lightmap/normalmap points
	const int smscale = (int)pow(2, clamp(r_dlightshadowmapscale->integer, 0, 5) - 1); // Convert [0, 1, 2, 3, 4, 5] to [0, 1, 2, 4, 8, 16]
	const float dlscale = max(gl_lms.lmscale, smscale); // Can't have dynamic light scaled lower than lightmap scale

	const int lmapsize = surf->light_smax * surf->light_tmax;
	const int pointssize = sizeof(float) * 3 * lmapsize;

	surf->lightmap_points = malloc(pointssize);
	surf->normalmap_normals = (calculatenormalmap ? malloc(pointssize) : NULL);

	vec3_t *lightmap_points = surf->lightmap_points;
	vec3_t *normalmap_points = surf->normalmap_normals;

	int index = 0;
	for (int t = 0; t < surf->light_tmax; t++)
	{
		for (int s = 0; s < surf->light_smax; s++, index++)
		{
			const int ss = surf->texturemins[0] + s * gl_lms.lmscale;
			const int st = surf->texturemins[1] + t * gl_lms.lmscale;

			float texpos[4] = { ss, st, 1.0f, 1.0f }; // One "unit" in front of surface

			// Offset first/last columns and rows towards the center, so texpos isn't at the surface edge
			if (gl_lms.lmscale > 1)
			{
				if (s == 0)
					texpos[0] += gl_lms.lmscale * 0.25f;
				else if (s == surf->light_smax - 1)
					texpos[0] -= gl_lms.lmscale * 0.25f;

				if (t == 0)
					texpos[1] += gl_lms.lmscale * 0.25f;
				else if (t == surf->light_tmax - 1)
					texpos[1] -= gl_lms.lmscale * 0.25f;
			}
			else // lmscale == 1
			{
				// Pixel shift, so texpos is at the center of r_dlightshadowmapscale x r_dlightshadowmapscale lightmap texels
				// (special handling because of special handling in R_BuildPolygonFromSurface)
				texpos[0] += dlscale * 0.5f; //TODO: this needs to be updated when r_dlightshadowmapscale is changed
				texpos[1] += dlscale * 0.5f;
			}

			// Store world position
			float worldpos4[4];
			Matrix4Multiply(texSpaceToWorld, texpos, worldpos4);
			VectorSet(lightmap_points[index], worldpos4[0], worldpos4[1], worldpos4[2]);

			// Store normalmap position?
			if(calculatenormalmap)
			{
				const int x = fix_coord(ss, tex->image->width);
				const int y = fix_coord(st, tex->image->height);

				float *normal = &tex->nmapvectors[((tex->image->width * y) + x) * 3];

				Matrix3Multiply(tbn, normal, normalmap_points[index]);
				VectorNormalize(normalmap_points[index]);
			}
		}
	}
}


/*
========================
R_CreateSurfaceLightmap
========================
*/
void R_CreateSurfaceLightmap (msurface_t *surf)
{
	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return;

	//if (surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP))
	if (surf->texinfo->flags & (SURF_SKY | SURF_WARP))
		return;

	// Store extents
	surf->light_smax = (surf->extents[0] >> gl_lms.lmshift) + 1; //mxd. 4 -> lmshift
	surf->light_tmax = (surf->extents[1] >> gl_lms.lmshift) + 1; //mxd. 4 -> lmshift

	if (!LM_AllocBlock(surf->light_smax, surf->light_tmax, &surf->light_s, &surf->light_t))
	{
		LM_UploadBlock(false);
		LM_InitBlock();
		if (!LM_AllocBlock(surf->light_smax, surf->light_tmax, &surf->light_s, &surf->light_t))
			VID_Error(ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d, %d) failed\n", surf->light_smax, surf->light_tmax);
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	unsigned *base;
#ifdef BATCH_LM_UPDATES
	base = gl_lms.lightmap_update[surf->lightmaptexturenum];
#else
	base = gl_lms.lightmap_buffer;
#endif	// BATCH_LM_UPDATES

	base += (surf->light_t * LM_BLOCK_WIDTH + surf->light_s);	// * LIGHTMAP_BYTES

	R_SetCacheState(surf);
	R_BuildLightMap(surf, (void *)base, LM_BLOCK_WIDTH * LIGHTMAP_BYTES);
}


/*
==================
R_BeginBuildingLightmaps
==================
*/
void R_BeginBuildingLightmaps (model_t *m)
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	unsigned dummy[LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT];	// 128*128

	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));

#ifdef BATCH_LM_UPDATES
	// free lightmap update buffers
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (gl_lms.lightmap_update[i])
			Z_Free(gl_lms.lightmap_update[i]);

		gl_lms.lightmap_update[i] = NULL;
		gl_lms.modified[i] = false;
		gl_lms.lightrect[i].left = LM_BLOCK_WIDTH;
		gl_lms.lightrect[i].right = 0;
		gl_lms.lightrect[i].top = LM_BLOCK_HEIGHT;
		gl_lms.lightrect[i].bottom = 0;
	}
#endif	// BATCH_LM_UPDATES

	r_framecount = 1;		// no dlightcache

	GL_EnableMultitexture(true);
	GL_SelectTexture(1);

	// setup the base lightstyles so the lightmaps won't have to be regenerated the first time they're seen
	for (int i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}
	r_newrefdef.lightstyles = lightstyles;

	if (!glState.lightmap_textures)
		glState.lightmap_textures = TEXNUM_LIGHTMAPS;

	gl_lms.current_lightmap_texture = 1;

#ifdef BATCH_LM_UPDATES
	// alloc lightmap update buffer if needed
	if (!gl_lms.lightmap_update[gl_lms.current_lightmap_texture]) 
		gl_lms.lightmap_update[gl_lms.current_lightmap_texture] = Z_Malloc(LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT * LIGHTMAP_BYTES);
#endif	// BATCH_LM_UPDATES

	gl_lms.internal_format = GL_RGBA8;
	gl_lms.format = GL_BGRA;
	gl_lms.type = GL_UNSIGNED_INT_8_8_8_8_REV;

	//mxd
	const int filter = (gl_lms.lmshift == 0 && !strncmp(r_texturemode->string, "GL_NEAREST", 10) ? GL_NEAREST : GL_LINEAR);

	// initialize the dynamic lightmap texture
	GL_Bind(glState.lightmap_textures);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	qglTexImage2D(GL_TEXTURE_2D, 
				  0, 
				  gl_lms.internal_format,
				  LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 
				  0, 
				  gl_lms.format,	// GL_LIGHTMAP_FORMAT 
				  gl_lms.type,		// GL_UNSIGNED_BYTE 
				  dummy);
}


/*
=======================
R_EndBuildingLightmaps
=======================
*/
void R_EndBuildingLightmaps (void)
{
	LM_UploadBlock(false);
	GL_EnableMultitexture(false);
}