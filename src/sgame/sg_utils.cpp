/*
===========================================================================

Unvanquished GPL Source Code
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2000-2009 Darklegion Development

This file is part of the Unvanquished GPL Source Code (Unvanquished Source Code).

Unvanquished is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Unvanquished is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Unvanquished; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

===========================================================================
*/

// sg_utils.c -- misc utility functions for game module

#include "sg_local.h"
#include "Entities.h"

#include <glm/geometric.hpp>

struct shaderRemap_t
{
	char  oldShader[ MAX_QPATH ];
	char  newShader[ MAX_QPATH ];
	float timeOffset;
};

#define MAX_SHADER_REMAPS 128

int           remapCount = 0;
shaderRemap_t remappedShaders[ MAX_SHADER_REMAPS ];

void G_SetShaderRemap( const char *oldShader, const char *newShader, float timeOffset )
{
	int i;

	for ( i = 0; i < remapCount; i++ )
	{
		if ( Q_stricmp( oldShader, remappedShaders[ i ].oldShader ) == 0 )
		{
			// found it, just update this one
			strncpy( remappedShaders[ i ].newShader, newShader, MAX_QPATH );
			remappedShaders[ i ].newShader[ MAX_QPATH - 1 ] = '\0';
			remappedShaders[ i ].timeOffset = timeOffset;
			return;
		}
	}

	if ( remapCount < MAX_SHADER_REMAPS )
	{
		strncpy( remappedShaders[ remapCount ].newShader, newShader, MAX_QPATH );
		remappedShaders[ remapCount ].newShader[ MAX_QPATH - 1 ] = '\0';
		strncpy( remappedShaders[ remapCount ].oldShader, oldShader, MAX_QPATH );
		remappedShaders[ remapCount ].oldShader[ MAX_QPATH - 1 ] = '\0';
		remappedShaders[ remapCount ].timeOffset = timeOffset;
		remapCount++;
	}
}

const char *BuildShaderStateConfig()
{
	static char buff[ MAX_STRING_CHARS * 4 ];
	char        out[ MAX_QPATH * 2 + 5 ];
	int         i;

	memset( buff, 0, sizeof(buff) );

	for ( i = 0; i < remapCount; i++ )
	{
		Com_sprintf( out, sizeof( out ), "%s=%s:%5.2f@", remappedShaders[ i ].oldShader,
		             remappedShaders[ i ].newShader, remappedShaders[ i ].timeOffset );
		Q_strcat( buff, sizeof( buff ), out );
	}

	return buff;
}

/*
=========================================================================

model / sound configstring indexes

=========================================================================
*/

/*
================
G_FindConfigstringIndex

================
*/
static int G_FindConfigstringIndex( const char *name, int start, int max, bool create )
{
	int  i;
	char s[ MAX_STRING_CHARS ];

	if ( !name || !name[ 0 ] )
	{
		return 0;
	}

	for ( i = 1; i < max; i++ )
	{
		trap_GetConfigstring( start + i, s, sizeof( s ) );

		if ( !s[ 0 ] )
		{
			break;
		}

		if ( !strcmp( s, name ) )
		{
			return i;
		}
	}

	if ( !create )
	{
		return 0;
	}

	if ( i == max )
	{
		Sys::Drop( "G_FindConfigstringIndex: overflow" );
	}

	trap_SetConfigstring( start + i, name );

	return i;
}

int G_ParticleSystemIndex( const char *name )
{
	return G_FindConfigstringIndex( name, CS_PARTICLE_SYSTEMS, MAX_GAME_PARTICLE_SYSTEMS, true );
}

int G_ShaderIndex( const char *name )
{
	return G_FindConfigstringIndex( name, CS_SHADERS, MAX_GAME_SHADERS, true );
}

int G_ModelIndex( const char *name )
{
	return G_FindConfigstringIndex( name, CS_MODELS, MAX_MODELS, true );
}

