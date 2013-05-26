/*
===========================================================================
Copyright (C) 2000-2009 Darklegion Development

This file is part of Daemon.

Daemon is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "cg_local.h"

#define MINIMAP_MAP_DISPLAY_SIZE 1024.0f
#define MINIMAP_PLAYER_DISPLAY_SIZE 50.0f
#define MINIMAP_TEAMMATE_DISPLAY_SIZE 50.0f

//It is multiplied by msecs
#define MINIMAP_FADE_TIME (2.0f / 1000.0f)

//The minimap parser

/*
================
ParseFloats
================
*/
qboolean ParseFloats( float* res, int number, char **text )
{
    char* token;

    while( number --> 0 )
    {
        if( !*(token = COM_Parse( text )) )
        {
            return qfalse;
        }

        *res = atof(token);
        res ++;
    }

    return qtrue;
}

/*
================
CG_ParseMinimapZone
================
*/
qboolean CG_ParseMinimapZone( minimapZone_t* z, char **text )
{
    char* token;
    qboolean hasImage = qfalse;
    qboolean hasBounds = qfalse;

    z->scale = 1.0f;

    if( !*(token = COM_Parse( text )) || Q_stricmp( token, "{" ) )
    {
        CG_Printf( S_ERROR "expected a { at the beginning of a zones\n" );
        return qfalse;
    }

    while(1)
    {
        if( !*(token = COM_Parse( text )) )
        {
            break;
        }

        if( !Q_stricmp( token, "bounds" ) )
        {
            if( !ParseFloats( z->boundsMin, 3, text) || !ParseFloats( z->boundsMax, 3, text) )
            {
                CG_Printf( S_ERROR "error while parsing 'bounds'\n" );
                return qfalse;
            }

            hasBounds = qtrue;
        }
        else if( !Q_stricmp( token, "image") )
        {
            if( !*(token = COM_Parse( text )) )
            {
                CG_Printf( S_ERROR "error while parsing the image name while parsing 'image'\n" );
            }

            z->image = trap_R_RegisterShader( token, RSF_DEFAULT );

            if( !ParseFloats( z->imageMin, 2, text) || !ParseFloats( z->imageMax, 2, text) )
            {
                CG_Printf( S_ERROR "error while parsing 'image'\n" );
                return qfalse;
            }

            hasImage = qtrue;
        }
        else if( !Q_stricmp( token, "scale" ))
        {
            if( !*(token = COM_Parse( text )) )
            {
                CG_Printf( S_ERROR "error while parsing the value  while parsing 'scale'\n" );
            }

            z->scale = atof( token );
        }
        else if( !Q_stricmp( token, "}" ) )
        {
            break;
        }
        else
        {
            Com_Printf( S_ERROR "unknown token '%s'\n", token );
        }
    }

    if( Q_stricmp( token, "}" ) )
    {
        CG_Printf( S_ERROR "expected a } at the end of a zone\n");
        return qfalse;
    }

    if( !hasBounds )
    {
        CG_Printf( S_ERROR "missing bounds in the zone\n" );
        return qfalse;
    }

    if( !hasImage )
    {
        CG_Printf( S_ERROR "missing image in the zone\n" );
        return qfalse;
    }

    return qtrue;
}

