#ifndef _ABILITY_JUMPSTART_H_INCLUDED_
#define _ABILITY_JUMPSTART_H_INCLUDED_

#include "../AbilityBase.h"
#include "../ServerManager.h"

class AJumpStart : public AbilityBase
{
public:
	AJumpStart(Player * me) {
		m_Player = me;
		m_LastUse = 0;
		m_InUse = false;
		m_TargetType = OT_PLAYER;
	};

public:
	bool Use(int TargetID);						// When a player trys to use an ability
	void Confirmation(bool Confirm);			// Used when a conformation is needed
	void Execute();								// Used when ability can be used on other players
	bool CanUse(int TargetID, long AbilityID, long SkillID);	// Used to see if player can use this ability
	void Update();								// Updated by a timer

	bool SkillInterruptable(bool* OnMotion, bool* OnDamage, bool* OnAction);	//Can this skill be interrupted by anything?
	bool InterruptSkillOnDamage(float Damage);		//Returns if this skill was interrupted based on damage taken
	bool InterruptSkillOnMotion(float Speed);		//Returns if this skill was interrupted based on current motion.
	bool InterruptSkillOnAction(int Type);

	// Calculate skill level Data
private:
	float CalculateEnergy ( float SkillLevel, long SkillRank );
	float CalculateChargeUpTime ( float SkillLevel, long SkillRank );
	float CalculateCoolDownTime ( float SkillLevel, long SkillRank );
	float CalculateRange ( float SkillLevel, long SkillRank );
	float CalculateAOE ( float SkillLevel, long SkillRank );
	float CalculateHullRepair ( float SkillLevel );
	long DetermineSkillRank(int SkillID);

	int m_TargetType;
	int m_TargetID;
};



#endif
