/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "gl_local.h"

vec3_t modelViewOrigin; // viewer origin in model space

qboolean GL_LightPoint( vec3_t origin, vec3_t color ) {
    bsp_t *bsp = gl_static.world.cache;
    mface_t *surf;
    int s, t;
    byte *b1, *b2, *b3, *b4;
    int fracu, fracv;
    int w1, w2, w3, w4;
    byte temp[3];
    int pitch;
    vec3_t point;

    if( !bsp || !bsp->lightmap ) {
        return qfalse;
    }

    point[0] = origin[0];
    point[1] = origin[1];
    point[2] = origin[2] - 8192;
    
    surf = BSP_LightPoint( bsp->nodes, origin, point, &s, &t );
    if( !surf ) {
        return qfalse;
    }

    fracu = s & 15;
    fracv = t & 15;

    s >>= 4;
    t >>= 4;

    pitch = ( surf->extents[0] >> 4 ) + 1;
    b1 = &surf->lightmap[3 * ( ( t + 0 ) * pitch + ( s + 0 ) )];
    b2 = &surf->lightmap[3 * ( ( t + 0 ) * pitch + ( s + 1 ) )];
    b3 = &surf->lightmap[3 * ( ( t + 1 ) * pitch + ( s + 1 ) )];
    b4 = &surf->lightmap[3 * ( ( t + 1 ) * pitch + ( s + 0 ) )];
    
    w1 = ( 16 - fracu ) * ( 16 - fracv );
    w2 = fracu * ( 16 - fracv );
    w3 = fracu * fracv;
    w4 = ( 16 - fracu ) * fracv;

    temp[0] = ( w1 * b1[0] + w2 * b2[0] + w3 * b3[0] + w4 * b4[0] ) >> 8;
    temp[1] = ( w1 * b1[1] + w2 * b2[1] + w3 * b3[1] + w4 * b4[1] ) >> 8;
    temp[2] = ( w1 * b1[2] + w2 * b2[2] + w3 * b3[2] + w4 * b4[2] ) >> 8;

    GL_AdjustColor( temp, temp, 2 );

    VectorScale( temp, 1.0f / 255, color );

    return qtrue;
}

#if USE_DLIGHTS
static void GL_MarkLights_r( mnode_t *node, dlight_t *light ) {
    vec_t dot;
    int count;
    mface_t *face;
    int lightbit = 1 << light->index;
    
    while( node->plane ) {
        dot = PlaneDiffFast( light->transformed, node->plane );
        if( dot > light->intensity ) {
            node = node->children[0];
            continue;
        }
        if( dot < -light->intensity ) {
            node = node->children[1];
            continue;
        }

        face = node->firstFace;
        count = node->numFaces;
        while( count-- ) {
            if( !( face->texinfo->flags & NOLIGHT_MASK ) ) {
                if( face->dlightframe != glr.drawframe ) {
                    face->dlightframe = glr.drawframe;
                    face->dlightbits = 0;
                }
            
                face->dlightbits |= lightbit;
            }
            face++;
        }
        
        GL_MarkLights_r( node->children[0], light );

        node = node->children[1];
    }
}

void GL_MarkLights( void ) {
    int i;
    dlight_t *light;

    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
        light->index = i;
        VectorCopy( light->origin, light->transformed );
        GL_MarkLights_r( gl_static.world.cache->nodes, light );
    }
}

static void GL_TransformLights( mmodel_t *model ) {
    int i;
    dlight_t *light;
    vec3_t temp;

    if( !model->headnode ) {
        return;
    }
    
    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
        light->index = i;
        VectorSubtract( light->origin, glr.ent->origin, temp );
        light->transformed[0] = DotProduct( temp, glr.entaxis[0] );
        light->transformed[1] = DotProduct( temp, glr.entaxis[1] );
        light->transformed[2] = DotProduct( temp, glr.entaxis[2] );

        GL_MarkLights_r( model->headnode, light );
    }
}

void GL_AddLights( vec3_t origin, vec3_t color ) {
    dlight_t *light;
    vec3_t dir;
    vec_t dist, f;
    int i;

    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
        VectorSubtract( light->origin, origin, dir );
        dist = VectorLength( dir );
        if( dist > light->intensity ) {
            continue;
        }
        f = 1.0f - dist / light->intensity;
        VectorMA( color, f, light->color, color );
    }
}
#endif