/*
================
CG_ParseMinimap
================
*/
qboolean CG_ParseMinimap( minimap_t* m, char* filename )
{
    char text_buffer[ 20000 ];
    char* text;
    char* token;

    m->nZones = 0;
    m->lastZone = -1;
    m->scale = 1.0f;
    m->bgColor[3] = 1.0f; //Initialise the bgColor to black

    if( !BG_ReadWholeFile( filename, text_buffer, sizeof(text_buffer) ) )
    {
        return qfalse;
    }

    text = text_buffer;

    if( !*(token = COM_Parse( &text )) || Q_stricmp( token, "{" ) )
    {
        CG_Printf( S_ERROR "expected a { at the beginning of %s\n", filename );
        return qfalse;
    }

    while(1)
    {
        if( !*(token = COM_Parse( &text )) )
        {
            break;
        }

        if( !Q_stricmp( token, "zone" ) ){
            m->nZones ++;

            if( m->nZones > MAX_MINIMAP_ZONES )
            {
                CG_Printf( S_ERROR "reached the zone number limit (%i) in %s\n", MAX_MINIMAP_ZONES, filename );
                return qfalse;
            }

            if( !CG_ParseMinimapZone( &m->zones[m->nZones - 1], &text ) )
            {
                CG_Printf( S_ERROR "error while reading zone n°%i in %s\n", m->nZones, filename );
                return qfalse;
            }
        }
        else if( !Q_stricmp( token, "backgroundColor") )
        {
            if( !ParseFloats( m->bgColor, 4, &text) )
            {
                CG_Printf( S_ERROR "error while parsing 'backgroundColor' in %s\n", filename );
                return qfalse;
            }
        }
        else if( !Q_stricmp( token, "globalScale") )
        {
            if( !ParseFloats( &m->scale, 1, &text) )
            {
                CG_Printf( S_ERROR "error while parsing 'globalScale' in %s\n", filename );
                return qfalse;
            }
        }
        else if( !Q_stricmp( token, "}" ) )
        {
            break;
        }
        else
        {
            Com_Printf( S_ERROR "%s: unknown token '%s'\n", filename, token );
        }
    }

    if( Q_stricmp( token, "}" ) )
    {
        CG_Printf( S_ERROR "expected a } at the end of %s\n", filename );
        return qfalse;
    }

    return qtrue;
}

//Helper functions for the minimap

/*
================
CG_IsInMinimapZone
================
*/
qboolean CG_IsInMinimapZone(minimapZone_t* z)
{
    return PointInBounds(cg.refdef.vieworg, z->boundsMin, z->boundsMax);
}

//The parameters for the current frame's minimap transform
static float transform[4];
static float transformVector[2];
static float transformAngle;
static float transformScale;

/*
================
CG_SetupMinimapTransform
================
*/
void CG_SetupMinimapTransform( const rectDef_t *rect, const minimap_t* minimap, const minimapZone_t* zone)
{
    float posx, posy, x, y, s, c, angle, scale;

    //The refdefview angle is the angle from the x axis
    //the 90 gets it back to the Y axis (we want the view to point up)
    //and the orientation change gives the -
    transformAngle = - cg.refdefViewAngles[1];
    angle = DEG2RAD(transformAngle + 90.0);

    transformScale = zone->scale * minimap->scale;
    scale = transformScale * MINIMAP_MAP_DISPLAY_SIZE / (zone->imageMax[0] - zone->imageMin[0]);
    s = sin(angle) * scale;
    c = cos(angle) * scale;

    //Simply a 2x2 rotoscale matrix
    transform[0] =  c;
    transform[1] =  s;
    transform[2] = -s;
    transform[3] =  c;

    //the minimap is shown with Z pointing to the viewer but OpenGL has Z pointing to the screen
    //thus the 2d axis don't have the same orientation
    posx = - cg.refdef.vieworg[0];
    posy = + cg.refdef.vieworg[1];

    //Compute the constant member of the transform
    x = transform[0] * posx + transform[1] * posy;
    y = transform[2] * posx + transform[3] * posy;

    transformVector[0] = x + rect->x + rect->w / 2;
    transformVector[1] = y + rect->y + rect->h / 2;
}

/*
================
CG_WorldToMinimap
================
*/
void CG_WorldToMinimap( const vec3_t worldPos, vec2_t minimapPos )
{
    //Correct the orientation by inverting worldPos.y
    minimapPos[0] = transform[0] * worldPos[0] - transform[1] * worldPos[1] +
                    transformVector[0];
    minimapPos[1] = transform[2] * worldPos[0] - transform[3] * worldPos[1] +
                    transformVector[1];
}

/*
================
CG_WorldToMinimapAngle
================
*/
float CG_WorldToMinimapAngle( float angle )
{
    return angle + transformAngle;
}

/*
================
CG_WorldToMinimapScale
================
*/
float CG_WorldToMinimapScale( float scale )
{
    return scale * transformScale;
}