int G_SoundIndex( const char *name )
{
	return G_FindConfigstringIndex( name, CS_SOUNDS, MAX_SOUNDS, true );
}

/**
 * searches for a the grading texture with the given name among the configstrings and returns the index
 * if it wasn't found it will add the texture to the configstrings, send these to the client and return the new index
 *
 * the first one at CS_GRADING_TEXTURES is always the global one, so we start searching from CS_GRADING_TEXTURES+1
 */
int G_GradingTextureIndex( const char *name )
{
	return G_FindConfigstringIndex( name, CS_GRADING_TEXTURES+1, MAX_GRADING_TEXTURES-1, true );
}

int G_ReverbEffectIndex( const char *name )
{
	return G_FindConfigstringIndex( name, CS_REVERB_EFFECTS+1, MAX_REVERB_EFFECTS-1, true );
}

int G_LocationIndex( const char *name )
{
	return G_FindConfigstringIndex( name, CS_LOCATIONS, MAX_LOCATIONS, true );
}

/*
=============
VectorToString

This is just a convenience function
for printing vectors
=============
*/
char *vtos( const vec3_t v )
{
	static  int  index;
	static  char str[ 8 ][ 32 ];
	char         *s;

	// use an array so that multiple vtos won't collide
	s = str[ index ];
	index = ( index + 1 ) & 7;

	Com_sprintf( s, 32, "(%i %i %i)", ( int ) v[ 0 ], ( int ) v[ 1 ], ( int ) v[ 2 ] );

	return s;
}

/*
=============
G_TeleportPlayer
teleports the player to another location
=============
*/
void G_TeleportPlayer( gentity_t *player, vec3_t origin, vec3_t angles, float speed )
{
	// unlink to make sure it can't possibly interfere with G_KillBox
	trap_UnlinkEntity( player );

	VectorCopy( origin, player->client->ps.origin );
	player->client->ps.groundEntityNum = ENTITYNUM_NONE;

	AngleVectors( angles, player->client->ps.velocity, nullptr, nullptr );
	VectorScale( player->client->ps.velocity, speed, player->client->ps.velocity );
	player->client->ps.pm_time = 0.4f * std::abs( speed ); // duration of loss of control
	if ( player->client->ps.pm_time > 160 )
		player->client->ps.pm_time = 160;
	if ( player->client->ps.pm_time != 0 )
		player->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;

	// toggle the teleport bit so the client knows to not lerp
	player->client->ps.eFlags ^= EF_TELEPORT_BIT;
	G_UnlaggedClear( player );

	// cut all relevant zap beams
	G_ClearPlayerZapEffects( player );

	// set angles
	G_SetClientViewAngle( player, angles );

	// save results of pmove
	BG_PlayerStateToEntityState( &player->client->ps, &player->s, true );

	// use the precise origin for linking
	VectorCopy( player->client->ps.origin, player->r.currentOrigin );

	if ( player->client->sess.spectatorState == SPECTATOR_NOT )
	{
		// kill anything at the destination
		G_KillBox( player );

		trap_LinkEntity( player );
	}
}

/*
==============================================================================

Kill box

==============================================================================
*/

/*
=================
G_KillBox

Kills all entities overlapping with `ent`.
=================
*/
void G_KillBox( gentity_t *ent )
{
	int       i, num;
	int       touch[ MAX_GENTITIES ];
	gentity_t *hit;
	vec3_t    mins, maxs;

	VectorAdd( ent->r.currentOrigin, ent->r.mins, mins );
	VectorAdd( ent->r.currentOrigin, ent->r.maxs, maxs );
	num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

	for ( i = 0; i < num; i++ )
	{
		hit = &g_entities[ touch[ i ] ];

		// impossible to telefrag self
		if ( ent == hit )
		{
			continue;
		}

		Entities::Kill(hit, ent, MOD_TELEFRAG);
	}
}