void _R_LightPoint( vec3_t origin, vec3_t color ) {
    if( gl_fullbright->integer ) {
        VectorSet( color, 1, 1, 1 );
        return;
    }

    // get lighting from world
    if( !GL_LightPoint( origin, color ) ) {
        VectorSet( color, 1, 1, 1 );
    }

#if USE_DLIGHTS
    if( gl_dynamic->integer ) {
        // add dynamic lights
        GL_AddLights( origin, color );
    }
#endif

    // apply modulate twice to mimic original ref_gl behavior
    VectorScale( color, gl_modulate->value, color );
}

void R_LightPoint( vec3_t origin, vec3_t color ) {
    int i;

    _R_LightPoint( origin, color );

    for( i = 0; i < 3; i++ ) {
        clamp( color[i], 0, 1 );
    }
}

void GL_MarkLeaves( void ) {
    static int lastNodesVisible;
    byte vis1[MAX_MAP_VIS];
    byte vis2[MAX_MAP_VIS];
    mleaf_t *leaf;
    mnode_t *node;
    uint32_t *src1, *src2;
    int cluster1, cluster2, longs;
    vec3_t tmp;
    int i;
    bsp_t *bsp = gl_static.world.cache;

    leaf = BSP_PointLeaf( bsp->nodes, glr.fd.vieworg );
    cluster1 = cluster2 = leaf->cluster;
    VectorCopy( glr.fd.vieworg, tmp );
    if( !leaf->contents ) {
        tmp[2] -= 16;   
    } else {
        tmp[2] += 16;   
    }   
    leaf = BSP_PointLeaf( bsp->nodes, tmp );
    if( !( leaf->contents & CONTENTS_SOLID ) ) {
        cluster2 = leaf->cluster;
    }
    
    if( cluster1 == glr.viewcluster1 && cluster2 == glr.viewcluster2 ) {
        goto finish;
    }
        
    if( gl_lockpvs->integer ) {
        goto finish;
    }

    glr.visframe++;
    glr.viewcluster1 = cluster1;
    glr.viewcluster2 = cluster2;

    if( !bsp->vis || gl_novis->integer || cluster1 == -1 ) {
        // mark everything visible
        for( i = 0; i < bsp->numnodes; i++ ) {
            bsp->nodes[i].visframe = glr.visframe;
        }
        for( i = 0; i < bsp->numleafs; i++ ) {
            bsp->leafs[i].visframe = glr.visframe;
        }
        lastNodesVisible = bsp->numnodes;
        goto finish;
    }

    BSP_ClusterVis( bsp, vis1, cluster1, DVIS_PVS );
    if( cluster1 != cluster2 ) {
        BSP_ClusterVis( bsp, vis2, cluster2, DVIS_PVS );
        longs = ( bsp->visrowsize + 3 ) / 4;
        src1 = ( uint32_t * )vis1;
        src2 = ( uint32_t * )vis2;
        while( longs-- ) {
            *src1++ |= *src2++;
        }
    }

    lastNodesVisible = 0;
    for( i = 0, leaf = bsp->leafs; i < bsp->numleafs; i++, leaf++ ) {
        cluster1 = leaf->cluster;
        if( cluster1 == -1 ) {
            continue;
        }
        if( Q_IsBitSet( vis1, cluster1 ) ) {
            node = ( mnode_t * )leaf;

            // mark parent nodes visible
            do {
                if( node->visframe == glr.visframe ) {
                    break;
                }
                node->visframe = glr.visframe;
                node = node->parent;
                lastNodesVisible++;
            } while( node );
        }
    }
    
finish:
    c.nodesVisible = lastNodesVisible;

}



#define BACKFACE_EPSILON    0.001f

#define BSP_CullFace( face, dot ) \
        ( ( (dot) < -BACKFACE_EPSILON && !( (face)->drawflags & DSURF_PLANEBACK ) ) || \
          ( (dot) >  BACKFACE_EPSILON &&  ( (face)->drawflags & DSURF_PLANEBACK ) ) )