/*
================
CG_DrawMinimapObject
================
*/
void CG_DrawMinimapObject( qhandle_t image, const vec3_t pos3d, float angle, float scale, float texSize )
{
    vec2_t offset;
    float x, y, wh;

    angle = CG_WorldToMinimapAngle( angle );
    scale = CG_WorldToMinimapScale( scale );

    CG_WorldToMinimap( pos3d, offset );
    x = - texSize/2 * scale + offset[0];
    y = - texSize/2 * scale + offset[1];
    wh = texSize * scale;

    trap_R_DrawRotatedPic( x, y, wh, wh, 0.0, 0.0, 1.0, 1.0, image, angle );
}

/*
================
CG_UpdateMinimapActive
================
*/
void CG_UpdateMinimapActive(minimap_t* m)
{
    qboolean active = m->defined && cg_drawMinimap.integer;

    m->active = active;

    if( cg_minimapActive.integer != active )
    {
        trap_Cvar_Set( "cg_minimapActive", va( "%d", active ) );
    }
}

//Other logical minimap functions

/*
================
CG_ChooseMinimapZone

Chooses the current zone, tries the last zone first
More than providing a performance improvement it helps
the mapper make a nicer looking minimap: once you enter a zone I'll stay in it
until you reach the bounds
================
*/
minimapZone_t* CG_ChooseMinimapZone( minimap_t* m )
{
    if( m->lastZone < 0 || !CG_IsInMinimapZone( &m->zones[m->lastZone] ) )
    {
        int i;
        for( i = 0; i < m->nZones; i++ )
        {
            if( CG_IsInMinimapZone( &m->zones[i] ) )
            {
                break;
            }
        }

        //The mapper should make sure this never happens but we prevent crashes
        //could also be used to make a default zone
        if( i >= m->nZones )
        {
            i = m->nZones - 1;
        }

        m->lastZone = i;

        return &m->zones[i];
    }
    else
    {
        return &m->zones[m->lastZone];
    }
}

/*
================
CG_MinimapDrawMap
================
*/
void CG_MinimapDrawMap( minimapZone_t* z )
{
    vec3_t origin = {0.0f, 0.0f, 0.0f};
    origin[0] = 0.5 * (z->imageMin[0] + z->imageMax[0]);
    origin[1] = 0.5 * (z->imageMin[1] + z->imageMax[1]);

    CG_DrawMinimapObject( z->image, origin, 90.0, 1.0, MINIMAP_MAP_DISPLAY_SIZE );
}

/*
================
CG_MinimapDrawPlayer
================
*/
void CG_MinimapDrawPlayer( minimap_t* m )
{
    CG_DrawMinimapObject( m->gfx.playerArrow, cg.refdef.vieworg, cg.refdefViewAngles[1], 1.0, MINIMAP_PLAYER_DISPLAY_SIZE );
}

/*
================
CG_MinimapUpdateTeammateFadingAndPos

When the player leaves the PVS we cannot track its movement on the minimap
anymore so we fade its arrow by keeping in memory its last know pos and angle.
When he comes back in the PVS we don't want to have to manage two arrows or to
make the arrow warp. That's why we wait until the fadeout is finished before
fading it back in.
================
*/
void CG_MinimapUpdateTeammateFadingAndPos( centity_t* mate )
{
    playerEntity_t* state = &mate->pe;

    if( state->minimapFadingOut )
    {
        if( state->minimapFading != 0.0f )
        {
            state->minimapFading = MAX( 0.0f, state->minimapFading - cg.frametime * MINIMAP_FADE_TIME );
        }

        if( state->minimapFading == 0.0f )
        {
            state->minimapFadingOut = qfalse;
        }
    }
    else
    {
        //The player is out of the PVS or is dead
        qboolean shouldStayVisible = mate->valid && ! ( mate->currentState.eFlags & EF_DEAD );

        if( !shouldStayVisible )
        {
            state->minimapFadingOut = qtrue;
        }
        else
        {
            if( state->minimapFading != 1.0f )
            {
                state->minimapFading = MIN( 1.0f, state->minimapFading + cg.frametime * MINIMAP_FADE_TIME );
            }

            //Copy the current state so that we can use it once the player is out of the PVS
            VectorCopy( mate->lerpOrigin, state->lastMinimapPos );
            state->lastMinimapAngle = mate->lerpAngles[1];
        }
    }
}

