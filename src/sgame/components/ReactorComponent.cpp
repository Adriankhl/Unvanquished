#include "ReactorComponent.h"
#include "../Entities.h"

const float ReactorComponent::ATTACK_RANGE  = 200.0f;
const float ReactorComponent::ATTACK_DAMAGE = 25.0f;

ReactorComponent::ReactorComponent(Entity& entity, HumanBuildableComponent& r_HumanBuildableComponent,
                                   MainBuildableComponent& r_MainBuildableComponent)
	: ReactorComponentBase(entity, r_HumanBuildableComponent, r_MainBuildableComponent)
{
	GetMainBuildableComponent().GetBuildableComponent().REGISTER_THINKER(
		Think, ThinkingComponent::SCHEDULER_AVERAGE, 1000
	);
}

void ReactorComponent::Think(int timeDelta) {
	if (!GetMainBuildableComponent().GetBuildableComponent().Active()) return;

	float baseDamage = ATTACK_DAMAGE * ((float)timeDelta / 1000.0f);

	// Zap close enemies.
	for (Entity& other : Entities::Having<AlienClassComponent>()) {
		// Respect the no-target flag.
		if (other.oldEnt->flags & FL_NOTARGET) continue;

		// Don't zap through walls
		if ( !G_LineOfSight( entity.oldEnt, other.oldEnt, MASK_SOLID, false ) ) continue;

		// TODO: Add LocationComponent and Utility::BBOXDistance.
		float distance = G_Distance(entity.oldEnt, other.oldEnt);

		if (distance >= ATTACK_RANGE) continue;

		float damage = baseDamage * (1.0f - (0.7f * distance / ATTACK_RANGE));

		CreateTeslaTrail(other);

		other.Damage(damage, entity.oldEnt, {}, {}, 0, MOD_REACTOR);
	}
}

void ReactorComponent::CreateTeslaTrail(Entity& target) {
	gentity_t* trail = G_NewTempEntity( VEC2GLM( entity.oldEnt->s.origin ), EV_TESLATRAIL);
	trail->s.otherEntityNum = entity.oldEnt->num(); // Source.
	trail->s.clientNum = target.oldEnt->num(); // Destination.
}