/*
====================
G_KillBrushModel
====================
*/
void G_KillBrushModel( gentity_t *ent, gentity_t *activator )
{
  gentity_t *e;
  vec3_t mins, maxs;
  trace_t tr;

  for( e = &g_entities[ 0 ]; e < &g_entities[ level.num_entities ]; ++e )
  {
    if( !e->r.linked || !e->clipmask )
      continue;

    VectorAdd( e->r.currentOrigin, e->r.mins, mins );
    VectorAdd( e->r.currentOrigin, e->r.maxs, maxs );

    if( !trap_EntityContact( mins, maxs, ent ) )
      continue;

    trap_Trace( &tr, e->r.currentOrigin, e->r.mins, e->r.maxs,
                e->r.currentOrigin, e->num(), e->clipmask, 0 );

	if( tr.entityNum != ENTITYNUM_NONE ) {
	  Entities::Kill(e, activator, MOD_CRUSH);
	}
  }
}

//==============================================================================

/*
===============
G_AddPredictableEvent

Use for non-pmove events that would also be predicted on the
client side: jumppads and item pickups
Adds an event+parm and twiddles the event counter
===============
*/
void G_AddPredictableEvent( gentity_t *ent, int event, int eventParm )
{
	if ( !ent->client )
	{
		return;
	}

	BG_AddPredictableEventToPlayerstate( event, eventParm, &ent->client->ps );
}

/*
===============
G_AddEvent

Adds an event+parm and twiddles the event counter
===============
*/
void G_AddEvent( gentity_t *ent, int event, int eventParm )
{
	int bits;

	if ( !event )
	{
		Log::Warn( "G_AddEvent: zero event added for entity %i", ent->num() );
		return;
	}

	// eventParm is converted to uint8_t (0 - 255) in msg.c
	if ( eventParm & ~0xFF )
	{
		Log::Warn( "G_AddEvent( %s ) has eventParm %d, "
		          "which will overflow", BG_EventName( event ), eventParm );
	}

	// clients need to add the event in playerState_t instead of entityState_t
	if ( ent->client )
	{
		ent->client->ps.events[ ent->client->ps.eventSequence & ( MAX_EVENTS - 1 ) ] = event;
		ent->client->ps.eventParms[ ent->client->ps.eventSequence & ( MAX_EVENTS - 1 ) ] = eventParm;
		ent->client->ps.eventSequence++;
	}
	else
	{
		bits = ent->s.event & EV_EVENT_BITS;
		bits = ( bits + EV_EVENT_BIT1 ) & EV_EVENT_BITS;
		ent->s.event = event | bits;
		ent->s.eventParm = eventParm;
	}

	ent->eventTime = level.time;
}

/*
===============
G_BroadcastEvent

Sends an event to every client
===============
*/
void G_BroadcastEvent( int event, int eventParm, team_t team )
{
	gentity_t *ent;

	ent = G_NewTempEntity( VEC2GLM( vec3_origin ), event );
	ent->s.eventParm = eventParm;

	if ( team )
	{
		G_TeamToClientmask( team, &ent->r.loMask, &ent->r.hiMask );
		ent->r.svFlags = SVF_BROADCAST | SVF_CLIENTMASK;
	}
	else
	{
		ent->r.svFlags = SVF_BROADCAST;
	}
}

/*
=============
G_Sound
=============
*/
void G_Sound( gentity_t *ent, soundChannel_t, int soundIndex )
{
	gentity_t *te;

	te = G_NewTempEntity( VEC2GLM( ent->r.currentOrigin ), EV_GENERAL_SOUND );
	te->s.eventParm = soundIndex;
}

