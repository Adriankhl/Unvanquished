/*
===========================================================================

Unvanquished GPL Source Code
Copyright (C) 1999-2005 Id Software, Inc.

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

#include "sg_bot_ai.h"
#include "sg_bot_util.h"
#include "botlib/bot_api.h"
#include "Entities.h"
#include "CBSE.h"

#include <glm/gtx/norm.hpp>

/*
======================
g_bot_ai.c

This file contains the implementation of the different behavior tree nodes

On each frame, the behavior tree for each bot is evaluated starting from the root node
Each node returns either STATUS_SUCCESS, STATUS_RUNNING, or STATUS_FAILURE depending on their logic
The return values are used in various sequences and selectors to change the execution of the tree
======================
*/

bool isBinaryOp( AIOpType_t op )
{
	switch ( op )
	{
		case OP_GREATERTHAN:
		case OP_GREATERTHANEQUAL:
		case OP_LESSTHAN:
		case OP_LESSTHANEQUAL:
		case OP_EQUAL:
		case OP_NEQUAL:
		case OP_AND:
		case OP_OR:
			return true;
		default: return false;
	}
}

bool isUnaryOp( AIOpType_t op )
{
	return op == OP_NOT;
}

// functions for using values specified in the bt
AIValue_t AIBoxFloat( float f )
{
	AIValue_t t;
	t.expType = EX_VALUE;
	t.valType = VALUE_FLOAT;
	t.l.floatValue = f;
	return t;
}

AIValue_t AIBoxInt( int i )
{
	AIValue_t t;
	t.expType = EX_VALUE;
	t.valType = VALUE_INT;
	t.l.intValue = i;
	return t;
}

AIValue_t AIBoxString( char *s )
{
	AIValue_t t;
	t.expType = EX_VALUE;
	t.valType = VALUE_STRING;
	t.l.stringValue = BG_strdup( s );
	return t;
}

float AIUnBoxFloat( AIValue_t v )
{
	switch ( v.valType )
	{
		case VALUE_FLOAT:
			return v.l.floatValue;
		case VALUE_INT:
			return ( float ) v.l.intValue;
		default:
			return 0.0f;
	}
}

int AIUnBoxInt( AIValue_t v )
{
	switch ( v.valType )
	{
		case VALUE_FLOAT:
			return ( int ) v.l.floatValue;
		case VALUE_INT:
			return v.l.intValue;
		default:
			return 0;
	}
}

const char *AIUnBoxString( AIValue_t v )
{
	static char empty[] = "";

	switch ( v.valType )
	{
		case VALUE_FLOAT:
			return va( "%f", v.l.floatValue );
		case VALUE_INT:
			return va( "%d", v.l.intValue );
		case VALUE_STRING:
			return v.l.stringValue;
		default:
			return empty;
	}
}

double AIUnBoxDouble( AIValue_t v )
{
	switch ( v.valType )
	{
		case VALUE_FLOAT:
			return ( double ) v.l.floatValue;
		case VALUE_INT:
			return ( double ) v.l.intValue;
		default:
			return 0.0;
	}
}

void AIDestroyValue( AIValue_t v )
{
	switch ( v.valType )
	{
		case VALUE_STRING:
			BG_Free( v.l.stringValue );
			break;

		default:
			break;
	}
}

// Closest alive, but (unlike the botMind->closestBuildings) not necessarily active building
static botEntityAndDistance_t ClosestBuilding(gentity_t *self, bool alignment)
{
	botEntityAndDistance_t result;
	result.distance = HUGE_QFLT;
	result.ent = nullptr;
	for (Entity& e : Entities::Having<BuildableComponent>()) {
		if (!e.Get<HealthComponent>()->Alive() ||
		    (e.Get<TeamComponent>()->Team() == G_Team(self)) != alignment) {
			continue;
		}
		float distance = G_Distance(self, e.oldEnt);
		if (distance < result.distance) {
			result.distance = distance;
			result.ent = e.oldEnt;
		}
	}
	return result;
}

botEntityAndDistance_t AIEntityToGentity( gentity_t *self, AIEntity_t e )
{
	static const botEntityAndDistance_t nullEntity = { nullptr, HUGE_QFLT };
	botEntityAndDistance_t              ret = nullEntity;

	if ( e > E_NONE && e < E_NUM_BUILDABLES )
	{
		return self->botMind->closestBuildings[ e ];
	}

	switch ( e )
	{
	case E_NONE:
		return ret;

	case E_ENEMY:
		return self->botMind->bestEnemy;

	case E_DAMAGEDBUILDING:
		return self->botMind->closestDamagedBuilding;

	case E_FRIENDLYBUILDING:
		return ClosestBuilding( self, true );

	case E_ENEMYBUILDING:
		return ClosestBuilding( self, false );

	case E_GOAL:
		if (self->botMind->goal.targetsValidEntity()) {
			ret.ent = self->botMind->goal.getTargetedEntity();
			ret.distance = DistanceToGoal( self );
		}
		return ret;

	case E_SELF:
		ret.ent = self;
		ret.distance = 0;
		return ret;

	default:
		Log::Warn("Unknown AIEntity_t %d", e);
		return ret;
	}
}