/*
================
CG_MinimapDrawTeammates
================
*/
void CG_MinimapDrawTeammates( minimap_t* m )
{
    int ownTeam = cg.predictedPlayerState.stats[ STAT_TEAM ];
    int i;

    for ( i = 0; i < MAX_GENTITIES; i++ )
    {
        centity_t* mate = &cg_entities[i];
        playerEntity_t* state = &mate->pe;

        int clientNum = mate->currentState.clientNum;

        qboolean isTeammate = mate->currentState.eType == ET_PLAYER && clientNum >= 0 && clientNum < MAX_CLIENTS && (mate->currentState.misc & 0x00FF) == ownTeam;

        if ( !isTeammate )
        {
            continue;
        }

        //We have a fading effect for teammates going out of the PVS
        CG_MinimapUpdateTeammateFadingAndPos( mate );

        //Draw the arrow for this teammate with the right fading
        if( state->minimapFading != 0.0f )
        {
            //Avoid doing 2 trap calls for setColor if we can
            if( state->minimapFading == 1.0f )
            {
                CG_DrawMinimapObject( m->gfx.teamArrow, state->lastMinimapPos, state->lastMinimapAngle, 1.0, 50.0 );
            }
            else
            {
                vec4_t fadeColor = {1.0f, 1.0f, 1.0f, 1.0f};
                fadeColor[3] = state->minimapFading;

                trap_R_SetColor( fadeColor );
                CG_DrawMinimapObject( m->gfx.teamArrow, state->lastMinimapPos, state->lastMinimapAngle, 1.0, MINIMAP_TEAMMATE_DISPLAY_SIZE );
                trap_R_SetColor( NULL );
            }
        }
    }
}

//Entry points in the minimap code

/*
================
CG_InitMinimap
================
*/
void CG_InitMinimap( void )
{
    minimap_t* m = &cg.minimap;

    m->defined = qtrue;

    if( !CG_ParseMinimap( m, va( "minimaps/%s.minimap", cgs.mapname ) ) )
    {
        m->defined = qfalse;
        CG_Printf( S_WARNING "could not parse the minimap, defaulting to no minimap.\n" );
    }
    else if( m->nZones == 0 )
    {
        m->defined = qfalse;
        CG_Printf( S_ERROR "the minimap did not define any zone.\n" );
    }

    m->gfx.playerArrow = trap_R_RegisterShader( "gfx/2d/player-arrow", RSF_DEFAULT );
    m->gfx.teamArrow = trap_R_RegisterShader( "gfx/2d/team-arrow", RSF_DEFAULT );

    CG_UpdateMinimapActive( m );
}

/*
================
CG_DrawMinimap
================
*/
void CG_DrawMinimap( const rectDef_t* rect640 )
{
    minimap_t* m = &cg.minimap;
    minimapZone_t *z = NULL;
    rectDef_t rect = *rect640;

    CG_UpdateMinimapActive( m );

    if( !m->active )
    {
        return;
    }

    z = CG_ChooseMinimapZone( m );

    //Setup the transform
    CG_AdjustFrom640( &rect.x, &rect.y, &rect.w, &rect.h );
    CG_SetupMinimapTransform( &rect, m, z );

    //Add the backgound
    CG_FillRect( rect640->x, rect640->y, rect640->w, rect640->h, m->bgColor );

    //Draw things inside the rectangle we were given
    CG_SetScissor( rect.x, rect.y, rect.w, rect.h );
    CG_EnableScissor( qtrue );
    {

        CG_MinimapDrawMap( z );
        CG_MinimapDrawPlayer( m );
        CG_MinimapDrawTeammates( m );
    }
    CG_EnableScissor( qfalse );
}