/*
=============
G_ClientIsLagging
=============
*/
bool G_ClientIsLagging( gclient_t *client )
{
	if ( client )
	{
		if ( client->ps.ping >= 999 || client->ps.ping == 0 )
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	return false; //is a non-existent client lagging? woooo zen
}

//==============================================================================

/*
===============
G_TriggerMenu

Trigger a menu on some client
===============
*/
void G_TriggerMenu( int clientNum, dynMenu_t menu )
{
	char buffer[ 32 ];

	Com_sprintf( buffer, sizeof( buffer ), "servermenu %d", menu );
	trap_SendServerCommand( clientNum, buffer );
}

/*
===============
G_TriggerMenuArgs

Trigger a menu on some client and passes an argument
===============
*/
void G_TriggerMenuArgs( int clientNum, dynMenu_t menu, int arg )
{
	char buffer[ 64 ];

	Com_sprintf( buffer, sizeof( buffer ), "servermenu %d %d", menu, arg );
	trap_SendServerCommand( clientNum, buffer );
}

/*
===============
G_CloseMenus

Close all open menus on some client
===============
*/
void G_CloseMenus( int clientNum )
{
	char buffer[ 32 ];

	Com_sprintf( buffer, 32, "serverclosemenus" );
	trap_SendServerCommand( clientNum, buffer );
}

/*
===============
G_AddressParse

Make an IP address more usable
===============
*/
static const char *addr4parse( const char *str, addr_t *addr )
{
	int i;
	int octet = 0;
	int num = 0;
	memset( addr, 0, sizeof( addr_t ) );
	addr->type = IPv4;

	for ( i = 0; octet < 4; i++ )
	{
		if ( Str::cisdigit( str[ i ] ) )
		{
			num = num * 10 + str[ i ] - '0';
		}
		else
		{
			if ( num < 0 || num > 255 )
			{
				return nullptr;
			}

			addr->addr[ octet ] = ( byte ) num;
			octet++;

			if ( str[ i ] != '.' || str[ i + 1 ] == '.' )
			{
				break;
			}

			num = 0;
		}
	}

	return str + i;
}

static const char *addr6parse( const char *str, addr_t *addr )
{
	int      i;
	bool seen = false;

	/* keep track of the parts before and after the ::
	   it's either this or even uglier hacks */
	byte   a[ ADDRLEN ], b[ ADDRLEN ];
	size_t before = 0, after = 0;
	int    num = 0;

	/* 8 hexadectets unless :: is present */
	for ( i = 0; before + after <= 8; i++ )
	{
		//num = num << 4 | str[ i ] - '0';
		if ( Str::cisdigit( str[ i ] ) )
		{
			num = num * 16 + str[ i ] - '0';
		}
		else if ( str[ i ] >= 'A' && str[ i ] <= 'F' )
		{
			num = num * 16 + 10 + str[ i ] - 'A';
		}
		else if ( str[ i ] >= 'a' && str[ i ] <= 'f' )
		{
			num = num * 16 + 10 + str[ i ] - 'a';
		}
		else
		{
			if ( num < 0 || num > 65535 )
			{
				return nullptr;
			}

			if ( i == 0 )
			{
				//
			}
			else if ( seen ) // :: has been seen already
			{
				b[ after * 2 ] = num >> 8;
				b[ after * 2 + 1 ] = num & 0xff;
				after++;
			}
			else
			{
				a[ before * 2 ] = num >> 8;
				a[ before * 2 + 1 ] = num & 0xff;
				before++;
			}

			if ( !str[ i ] )
			{
				break;
			}

			if ( str[ i ] != ':' || before + after == 8 )
			{
				break;
			}

			if ( str[ i + 1 ] == ':' )
			{
				// ::: or multiple ::
				if ( seen || str[ i + 2 ] == ':' )
				{
					break;
				}

				seen = true;
				i++;
			}
			else if ( i == 0 ) // starts with : but not ::
			{
				return nullptr;
			}

			num = 0;
		}
	}

	if ( seen )
	{
		// there have to be fewer than 8 hexadectets when :: is present
		if ( before + after == 8 )
		{
			return nullptr;
		}
	}
	else if ( before + after < 8 ) // require exactly 8 hexadectets
	{
		return nullptr;
	}

	memset( addr, 0, sizeof( addr_t ) );
	addr->type = IPv6;

	if ( before )
	{
		memcpy( addr->addr, a, before * 2 );
	}

	if ( after )
	{
		memcpy( addr->addr + ADDRLEN - 2 * after, b, after * 2 );
	}

	return str + i;
}

bool G_AddressParse( const char *str, addr_t *addr )
{
	const char *p;
	int        max;

	if ( strchr( str, ':' ) )
	{
		p = addr6parse( str, addr );
		max = 64;
	}
	else if ( strchr( str, '.' ) )
	{
		p = addr4parse( str, addr );
		max = 32;
	}
	else
	{
		return false;
	}

	Q_strncpyz( addr->str, str, sizeof( addr->str ) );

	if ( !p )
	{
		return false;
	}

	if ( *p == '/' )
	{
		addr->mask = atoi( p + 1 );

		if ( addr->mask < 1 || addr->mask > max )
		{
			addr->mask = max;
		}
	}
	else
	{
		if ( *p )
		{
			return false;
		}

		addr->mask = max;
	}

	return true;
}

/*
===============
G_AddressCompare

Based largely on NET_CompareBaseAdrMask from ioq3 revision 1557
===============
*/
bool G_AddressCompare( const addr_t *a, const addr_t *b )
{
	int i, netmask;

	if ( a->type != b->type )
	{
		return false;
	}

	netmask = a->mask;

	if ( a->type == IPv4 )
	{
		if ( netmask < 1 || netmask > 32 )
		{
			netmask = 32;
		}
	}
	else if ( a->type == IPv6 )
	{
		if ( netmask < 1 || netmask > 128 )
		{
			netmask = 128;
		}
	}

	for ( i = 0; netmask > 7; i++, netmask -= 8 )
	{
		if ( a->addr[ i ] != b->addr[ i ] )
		{
			return false;
		}
	}

	if ( netmask )
	{
		netmask = ( ( 1 << netmask ) - 1 ) << ( 8 - netmask );
		return ( a->addr[ i ] & netmask ) == ( b->addr[ i ] & netmask );
	}

	return true;
}

/*
===============
G_ClientnumToMask

Calculates loMask/hiMask as used by SVF_CLIENTMASK type events to match only the given client.
===============
*/
void G_ClientnumToMask( int clientNum, int *loMask, int *hiMask )
{
	*loMask = *hiMask = 0;

	if ( clientNum < 32 )
	{
		*loMask |= BIT( clientNum );
	}
	else
	{
		*hiMask |= BIT( clientNum - 32 );
	}
}

/*
===============
G_TeamToClientmask

Calculates loMask/hiMask as used by SVF_CLIENTMASK type events to match all clients in a team.
===============
*/
void G_TeamToClientmask( team_t team, int *loMask, int *hiMask )
{
	int       clientNum;
	gclient_t *client;

	*loMask = *hiMask = 0;

	for ( clientNum = 0; clientNum < level.maxclients; clientNum++ )
	{
		client = &g_clients[ clientNum ];

		if ( client->pers.team == team )
		{
			if ( clientNum < 32 )
			{
				*loMask |= BIT( clientNum );
			}
			else
			{
				*hiMask |= BIT( clientNum - 32 );
			}
		}
	}
}

bool G_LineOfSight( const gentity_t *from, const gentity_t *to, int mask, bool useTrajBase )
{
	trace_t trace;

	if ( !from || !to )
	{
		return false;
	}

	trap_Trace( &trace, useTrajBase ? from->s.pos.trBase : from->s.origin, nullptr, nullptr, to->s.origin,
	            from->num(), mask, 0 );

	// Also check for fraction in case the mask is chosen so that the trace skips the target entity
	return ( trace.entityNum == to->num() || trace.fraction == 1.0f );
}

/**
 * @return Wheter a shot from the source's origin towards the target's origin would hit the target.
 */
bool G_LineOfSight( const gentity_t *from, const gentity_t *to )
{
	return G_LineOfSight( from, to, MASK_SHOT, false );
}

/**
 * @return Wheter a shot from the source's trajectory base towards the target's origin would hit the
 *         target.
 */
bool G_LineOfFire( const gentity_t *from, const gentity_t *to )
{
	return G_LineOfSight( from, to, MASK_SHOT, true );
}

/**
 * @brief This version of line of sight only considers map geometry, including movers.
 * @return Whether a line from one point to the other would intersect the world.
 */
bool G_LineOfSight( const vec3_t point1, const vec3_t point2 )
{
	trace_t trace;

	trap_Trace( &trace, point1, nullptr, nullptr, point2, ENTITYNUM_NONE, MASK_SOLID, 0 );

	return ( trace.entityNum != ENTITYNUM_WORLD );
}

bool G_IsPlayableTeam( team_t team )
{
	return ( team > TEAM_NONE && team < NUM_TEAMS );
}

bool G_IsPlayableTeam( int team )
{
	return G_IsPlayableTeam( (team_t)team );
}

team_t G_IterateTeams( team_t team )
{
	team_t nextTeam = (team_t)(std::max((int)team, (int)TEAM_NONE) + 1);

	if ( nextTeam >= NUM_TEAMS )
	{
		return TEAM_NONE;
	}
	else
	{
		return nextTeam;
	}
}

std::string G_EscapeServerCommandArg( Str::StringRef str )
{
	if ( str.find( '\n' ) == str.npos )
	{
		return Cmd::Escape( str );
	}

	std::string out = "\"";
	for (char c : str)
	{
		if ( c == '\\' || c == '$' || c == '"' )
		{
			out.push_back( '\\' );
		}
		out.push_back( c );
	}
	out.push_back( '"' );
	return out;
}

// Escape a command for used in server commands (sent from client to server)
// Difference from Cmd::Escape and normal command parsing is that newlines are allowed
// (for commands that have multi-line output)
char *Quote( Str::StringRef str )
{
	static char buffer[ 4 ][ MAX_STRING_CHARS ];
	static int index = -1;

	index = ( index + 1 ) & 3;
	Q_strncpyz( buffer[ index ], G_EscapeServerCommandArg( str ).c_str(), sizeof( buffer[ index ] ) );

	return buffer[ index ];
}

// TODO: Add LocationComponent
float G_Distance( gentity_t *ent1, gentity_t *ent2 ) {
	return Distance(ent1->s.origin, ent2->s.origin);
}

float G_DistanceToBBox( const vec3_t origin, gentity_t* ent )
{
	float distanceSquared = 0.0f;
	for ( int i = 0; i < 3; i++ )
	{
		if ( origin[ i ] < ent->r.absmin [i ] )
		{
			distanceSquared += Square( ent->r.absmin[ i ] - origin[ i ] );
		}
		else if ( origin[ i ] > ent->r.absmax[ i ] )
		{
			distanceSquared += Square( origin[ i ] - ent->r.absmax[ i ] );
		}
	}
	return sqrtf( distanceSquared );
}

bool G_IsOnFire( const gentity_t *ent )
{
	return ent->s.eFlags & EF_B_ONFIRE;
}

/*
===============
Set muzzle location relative to pivoting eye.
===============
*/
glm::vec3 G_CalcMuzzlePoint( const gentity_t *self, const glm::vec3 &forward )
{
	glm::vec3 muzzlePoint = VEC2GLM( self->client->ps.origin );
	glm::vec3 normal = BG_GetClientNormal( &self->client->ps );
	muzzlePoint += static_cast<float>( self->client->ps.viewheight ) * normal;
	muzzlePoint += 1.0f * forward;
	// snap to integer coordinates for more efficient network bandwidth usage
	// Meh. I doubt it saves much. Casting to short ints might have, though. (copypaste)
	return glm::floor( muzzlePoint + 0.5f );
}
