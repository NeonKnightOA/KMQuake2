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

// r_beam.c -- beam rendering. Moved from r_main.c

#include "r_local.h"

static void R_RenderBeam(const vec3_t start, const vec3_t end, const float size, const float red, const float green, const float blue, const float alpha) //mxd. red, green, blue, alpha are in [0..1] range
{
	vec3_t vert[4], ang_up, ang_right, vdelta;
	vec2_t texCoord[4];
	vec4_t beamColor;

	c_alias_polys += 2;

	GL_TexEnv(GL_MODULATE);
	GL_DepthMask(false);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE); // this fixes the black background
	GL_Enable(GL_BLEND);
	GL_ShadeModel(GL_SMOOTH);
	GL_Bind(glMedia.particlebeam->texnum);
	Vector4Set(beamColor, red, green, blue, alpha); //mxd

	VectorSubtract(start, end, ang_up);
	VectorNormalize(ang_up);

	VectorSubtract(r_newrefdef.vieworg, start, vdelta);
	CrossProduct(ang_up, vdelta, ang_right);
	if (!VectorCompare(ang_right, vec3_origin))
		VectorNormalize(ang_right);

	VectorScale(ang_right, size * 2, ang_right); // Knightmare- a little narrower, please

	VectorAdd(start, ang_right, vert[0]);
	VectorAdd(end, ang_right, vert[1]);
	VectorSubtract(end, ang_right, vert[2]);
	VectorSubtract(start, ang_right, vert[3]);

	Vector2Set(texCoord[0], 0, 1);
	Vector2Set(texCoord[1], 0, 0);
	Vector2Set(texCoord[2], 1, 0);
	Vector2Set(texCoord[3], 1, 1);

	rb_vertex = 0;
	rb_index = 0;

	indexArray[rb_index++] = rb_vertex + 0;
	indexArray[rb_index++] = rb_vertex + 1;
	indexArray[rb_index++] = rb_vertex + 2;
	indexArray[rb_index++] = rb_vertex + 0;
	indexArray[rb_index++] = rb_vertex + 2;
	indexArray[rb_index++] = rb_vertex + 3;

	for (int i = 0; i < 4; i++)
	{
		VA_SetElem2(texCoordArray[0][rb_vertex], texCoord[i][0], texCoord[i][1]);
		VA_SetElem3(vertexArray[rb_vertex], vert[i][0], vert[i][1], vert[i][2]);
		VA_SetElem4(colorArray[rb_vertex], beamColor[0], beamColor[1], beamColor[2], beamColor[3]);
		rb_vertex++;
	}
	RB_DrawArrays();

	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv(GL_MODULATE);
	GL_DepthMask(true);
	GL_Disable(GL_BLEND);

	RB_DrawMeshTris();

	rb_vertex = 0;
	rb_index = 0;
}

//mxd
static void R_RenderClassicBeam(const vec3_t start, const vec3_t end, const float size, const float red, const float green, const float blue, const float alpha) //mxd. red, green, blue, alpha are in [0..1] range
{
	#define NUM_BEAM_SEGS 6

	vec3_t perpvec;
	vec3_t direction, normalized_direction;
	vec3_t start_points[NUM_BEAM_SEGS];
	vec3_t end_points[NUM_BEAM_SEGS];

	VectorSubtract(end, start, direction);
	VectorCopy(direction, normalized_direction);

	if (!VectorNormalize(normalized_direction))
		return;

	PerpendicularVector(perpvec, normalized_direction);
	VectorScale(perpvec, size / 2, perpvec);

	for (int i = 0; i < 6; i++)
	{
		RotatePointAroundVector(start_points[i], normalized_direction, perpvec, (360.0f / NUM_BEAM_SEGS) * i);
		VectorAdd(start_points[i], start, start_points[i]);
		VectorAdd(start_points[i], direction, end_points[i]);
	}

	qglDisable(GL_TEXTURE_2D);
	qglEnable(GL_BLEND);
	qglDepthMask(GL_FALSE);

	qglColor4f(red, green, blue, alpha);

	qglBegin(GL_TRIANGLE_STRIP);
	for (int i = 0; i < NUM_BEAM_SEGS; i++)
	{
		qglVertex3fv(start_points[i]);
		qglVertex3fv(end_points[i]);
		qglVertex3fv(start_points[(i + 1) % NUM_BEAM_SEGS]);
		qglVertex3fv(end_points[(i + 1) % NUM_BEAM_SEGS]);
	}
	qglEnd();

	qglEnable(GL_TEXTURE_2D);
	qglDisable(GL_BLEND);
	qglDepthMask(GL_TRUE);
}

void R_DrawBeam(entity_t *e)
{
	qboolean fog_on = false;

	// Knightmare- no fog on lasers
	if (qglIsEnabled(GL_FOG)) // Check if fog is enabled
	{
		fog_on = true;
		qglDisable(GL_FOG); // If so, disable it
	}

	//mxd
	float r = (d_8to24table[e->skinnum & 0xFF]) & 0xFF;
	float g = (d_8to24table[e->skinnum & 0xFF] >> 8) & 0xFF;
	float b = (d_8to24table[e->skinnum & 0xFF] >> 16) & 0xFF;

	r *= 1 / 255.0f;
	g *= 1 / 255.0f;
	b *= 1 / 255.0f;

	//mxd
	if (r_particle_mode->integer == 1)
		R_RenderBeam(e->origin, e->oldorigin, e->frame, r, g, b, e->alpha);
	else
		R_RenderClassicBeam(e->origin, e->oldorigin, e->frame, r, g, b, e->alpha);

	// Re-enable fog if it was on
	if (fog_on)
		qglEnable(GL_FOG);
}