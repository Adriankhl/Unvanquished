/*
===========================================================================

Daemon GPL Source Code
Copyright (C) 2012 Unvanquished Developers

This file is part of the Daemon GPL Source Code (Daemon Source Code).

Daemon Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Daemon Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Daemon Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following the
terms and conditions of the GNU General Public License which accompanied the Daemon
Source Code.  If not, please request a copy in writing from id Software at the address
below.

If you have questions concerning this license or the applicable additional terms, you
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville,
Maryland 20850 USA.

===========================================================================
*/

#include "cg_local.h"

static int GCD( int a, int b )
{
	int c;

	while ( b != 0 )
	{
		c = a % b;
		a = b;
		b = c;
	}

	return a;
}

static const char *DisplayAspectString( int w, int h )
{
	int gcd = GCD( w, h );

	w /= gcd;
	h /= gcd;

	// For some reason 8:5 is usually referred to as 16:10
	if ( w == 8 && h == 5 )
	{
		w = 16;
		h = 10;
	}

	return va( "%d:%d", w, h );
}

static void CG_Rocket_DFResolution( int handle, const char *data )
{
	int w = atoi( Info_ValueForKey(data, "1" ) );
	int h = atoi( Info_ValueForKey(data, "2" ) );
	char *aspectRatio = BG_strdup( DisplayAspectString( w, h ) );
	trap_Rocket_DataFormatterFormattedData( handle, va( "%dx%d ( %s )", w, h, aspectRatio ), qfalse );
	BG_Free( aspectRatio );
}

static void CG_Rocket_DFServerPing( int handle, const char *data )
{
	const char *str = Info_ValueForKey( data, "1" );
	trap_Rocket_DataFormatterFormattedData( handle, *str && Q_isnumeric( *str ) ? va( "%s ms", Info_ValueForKey( data, "1" ) ) : "", qfalse );
}

static void CG_Rocket_DFServerPlayers( int handle, const char *data )
{
	char max[ 4 ];
	Q_strncpyz( max, Info_ValueForKey( data, "3" ), sizeof( max ) );
	trap_Rocket_DataFormatterFormattedData( handle, va( "%s + (%s) / %s", Info_ValueForKey( data, "1" ), Info_ValueForKey( data, "2" ), max ), qtrue );
}

static void CG_Rocket_DFPlayerName( int handle, const char *data )
{
	trap_Rocket_DataFormatterFormattedData( handle, cgs.clientinfo[ atoi( Info_ValueForKey( data, "1" ) ) ].name, qtrue );
}

static void CG_Rocket_DFUpgradeName( int handle, const char *data )
{
	trap_Rocket_DataFormatterFormattedData( handle, BG_Upgrade( atoi( Info_ValueForKey( data, "1" ) ) )->humanName, qtrue );
}

static void CG_Rocket_DFWeaponName( int handle, const char *data )
{
	trap_Rocket_DataFormatterFormattedData( handle, BG_Weapon( atoi( Info_ValueForKey( data, "1" ) ) )->humanName, qtrue );
}

static void CG_Rocket_DFClassName( int handle, const char *data )
{
	trap_Rocket_DataFormatterFormattedData( handle, BG_Class( atoi( Info_ValueForKey( data, "1" ) ) )->name, qtrue );
}

static void CG_Rocket_DFServerLabel( int handle, const char *data )
{
	const char *str = Info_ValueForKey( data, "1" );
	trap_Rocket_DataFormatterFormattedData( handle, *data ? ++str : "&nbsp;", qfalse );
}

static void CG_Rocket_DFCMArmouryBuyWeapon( int handle, const char *data )
{
	weapon_t weapon = atoi( Info_ValueForKey( data, "1" ) );

	trap_Rocket_DataFormatterFormattedData( handle, va( "<button class='armourybuy' onClick='setDS armouryBuyList weapons %s; execDS armouryBuyList weapons'><img src='/%s'/></button>", Info_ValueForKey( data, "2" ), CG_GetShaderNameFromHandle( cg_weapons[ weapon ].ammoIcon ) ), qfalse );
}

static void CG_Rocket_DFCMArmouryBuyUpgrade( int handle, const char *data )
{
	upgrade_t upgrade = atoi( Info_ValueForKey( data, "1" ) );

	trap_Rocket_DataFormatterFormattedData( handle, va( "<button class='armourybuy' onClick='setDS armouryBuyList upgrades %s; execDS armouryBuyList upgrades'><img src='/%s'/></button>", Info_ValueForKey( data, "2" ), CG_GetShaderNameFromHandle( cg_upgrades[ upgrade ].upgradeIcon ) ), qfalse );
}

typedef struct
{
	const char *name;
	void ( *exec ) ( int handle, const char *data );
} dataFormatterCmd_t;

static const dataFormatterCmd_t dataFormatterCmdList[] =
{
	{ "ClassName", &CG_Rocket_DFClassName },
	{ "CMArmouryBuyUpgrades", &CG_Rocket_DFCMArmouryBuyUpgrade },
	{ "CMArmouryBuyWeapons", &CG_Rocket_DFCMArmouryBuyWeapon },
	{ "PlayerName", &CG_Rocket_DFPlayerName },
	{ "Resolution", &CG_Rocket_DFResolution },
	{ "ServerLabel", &CG_Rocket_DFServerLabel },
	{ "ServerPing", &CG_Rocket_DFServerPing },
	{ "ServerPlayers", &CG_Rocket_DFServerPlayers },
	{ "UpgradeName", &CG_Rocket_DFUpgradeName },
	{ "WeaponName", &CG_Rocket_DFWeaponName }
};

static const size_t dataFormatterCmdListCount = ARRAY_LEN( dataFormatterCmdList );

static int dataFormatterCmdCmp( const void *a, const void *b )
{
	return Q_stricmp( ( const char * ) a, ( ( dataFormatterCmd_t * ) b )->name );
}

void CG_Rocket_FormatData( int handle )
{
	static char name[ 200 ], data[ BIG_INFO_STRING ];
	dataFormatterCmd_t *cmd;

	trap_Rocket_DataFormatterRawData( handle, name, sizeof( name ), data, sizeof( data ) );

	cmd = bsearch( name, dataFormatterCmdList, dataFormatterCmdListCount, sizeof( dataFormatterCmd_t ), dataFormatterCmdCmp );

	if ( cmd )
	{
		cmd->exec( handle, data );
	}
}

void CG_Rocket_RegisterDataFormatters( void )
{
	int i;

	for ( i = 0; i < dataFormatterCmdListCount; i++ )
	{
		trap_Rocket_RegisterDataFormatter( dataFormatterCmdList[ i ].name );
	}
}