void GL_DrawBspModel( mmodel_t *model ) {
    mface_t *face;
    int count;
    vec3_t bounds[2];
    vec_t dot;
    vec3_t temp;
    entity_t *ent = glr.ent;
    glCullResult_t cull;
    
    if( glr.entrotated ) {
        cull = GL_CullSphere( ent->origin, model->radius );
        if( cull == CULL_OUT ) {
            c.spheresCulled++;
            return;
        }
        if( cull == CULL_CLIP ) {
            VectorCopy( model->mins, bounds[0] );
            VectorCopy( model->maxs, bounds[1] );
            cull = GL_CullLocalBox( ent->origin, bounds );
            if( cull == CULL_OUT ) {
                c.rotatedBoxesCulled++;
                return;
            }
        }
        VectorSubtract( glr.fd.vieworg, ent->origin, temp );
        modelViewOrigin[0] = DotProduct( temp, glr.entaxis[0] );
        modelViewOrigin[1] = DotProduct( temp, glr.entaxis[1] );
        modelViewOrigin[2] = DotProduct( temp, glr.entaxis[2] );
    } else {
        VectorAdd( model->mins, ent->origin, bounds[0] );
        VectorAdd( model->maxs, ent->origin, bounds[1] );
        cull = GL_CullBox( bounds );
        if( cull == CULL_OUT ) {
            c.boxesCulled++;
            return;
        }
        VectorSubtract( glr.fd.vieworg, ent->origin, modelViewOrigin );
    }

    glr.drawframe++;

#if USE_DLIGHTS
    if( gl_dynamic->integer ) {
        GL_TransformLights( model );
    }
#endif

    qglPushMatrix();
    qglTranslatef( ent->origin[0], ent->origin[1], ent->origin[2] );
    if( glr.entrotated ) {
        qglRotatef( ent->angles[YAW],   0, 0, 1 );
        qglRotatef( ent->angles[PITCH], 0, 1, 0 );
        qglRotatef( ent->angles[ROLL],  1, 0, 0 );
    }

    /* draw visible faces */
    /* FIXME: go by headnode instead? */
    face = model->firstface;
    count = model->numfaces;
    while( count-- ) {
        dot = PlaneDiffFast( modelViewOrigin, face->plane );
        if( BSP_CullFace( face, dot ) ) {
            c.facesCulled++;
        } else {
            /* FIXME: trans surfaces are not supported on inline models */
            GL_AddSolidFace( face );  
        }
        face++;
    }

    GL_DrawSolidFaces();

    qglPopMatrix();
}

#define NODE_CLIPPED    0
#define NODE_UNCLIPPED  15

static inline qboolean GL_ClipNode( mnode_t *node, int *clipflags ) {
    int flags = *clipflags;
    int i, bits, mask;
    
    if( flags == NODE_UNCLIPPED ) {
        return qtrue;
    }
    for( i = 0, mask = 1; i < 4; i++, mask <<= 1 ) {
        if( flags & mask ) {
            continue;
        }
        bits = BoxOnPlaneSide( node->mins, node->maxs,
            &glr.frustumPlanes[i] );
        if( bits == BOX_BEHIND ) {
            return qfalse;
        }
        if( bits == BOX_INFRONT ) {
            flags |= mask;
        }
    }

    *clipflags = flags;

    return qtrue;
}

static inline void GL_DrawLeaf( mleaf_t *leaf ) {
    mface_t **face, **last;

    if( leaf->contents == CONTENTS_SOLID ) {
        return; // solid leaf
    }
    if( glr.fd.areabits && !Q_IsBitSet( glr.fd.areabits, leaf->area ) ) {
        return; // door blocks sight
    }
    last = leaf->firstleafface + leaf->numleaffaces;
    for( face = leaf->firstleafface; face < last; face++ ) {
        (*face)->drawframe = glr.drawframe;
    }
}

static inline void GL_DrawNode( mnode_t *node, vec_t dot ) {
    mface_t *face, *last = node->firstface + node->numfaces;

    for( face = node->firstface; face < last; face++ ) {
        if( face->drawframe != glr.drawframe ) {
            continue;
        }
        if( BSP_CullFace( face, dot ) ) {
            c.facesCulled++;
        } else {
            GL_AddFace( face );
        }
    }

    c.nodesDrawn++;
}

static void GL_WorldNode_r( mnode_t *node, int clipflags ) {
    int side;
    vec_t dot;

    while( node->visframe == glr.visframe ) {
        if( !GL_ClipNode( node, &clipflags ) ) {
            c.nodesCulled++;
            break;
        }

        if( !node->plane ) {
            GL_DrawLeaf( ( mleaf_t * )node );
            break;
        }

        dot = PlaneDiffFast( modelViewOrigin, node->plane );
        if( dot < 0 ) {
            side = 1;
        } else {
            side = 0;
        }

        GL_WorldNode_r( node->children[side], clipflags );

        GL_DrawNode( node, dot );

        node = node->children[ side ^ 1 ];
    }
}

void GL_DrawWorld( void ) { 
    GL_MarkLeaves();

#if USE_DLIGHTS
    if( gl_dynamic->integer ) {
        GL_MarkLights();
    }
#endif
    
    R_ClearSkyBox();

    VectorCopy( glr.fd.vieworg, modelViewOrigin );

    GL_WorldNode_r( gl_static.world.cache->nodes,
        gl_cull_nodes->integer ? NODE_CLIPPED : NODE_UNCLIPPED );

    GL_DrawSolidFaces();

    R_DrawSkyBox();
}