static bool NodeIsRunning( gentity_t *self, AIGenericNode_t *node )
{
	auto &nodes = self->botMind->runningNodes;
	return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

/*
======================
Sequences and Selectors

A sequence or selector contains a list of child nodes which are evaluated
based on a combination of the child node return values and the internal logic
of the sequence or selector

A selector evaluates its child nodes like an if ( ) else if ( ) loop
It starts at the first child node, and if the node did not fail, it returns its status
if the node failed, it evaluates the next child node in the list
A selector will fail if all of its child nodes fail

A sequence evaluates its child nodes like a series of statements
It starts at the first previously running child node, and if the node does not succeed, it returns its status
If the node succeeded, it evaluates the next child node in the list
A sequence will succeed if all of its child nodes succeed

A concurrent node will always evaluate all of its child nodes unless one fails
if one fails, the concurrent node will stop executing nodes and return failure
A concurrent node succeeds if none of its child nodes fail
======================
*/
AINodeStatus_t BotSelectorNode( gentity_t *self, AIGenericNode_t *node )
{
	AINodeList_t *selector = ( AINodeList_t * ) node;
	int i = 0;

	for ( ; i < selector->numNodes; i++ )
	{
		AINodeStatus_t status = BotEvaluateNode( self, selector->list[ i ] );
		if ( status == STATUS_FAILURE )
		{
			continue;
		}
		return status;
	}
	return STATUS_FAILURE;
}

AINodeStatus_t BotFallbackNode( gentity_t *self, AIGenericNode_t *node )
{
	AINodeList_t *sequence = ( AINodeList_t * ) node;
	int start = 0;

	// find a previously running node and start there
	for ( int i = sequence->numNodes - 1; i > 0; i-- )
	{
		if ( NodeIsRunning( self, sequence->list[ i ] ) )
		{
			start = i;
			break;
		}
	}

	for ( int i = start; i < sequence->numNodes; i++ )
	{
		AINodeStatus_t status = BotEvaluateNode( self, sequence->list[ i ] );
		if ( status == STATUS_SUCCESS )
		{
			return STATUS_SUCCESS;
		}

		if ( status == STATUS_RUNNING )
		{
			return STATUS_RUNNING;
		}
	}
	return STATUS_FAILURE;
}

AINodeStatus_t BotSequenceNode( gentity_t *self, AIGenericNode_t *node )
{
	AINodeList_t *sequence = ( AINodeList_t * ) node;
	int start = 0;

	// find a previously running node and start there
	for ( int i = sequence->numNodes - 1; i > 0; i-- )
	{
		if ( NodeIsRunning( self, sequence->list[ i ] ) )
		{
			start = i;
			break;
		}
	}

	for ( int i = start; i < sequence->numNodes; i++ )
	{
		AINodeStatus_t status = BotEvaluateNode( self, sequence->list[ i ] );
		if ( status == STATUS_FAILURE )
		{
			return STATUS_FAILURE;
		}

		if ( status == STATUS_RUNNING )
		{
			return STATUS_RUNNING;
		}
	}
	return STATUS_SUCCESS;
}

AINodeStatus_t BotConcurrentNode( gentity_t *self, AIGenericNode_t *node )
{
	AINodeList_t *con = ( AINodeList_t * ) node;
	int i = 0;

	for ( ; i < con->numNodes; i++ )
	{
		AINodeStatus_t status = BotEvaluateNode( self, con->list[ i ] );

		if ( status == STATUS_FAILURE )
		{
			return STATUS_FAILURE;
		}
	}
	return STATUS_SUCCESS;
}

/*
======================
Decorators

Decorators are used to add functionality to the child node
======================
*/

AINodeStatus_t BotDecoratorInvert( gentity_t *self, AIGenericNode_t *node )
{
	AIDecoratorNode_t *dec = ( AIDecoratorNode_t * ) node;

	AINodeStatus_t status = BotEvaluateNode( self, dec->child );

	if ( status == STATUS_SUCCESS )
		return STATUS_FAILURE;

	if ( status == STATUS_FAILURE )
		return STATUS_SUCCESS;

	return status;
}

AINodeStatus_t BotDecoratorTimer( gentity_t *self, AIGenericNode_t *node )
{
	AIDecoratorNode_t *dec = ( AIDecoratorNode_t * ) node;

	if ( level.time > dec->data[ self->s.number ] )
	{
		AINodeStatus_t status = BotEvaluateNode( self, dec->child );

		if ( status == STATUS_FAILURE )
		{
			dec->data[ self->s.number ] = level.time + AIUnBoxInt( dec->params[ 0 ] );
		}

		return status;
	}

	return STATUS_FAILURE;
}

AINodeStatus_t BotDecoratorReturn( gentity_t *self, AIGenericNode_t *node )
{
	AIDecoratorNode_t *dec = ( AIDecoratorNode_t * ) node;

	AINodeStatus_t status = ( AINodeStatus_t ) AIUnBoxInt( dec->params[ 0 ] );

	BotEvaluateNode( self, dec->child );
	return status;
}

static bool EvalConditionExpression( gentity_t *self, AIExpType_t *exp );

static double EvalFunc( gentity_t *self, AIExpType_t *exp )
{
	AIValueFunc_t *v = ( AIValueFunc_t * ) exp;
	AIValue_t vt = v->func( self, v->params );
	double vd = AIUnBoxDouble( vt );
	AIDestroyValue( vt );
	return vd;
}

// using double because it has enough precision to exactly represent both a float and an int
static double EvalValue( gentity_t *self, AIExpType_t *exp )
{
	AIValue_t *v = ( AIValue_t * ) exp;

	if ( *exp == EX_FUNC )
	{
		return EvalFunc( self, exp );
	}

	if ( *exp != EX_VALUE )
	{
		return ( double ) EvalConditionExpression( self, exp );
	}

	return AIUnBoxDouble( *v );
}

static bool EvaluateBinaryOp( gentity_t *self, AIExpType_t *exp )
{
	AIBinaryOp_t *o = ( AIBinaryOp_t * ) exp;

	switch ( o->opType )
	{
		case OP_LESSTHAN:
			return EvalValue( self, o->exp1 ) < EvalValue( self, o->exp2 );
		case OP_LESSTHANEQUAL:
			return EvalValue( self, o->exp1 ) <= EvalValue( self, o->exp2 );
		case OP_GREATERTHAN:
			return EvalValue( self, o->exp1 ) > EvalValue( self, o->exp2 );
		case OP_GREATERTHANEQUAL:
			return EvalValue( self, o->exp1 ) >= EvalValue( self, o->exp2 );
		case OP_EQUAL:
			return EvalValue( self, o->exp1 ) == EvalValue( self, o->exp2 );
		case OP_NEQUAL:
			return EvalValue( self, o->exp1 ) != EvalValue( self, o->exp2 );
		case OP_AND:
			return EvalConditionExpression( self, o->exp1 ) && EvalConditionExpression( self, o->exp2 );
		case OP_OR:
			return EvalConditionExpression( self, o->exp1 ) || EvalConditionExpression( self, o->exp2 );
		default:
			return false;
	}
}

static bool EvaluateUnaryOp( gentity_t *self, AIExpType_t *exp )
{
	AIUnaryOp_t *o = ( AIUnaryOp_t * ) exp;
	return !EvalConditionExpression( self, o->exp );
}

static bool EvalConditionExpression( gentity_t *self, AIExpType_t *exp )
{
	if ( *exp == EX_OP )
	{
		AIOp_t *op = ( AIOp_t * ) exp;

		if ( isBinaryOp( op->opType ) )
		{
			return EvaluateBinaryOp( self, exp );
		}
		else if ( isUnaryOp( op->opType ) )
		{
			return EvaluateUnaryOp( self, exp );
		}
	}
	else if ( *exp  == EX_VALUE )
	{
		return EvalValue( self, exp ) != 0.0;
	}
	else if ( *exp == EX_FUNC )
	{
		return EvalFunc( self, exp ) != 0.0;
	}

	return false;
}

/*
======================
BotConditionNode

Runs the child node if the condition expression is true
If there is no child node, returns success if the conditon expression is true
returns failure otherwise
======================
*/
AINodeStatus_t BotConditionNode( gentity_t *self, AIGenericNode_t *node )
{
	bool success = false;

	AIConditionNode_t *con = ( AIConditionNode_t * ) node;

	success = EvalConditionExpression( self, con->exp );
	if ( success )
	{
		if ( con->child )
		{
			return BotEvaluateNode( self, con->child );
		}
		else
		{
			return STATUS_SUCCESS;
		}
	}

	return STATUS_FAILURE;
}

/*
======================
BotBehaviorNode

Runs the root node of a behavior tree
A behavior tree may contain multiple other behavior trees which are run in this way
======================
*/
AINodeStatus_t BotBehaviorNode( gentity_t *self, AIGenericNode_t *node )
{
	AIBehaviorTree_t *tree = ( AIBehaviorTree_t * ) node;
	return BotEvaluateNode( self, tree->root );
}

/*
======================
BotEvaluateNode

Generic node running routine that properly handles
running information for sequences and selectors
This should always be used instead of the node->run function pointer
======================
*/
AINodeStatus_t BotEvaluateNode( gentity_t *self, AIGenericNode_t *node )
{
	AINodeStatus_t status = node->run( self, node );

	// reset the current node if it finishes
	// we do this so we can re-pathfind on the next entrance
	if ( ( status == STATUS_SUCCESS || status == STATUS_FAILURE ) && self->botMind->currentNode == node )
	{
		self->botMind->currentNode = nullptr;
	}

	// reset running information on node success so sequences and selectors reset their state
	if ( NodeIsRunning( self, node ) && status == STATUS_SUCCESS )
	{
		self->botMind->runningNodes.clear();
	}

	// store running information for sequence nodes and selector nodes
	if ( status == STATUS_RUNNING )
	{
		// clear out previous running list when we hit a running leaf node
		// this insures that only 1 node in a sequence or selector has the running state
		if ( node->type == ACTION_NODE )
		{
			self->botMind->runningNodes.clear();
		}

		if ( !NodeIsRunning( self, node ) )
		{
			if ( !self->botMind->runningNodes.append(node) )
			{
				Log::Warn( "Bot failed to execute action: "
						"MAX_NODE_DEPTH exceeded" );
				return status;
			}
		}
	}

	return status;
}

/*
======================
Action Nodes

Action nodes are always the leaves of the behavior tree
They make the bot do a specific thing while leaving decision making
to the rest of the behavior tree
======================
*/

AINodeStatus_t BotActionFireWeapon( gentity_t *self, AIGenericNode_t* )
{
	if ( WeaponIsEmpty( BG_GetPlayerWeapon( &self->client->ps ), &self->client->ps ) && G_Team( self ) == TEAM_HUMANS )
	{
		G_ForceWeaponChange( self, WP_BLASTER );
	}

	if ( BG_GetPlayerWeapon( &self->client->ps ) == WP_HBUILD )
	{
		G_ForceWeaponChange( self, WP_BLASTER );
	}

	BotFireWeaponAI( self );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionTeleport( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *action = ( AIActionNode_t * ) node;
	self->client->ps.origin[0] = AIUnBoxFloat( action->params[0] );
	self->client->ps.origin[1] = AIUnBoxFloat( action->params[1] );
	self->client->ps.origin[2] = AIUnBoxFloat( action->params[2] );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionActivateUpgrade( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *action = ( AIActionNode_t * ) node;
	upgrade_t u = ( upgrade_t ) AIUnBoxInt( action->params[ 0 ] );

	if ( u == UP_MEDKIT && !self->botMind->botSkillSet[BOT_H_MEDKIT] )
	{
		// we don't know how to use it
		return STATUS_FAILURE;
	}

	if ( !BG_InventoryContainsUpgrade( u, self->client->ps.stats ) )
	{
		return STATUS_FAILURE;
	}

	if ( BG_UpgradeIsActive( u, self->client->ps.stats ) )
	{
		return STATUS_FAILURE;
	}

	BG_ActivateUpgrade( u, self->client->ps.stats );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionDeactivateUpgrade( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *action = ( AIActionNode_t * ) node;
	upgrade_t u = ( upgrade_t ) AIUnBoxInt( action->params[ 0 ] );

	if ( !BG_InventoryContainsUpgrade( u, self->client->ps.stats ) )
	{
		return STATUS_FAILURE;
	}

	if ( !BG_UpgradeIsActive( u, self->client->ps.stats ) )
	{
		return STATUS_FAILURE;
	}

	BG_DeactivateUpgrade( u, self->client->ps.stats );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionAimAtGoal( gentity_t *self, AIGenericNode_t* )
{
	botMemory_t const* mind = self->botMind;
	if ( !self->botMind->goal.isValid() )
	{
		return STATUS_FAILURE;
	}

	if ( self->botMind->goal.targetsValidEntity()
	  && ( G_Team( self->botMind->goal.getTargetedEntity() )
	    != G_Team( self ) ) )
	{
		BotAimAtEnemy( self );
	}
	else
	{
		glm::vec3 pos = mind->goal.getPos();
		BotSlowAim( self, pos, 0.5 );
		BotAimAtLocation( self, pos );
	}

	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionMoveToGoal( gentity_t *self, AIGenericNode_t* )
{
	if ( GoalInRange( self, BotGetGoalRadius( self ) ) )
	{
		return STATUS_SUCCESS;
	}
	return BotMoveToGoal( self ) ? STATUS_RUNNING : STATUS_FAILURE;
}

AINodeStatus_t BotActionMoveInDir( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *a = ( AIActionNode_t * ) node;
	int dir = AIUnBoxInt( a->params[ 0 ] );
	if ( a->nparams == 2 )
	{
		dir |= AIUnBoxInt( a->params[ 1 ] );
	}
	BotMoveInDir( self, dir );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionStrafeDodge( gentity_t *self, AIGenericNode_t* )
{
	BotStrafeDodge( self );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionAlternateStrafe( gentity_t *self, AIGenericNode_t* )
{
	BotAlternateStrafe( self );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionClassDodge( gentity_t *self, AIGenericNode_t* )
{
	BotClassMovement( self, BotTargetInAttackRange( self, self->botMind->goal ) );
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionChangeGoal( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *a = ( AIActionNode_t * ) node;

	if( a->nparams == 1 )
	{
		AIEntity_t et = ( AIEntity_t ) AIUnBoxInt( a->params[ 0 ] );
		botEntityAndDistance_t e = AIEntityToGentity( self, et );
		if ( !BotChangeGoalEntity( self, e.ent ) )
		{
			return STATUS_FAILURE;
		}
	}
	else if( a->nparams == 3 )
	{
		glm::vec3 pos = { AIUnBoxFloat(a->params[0]), AIUnBoxFloat(a->params[1]), AIUnBoxFloat(a->params[2]) };
		if ( !BotChangeGoalPos( self, pos ) )
		{
			return STATUS_FAILURE;
		}
	}
	else
	{
		return STATUS_FAILURE;
	}

	self->botMind->currentNode = node;
	return STATUS_SUCCESS;
}

AINodeStatus_t BotActionEvolveTo( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *action = ( AIActionNode_t * ) node;
	class_t c = ( class_t )  AIUnBoxInt( action->params[ 0 ] );

	if ( BotEvolveToClass( self, c ) )
	{
		return STATUS_SUCCESS;
	}

	return STATUS_FAILURE;
}

AINodeStatus_t BotActionSay( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *action = ( AIActionNode_t * ) node;
	const char *str = AIUnBoxString( action->params[ 0 ] );
	saymode_t   say = ( saymode_t ) AIUnBoxInt( action->params[ 1 ] );
	G_Say( self, say, str );
	return STATUS_SUCCESS;
}

// TODO: Move decision making out of these actions and into the rest of the behavior tree
AINodeStatus_t BotActionFight( gentity_t *self, AIGenericNode_t *node )
{
	team_t myTeam = ( team_t ) self->client->pers.team;
	botMemory_t const* mind = self->botMind;

	if ( self->botMind->currentNode != node )
	{
		if ( !BotEntityIsValidEnemyTarget( self, self->botMind->bestEnemy.ent ) || !BotChangeGoalEntity( self, self->botMind->bestEnemy.ent ) )
		{
			return STATUS_FAILURE;
		}

		self->botMind->currentNode = node;
		self->botMind->enemyLastSeen = level.time;
		return STATUS_RUNNING;
	}

	// we killed it, yay!
	if ( !mind->goal.targetsValidEntity()
		|| !BotEntityIsValidEnemyTarget( self, mind->goal.getTargetedEntity() ) )
	{
		return STATUS_SUCCESS;
	}

	if ( !mind->nav().havePath )
	{
		return STATUS_FAILURE;
	}

	if ( WeaponIsEmpty( BG_GetPlayerWeapon( &self->client->ps ), &self->client->ps ) && myTeam == TEAM_HUMANS )
	{
		G_ForceWeaponChange( self, WP_BLASTER );
	}

	if ( BG_GetPlayerWeapon( &self->client->ps ) == WP_HBUILD )
	{
		G_ForceWeaponChange( self, WP_BLASTER );
	}

	//aliens have radar so they will always 'see' the enemy if they are in radar range
	if ( myTeam == TEAM_ALIENS && DistanceToGoalSquared( self ) <= Square( ALIENSENSE_RANGE ) )
	{
		self->botMind->enemyLastSeen = level.time;
	}

	if ( !BotTargetIsVisible( self, self->botMind->goal, MASK_OPAQUE ) )
	{
		botTarget_t proposedTarget;
		proposedTarget = self->botMind->bestEnemy.ent;

		//we can see another enemy (not our target) so switch to it
		if ( self->botMind->bestEnemy.ent
		  && ( self->botMind->goal.getTargetedEntity()
		    != self->botMind->bestEnemy.ent )
		  && BotPathIsWalkable( self, proposedTarget ) )
		{
			// force the BT to evaluate again and this action to
			// retarget
			return STATUS_SUCCESS;
		}
		else if ( level.time - self->botMind->enemyLastSeen >= g_bot_chasetime.Get() )
		{
			return STATUS_SUCCESS;
		}
		else
		{
			BotMoveToGoal( self );
			return STATUS_RUNNING;
		}
	}

	// We have a valid visible target

	bool inAttackRange = BotTargetInAttackRange( self, self->botMind->goal );
	self->botMind->enemyLastSeen = level.time;

	if ( !( inAttackRange && myTeam == TEAM_HUMANS ) && !mind->nav().directPathToGoal )
	{
		BotMoveToGoal( self );
		return STATUS_RUNNING;
	}

	// We have a visible target for which we haven't got a
	// direct navmesh path and we are not at at weapon range (if human)

	BotAimAtEnemy( self );
	BotMoveInDir( self, MOVE_FORWARD );

	if ( inAttackRange || self->client->ps.weapon == WP_PAIN_SAW )
	{
		BotFireWeaponAI( self );
	}

	if ( myTeam == TEAM_ALIENS )
	{
		BotClassMovement( self, inAttackRange );
		return STATUS_RUNNING;
	}

	// We are human and we either are at fire range, or have
	// a direct path to goal

	if ( self->botMind->botSkill.level >= 3 && DistanceToGoalSquared( self ) < Square( MAX_HUMAN_DANCE_DIST )
	        && ( DistanceToGoalSquared( self ) > Square( MIN_HUMAN_DANCE_DIST ) || self->botMind->botSkill.level < 5 )
	        && self->client->ps.weapon != WP_PAIN_SAW && self->client->ps.weapon != WP_FLAMER )
	{
		BotMoveInDir( self, MOVE_BACKWARD );
	}
	else if ( DistanceToGoalSquared( self ) <= Square( MIN_HUMAN_DANCE_DIST ) ) //we wont hit this if skill < 5
	{
		// We will be moving toward enemy, strafing to
		// the result: we go around the enemy
		BotAlternateStrafe( self );
	}
	else if ( DistanceToGoalSquared( self ) >= Square( MAX_HUMAN_DANCE_DIST ) && self->client->ps.weapon != WP_PAIN_SAW )
	{
		if ( DistanceToGoalSquared( self ) - Square( MAX_HUMAN_DANCE_DIST ) < 100 )
		{
			BotStandStill( self );
		}
		else
		{
			BotStrafeDodge( self );
		}
	}

	if ( inAttackRange && self->botMind->goal.getTargetType() == entityType_t::ET_BUILDABLE )
	{
		BotStandStill( self );
	}

	BotSprint( self, true );

	return STATUS_RUNNING;
}

AINodeStatus_t BotActionFlee( gentity_t *self, AIGenericNode_t *node )
{
	if ( node != self->botMind->currentNode )
	{
		if ( !BotChangeGoal( self, BotGetRetreatTarget( self ) ) )
		{
			return STATUS_FAILURE;
		}
		self->botMind->currentNode = node;
	}

	if ( !self->botMind->goal.isValid() )
	{
		return STATUS_FAILURE;
	}

	if ( GoalInRange( self, 70 ) )
	{
		return STATUS_SUCCESS;
	}
	else
	{
		BotMoveToGoal( self );
	}

	return STATUS_RUNNING;
}

AINodeStatus_t BotActionRoamInRadius( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *a = ( AIActionNode_t * ) node;
	AIEntity_t e = ( AIEntity_t ) AIUnBoxInt( a->params[ 0 ] );
	float radius = AIUnBoxFloat( a->params[ 1 ] );

	if ( node != self->botMind->currentNode )
	{
		glm::vec3 point;
		botEntityAndDistance_t ent = AIEntityToGentity( self, e );

		if ( !ent.ent )
		{
			return STATUS_FAILURE;
		}

		if ( !BotFindRandomPointInRadius( self->s.number, VEC2GLM( ent.ent->s.origin ), point, radius ) )
		{
			return STATUS_FAILURE;
		}

		if ( !BotChangeGoalPos( self, point ) )
		{
			return STATUS_FAILURE;
		}
		self->botMind->currentNode = node;
	}

	if ( GoalInRange( self, BotGetGoalRadius( self ) ) )
	{
		return STATUS_SUCCESS;
	}
	return BotMoveToGoal( self ) ? STATUS_RUNNING : STATUS_FAILURE;
}

AINodeStatus_t BotActionRoam( gentity_t *self, AIGenericNode_t *node )
{
	// we are just starting to roam, get a target location
	if ( node != self->botMind->currentNode )
	{
		botTarget_t target = BotGetRoamTarget( self );
		if ( !BotChangeGoal( self, target ) )
		{
			return STATUS_FAILURE;
		}
		self->botMind->currentNode = node;
	}

	if ( GoalInRange( self, BotGetGoalRadius( self ) ) )
	{
		return STATUS_SUCCESS;
	}
	return BotMoveToGoal( self ) ? STATUS_RUNNING : STATUS_FAILURE;
}

static botTarget_t BotGetMoveToTarget( gentity_t *self, AIEntity_t e )
{
	botTarget_t target;
	botEntityAndDistance_t en = AIEntityToGentity( self, e );
	target = en.ent;
	return target;
}

AINodeStatus_t BotActionMoveTo( gentity_t *self, AIGenericNode_t *node )
{
	float radius = 0;
	AIActionNode_t *moveTo = ( AIActionNode_t * ) node;
	AIEntity_t ent = ( AIEntity_t ) AIUnBoxInt( moveTo->params[ 0 ] );

	if ( moveTo->nparams > 1 )
	{
		radius = std::max( AIUnBoxFloat( moveTo->params[ 1 ] ), 0.0f );
	}

	if ( node != self->botMind->currentNode )
	{
		if ( !BotChangeGoal( self, BotGetMoveToTarget( self, ent ) ) )
		{
			return STATUS_FAILURE;
		}
		else
		{
			self->botMind->currentNode = node;
			return STATUS_RUNNING;
		}
	}

	if ( !self->botMind->goal.isValid() )
	{
		return STATUS_FAILURE;
	}

	if ( radius == 0 )
	{
		radius = BotGetGoalRadius( self );
	}

	if ( GoalInRange( self, radius ) )
	{
		return STATUS_SUCCESS;
	}
	return BotMoveToGoal( self ) ? STATUS_RUNNING : STATUS_FAILURE;
}

AINodeStatus_t BotActionRush( gentity_t *self, AIGenericNode_t *node )
{
	if ( self->botMind->currentNode != node )
	{
		if ( !BotChangeGoal( self, BotGetRushTarget( self ) ) )
		{
			return STATUS_FAILURE;
		}
		else
		{
			self->botMind->currentNode = node;
			return STATUS_RUNNING;
		}
	}

	if ( !self->botMind->goal.isValid() )
	{
		return STATUS_FAILURE;
	}

	if ( GoalInRange( self, BotGetGoalRadius( self ) ) )
	{
		return STATUS_SUCCESS;
	}
	return BotMoveToGoal( self ) ? STATUS_RUNNING : STATUS_FAILURE;
}

static AINodeStatus_t BotActionReachHealA( gentity_t *self );
static AINodeStatus_t BotActionReachHealH( gentity_t *self );
AINodeStatus_t BotActionHeal( gentity_t *self, AIGenericNode_t *node )
{
	bool needsMedikit = G_Team(self) == TEAM_HUMANS
			     && !BG_InventoryContainsUpgrade( UP_MEDKIT, self->client->ps.stats );
	bool fullyHealed = Entities::HasFullHealth(self) && !needsMedikit;

	if ( self->botMind->currentNode != node )
	{
		if ( fullyHealed )
		{
			return STATUS_FAILURE;
		}

		if ( !BotChangeGoalEntity( self, BotGetHealTarget( self ).ent ) )
		{
			return STATUS_FAILURE;
		}
		self->botMind->currentNode = node;
	}

	if ( fullyHealed )
	{
		return STATUS_SUCCESS;
	}

	if ( !self->botMind->goal.targetsValidEntity() )
	{
		return STATUS_FAILURE;
	}

	// Can't heal at powered off buildables
	if ( !self->botMind->goal.getTargetedEntity()->powered )
	{
		return STATUS_FAILURE;
	}

	if ( G_Team( self ) == TEAM_HUMANS )
	{
		return BotActionReachHealH( self );
	}
	else
	{
		return BotActionReachHealA( self );
	}
}

AINodeStatus_t BotActionSuicide( gentity_t *self, AIGenericNode_t* )
{
	Entities::Kill( self, MOD_SUICIDE );
	return AINodeStatus_t::STATUS_SUCCESS;
}

AINodeStatus_t BotActionJump( gentity_t *self, AIGenericNode_t* )
{
	return BotJump( self ) ? AINodeStatus_t::STATUS_SUCCESS : AINodeStatus_t::STATUS_FAILURE;
}

AINodeStatus_t BotActionResetStuckTime( gentity_t *self, AIGenericNode_t* )
{
	BotResetStuckTime( self );
	return AINodeStatus_t::STATUS_SUCCESS;
}

AINodeStatus_t BotActionGesture( gentity_t *self, AIGenericNode_t* )
{
	usercmd_t *botCmdBuffer = &self->botMind->cmdBuffer;
	usercmdPressButton( botCmdBuffer->buttons, BTN_GESTURE );
	return AINodeStatus_t::STATUS_SUCCESS;
}

/*
	alien specific actions
*/
static AINodeStatus_t BotActionReachHealA( gentity_t *self )
{
	if ( G_Team( self ) != TEAM_ALIENS )
	{
		return STATUS_FAILURE;
	}

	// retrieve creep size to have proper distance
	buildable_t targetType = static_cast<buildable_t>( self->botMind->goal.getTargetedEntity()->s.modelindex );
	float dist = -1.f;
	if ( targetType != BA_A_BOOSTER )
	{
		dist += BG_Buildable( targetType )->creepSize;
	}
	else
	{
		dist += REGEN_BOOSTER_RANGE;
	}

	if ( !GoalInRange( self, dist ) )
	{
		BotMoveToGoal( self );
	}
	return STATUS_RUNNING;
}

/*
	human specific actions
*/
static AINodeStatus_t BotActionReachHealH( gentity_t *self )
{
	if ( G_Team( self ) != TEAM_HUMANS )
	{
		return STATUS_FAILURE;
	}

	auto const& goal = self->botMind->goal;
	const gentity_t * medistation = goal.getTargetedEntity();

	glm::vec3 targetPos = goal.getPos();
	glm::vec3 myPos = VEC2GLM( self->s.origin );
	targetPos[2] += BG_BuildableModelConfig( BA_H_MEDISTAT )->maxs[2];
	myPos[2] += self->r.mins[2]; //mins is negative

	float dist2 = glm::distance2( myPos, targetPos );
	// If medistation is busy, do something else until can go on it anew.
	// See https://github.com/Unvanquished/Unvanquished/pull/1598
	// (It would be nice to allow the BT to check for the failure cause.
	//  How? That's a good question)
	if ( medistation->target && medistation->target.get() != self
	     && dist2 > Square( 200 ) )
	{
		return STATUS_FAILURE;
	}

	//keep moving to the medi until we are on top of it
	if ( dist2 > Square( BG_BuildableModelConfig( BA_H_MEDISTAT )->mins[1] ) )
	{
		BotMoveToGoal( self );
	}
	return STATUS_RUNNING;
}
AINodeStatus_t BotActionRepair( gentity_t *self, AIGenericNode_t *node )
{
	botMemory_t const* mind = self->botMind;

	if ( node != self->botMind->currentNode )
	{
		if ( !BotChangeGoalEntity( self, self->botMind->closestDamagedBuilding.ent ) )
		{
			return STATUS_FAILURE;
		}
		self->botMind->currentNode = node;
	}

	if ( !self->botMind->goal.targetsValidEntity() )
	{
		return STATUS_FAILURE;
	}

	if ( Entities::HasFullHealth( self->botMind->goal.getTargetedEntity() ) )
	{
		return STATUS_SUCCESS;
	}

	if ( BG_GetPlayerWeapon( &self->client->ps ) != WP_HBUILD )
	{
		G_ForceWeaponChange( self, WP_HBUILD );
	}

	glm::vec3 forward;
	AngleVectors( VEC2GLM( self->client->ps.viewangles ), &forward, nullptr, nullptr );

	//move to the damaged building until we are in range
	if ( !BotTargetIsVisible( self, self->botMind->goal, MASK_SHOT ) || DistanceToGoalSquared( self ) > Square( 100 ) )
	{
		BotMoveToGoal( self );
		return STATUS_RUNNING;
	}

	//aim at the buildable
	glm::vec3 targetPos = mind->goal.getPos();
	BotSlowAim( self, targetPos, 0.5 );
	BotAimAtLocation( self, targetPos );
	// we automatically heal a building if close enough and aiming at it
	return STATUS_RUNNING;
}
AINodeStatus_t BotActionBuy( gentity_t *self, AIGenericNode_t *node )
{
	AIActionNode_t *buy = ( AIActionNode_t * ) node;
	weapon_t  weapon;
	const size_t ARRAY_LENGTH = 4;
	upgrade_t upgrades[ARRAY_LENGTH];
	int numUpgrades;
	int i;

	if ( buy->nparams == 0 )
	{
		// equip action
		numUpgrades = BotGetDesiredBuy( self, weapon, upgrades, ARRAY_LENGTH);
	}
	else
	{
		// first parameter should always be a weapon
		weapon = ( weapon_t ) AIUnBoxInt( buy->params[ 0 ] );

		if ( weapon < WP_NONE || weapon >= WP_NUM_WEAPONS )
		{
			Log::Warn("parameter 1 to action buy out of range" );
			weapon = WP_NONE;
		}

		numUpgrades = 0;

		// other parameters are always upgrades
		for ( i = 1; i < buy->nparams; i++ )
		{
			upgrades[ numUpgrades ] = ( upgrade_t ) AIUnBoxInt( buy->params[ i ] );

			if ( upgrades[ numUpgrades ] <= UP_NONE || upgrades[ numUpgrades ] >= UP_NUM_UPGRADES )
			{
				Log::Warn("parameter %d to action buy out of range", i + 1 );
				continue;
			}

			numUpgrades++;
		}
	}

	if ( !g_bot_buy.Get() )
	{
		return STATUS_FAILURE;
	}

	if ( G_Team( self ) != TEAM_HUMANS )
	{
		return STATUS_FAILURE;
	}

	//check if we already have everything
	if ( BG_InventoryContainsWeapon( weapon, self->client->ps.stats ) || weapon == WP_NONE )
	{
		int numContain = 0;

		for ( i = 0; i < numUpgrades; i++ )
		{
			if ( BG_InventoryContainsUpgrade( upgrades[i], self->client->ps.stats ) )
			{
				numContain++;
			}
		}

		//we have every upgrade we want to buy
		if ( numContain == numUpgrades )
		{
			return STATUS_FAILURE;
		}
	}

	if ( self->botMind->currentNode != node )
	{
		if ( !BotChangeGoalEntity( self, self->botMind->closestBuildings[ BA_H_ARMOURY ].ent ) )
		{
			return STATUS_FAILURE;
		}
		self->botMind->currentNode = node;
	}

	if ( !self->botMind->goal.targetsValidEntity() )
	{
		return STATUS_FAILURE;
	}

	if ( !self->botMind->goal.getTargetedEntity()->powered )
	{
		return STATUS_FAILURE;
	}

	if ( GoalInRange( self, ENTITY_USE_RANGE ) )
	{
		if ( numUpgrades )
		{
			BotSellUpgrades( self );
			for ( i = 0; i < numUpgrades; i++ )
			{
				if ( !BotBuyUpgrade( self, upgrades[i] ) )
				{
					return STATUS_FAILURE;
				}
			}
		}

		if ( weapon != WP_NONE )
		{
			BotSellWeapons( self );
			if ( !BotBuyWeapon( self, weapon ) )
			{
				return STATUS_FAILURE;
			}
		}

		// make sure that we're not using the blaster
		if ( weapon != WP_NONE )
		{
			G_ForceWeaponChange( self, weapon );
		}

		return STATUS_SUCCESS;
	}

	BotMoveToGoal( self );
	return STATUS_RUNNING;
}
