// MOBClass.cpp

#include "Net7.h"
#include "ObjectClass.h"
#include "MOBClass.h"
#include "PlayerClass.h"
#include "ObjectManager.h"
#include "Opcodes.h"
#include "ServerManager.h"
#include "MOBDatabase.h"
#include "PlayerManager.h"

#define SHIELD 1
#define HULL 2

//MOB DPS average for each level step, 1.1f = level 0, 14933 = level 200 (CL 66).
float MobDPS[] =
{
	1.1f,
	3.2f,
	7.2f,
	14.7f,
	24.0f,
	41.0f,
	74.0f,
	131.0f,
	198.0f,
	310.0f,
	448.0f,
	648.0f,
	915.0f,
	1436.0f,
	2080.0f,
	2912.0f,
	3965.0f,
	5017.0f,
	6347.0f,
	8028.0f,
	14933.0f
};

//Damage Absorbtion Capacity - average for MOBs
float MobDAC[] =
{
	29,
	57,
	160,
	377,
	864,
	1365,
	2845,
	5203,
	10146,
	14171,
	27030,
	36864,
	62797,
	87172,
	164296,
	220191,
	350057,
	466263,
	619402,
	820852,
	1085421
};

MOB::MOB(long object_id) : Object (object_id)
{
	m_Type = OT_MOB;
	m_CreateInfo.Type = 0;
	m_MOBVisibleList.clear();
	m_BehaviourList.clear();
	m_Respawn_tick = 0;
	m_MovementID = 0;
	m_Velocity = 0;
	m_LastUpdate = GetNet7TickCount();
	m_YInput = 0;
	m_ZInput = 0;
	m_LastAttackTime = 0;
	m_Position_info.Orientation[0] = 0.0f;
	m_Position_info.Orientation[1] = 0.0f;
	m_Position_info.Orientation[2] = 0.0f;
	m_Position_info.Orientation[3] = 1.0f;
	m_ObjectRadius = 250.0f; //default ship size
	m_Signature = 3000.0f;
	m_ScanSkill = 0;

	m_Attacking = false;
	m_MOB_Data = (0);

	//TODO: set this up properly somewhere
	m_DamageType = 1;

	// Set up gameID
	m_ShipIndex.SetGameID(object_id);

	m_ArrivalTime = 0;
	m_Behaviour = CURIOUS;
	m_Destination = (0);
	m_ShieldFXSent = 0;

	memset (&m_HuntPosition, 0, sizeof(m_HuntPosition));
	memset (&m_DestinationPos, 0, sizeof(m_DestinationPos));

	memset (&m_RangeList, 0, sizeof(m_RangeList));
	memset (&m_PlayerVisibleList, 0, sizeof(m_PlayerVisibleList));
	memset (&m_ClickedList, 0, sizeof(m_ClickedList));
	memset (&m_HateList, 0, sizeof(m_HateList));

	m_DamageTime = 0;
}

MOB::~MOB()
{
	// TODO: destroy everything
}

void MOB::SetMOBType(short type)
{
	MOBData *mob_data = g_ServerMgr->MOBList()->GetMOBData(type);
	m_MOB_Data = mob_data;

	if (!mob_data)
	{
		return;
	}

	// Set mob info
	SetName(mob_data->m_Name);
	SetBasset(mob_data->m_Basset);
	SetLevel(type);
	SetHSV(mob_data->m_HSV[0], mob_data->m_HSV[1], mob_data->m_HSV[2]);
	SetScale(mob_data->m_Scale);
	//LogMessage("Adding MOB: %s\n", Name());
	m_MOB_Type = type;
	// Set aux data
	m_ShipIndex.SetName(m_Name);
	m_ShipIndex.SetCombatLevel(mob_data->m_Level);
}

void MOB::SetLevel(short type)
{
	if (!m_MOB_Data)
	{
		return;
	}

	short overall_level = m_MOB_Data->m_Level * 3;
	short index = (overall_level / 10);
	float fraction = (float)(overall_level) / 10.0f - (float) index;

	//TODO: calculate this from MOB items when in place
	float lower_bound = MobDPS[index];
	float upper_bound = MobDPS[index+1];
	m_WeaponDPS = lower_bound + (upper_bound - lower_bound)*fraction;

	//work out MOB Damage absorbtion Capacity from table
	lower_bound = MobDAC[index];
	upper_bound = MobDAC[index+1];

	m_StartShieldLevel  = lower_bound + (upper_bound - lower_bound)*fraction;
	if (m_MOB_Data->m_Type == STRUCTURAL || m_MOB_Data->m_Type == CYBERNETIC)
	{
		//split the DAC between hull and shield
		m_StartHullLevel = m_StartShieldLevel * 0.3f;
		m_StartShieldLevel -= m_StartHullLevel * 0.9f;
	}
	else
	{
		m_StartHullLevel    = 0;
	}

	m_ShieldLevel   = m_StartShieldLevel;
	m_HullLevel     = m_StartHullLevel;
	m_Level         = m_MOB_Data->m_Level;

	// Set up hull points
	m_ShipIndex.SetMaxHullPoints(m_HullLevel);
	m_ShipIndex.SetHullPoints(m_HullLevel);
	//Setup Shields
	m_ShipIndex.Shield.SetStartValue(m_StartShieldLevel);
	m_ShipIndex.Shield.SetChangePerTick(0);
	m_ShipIndex.Shield.SetEndTime(GetNet7TickCount());
	m_ShipIndex.SetMaxShield(m_ShieldLevel);

	m_ScanRange = 2500.0f + (150.0f*m_MOB_Data->m_Level); //TODO: Add this entry into MOB database, and get it from there.
}

///////////////////////////////////////////////////
// Range List Handling
//
// These methods handle adding to and removing from
// the player range lists.

// m_RangeList functions in exactly the same way as the Player list, it's a list of players who can see this MOB.
//             Used only for updating the MOB status to players
//
// m_MOBVisibleList on the other hand is a list of players and MOBs that this MOB can see
//             Used for deciding which target to attack, and target interactions.

void MOB::UpdateObjectVisibilityList()
{
	Player *p = (0);
	u32 * sector_list = m_ObjectMgr->GetSectorList();
	ObjectList * mob_list = m_ObjectMgr->GetMOBList();
	bool in_range;
	bool in_mob_range;
	float range;

	if (RespawnTick() != 0)
	{
		return;
	}

	//TODO:
	//      - Add grid system to further reduce checking
	//		- Change object list to use a MOB list bitfield

	while (g_PlayerMgr->GetNextPlayerOnList(p, sector_list))
	{
		if (p && p->Active())
		{
			//get range and find out if this player is already on the list
			range    = RangeFrom(p->Position());
			in_range = g_PlayerMgr->GetIndex(p->GameID(), m_RangeList);
			in_mob_range = g_PlayerMgr->GetIndex(p->GameID(), m_PlayerVisibleList);

			if (in_range)
			{
				//see if this MOB has gone out of player 'p' scan range
				if (range > (float)(p->ShipIndex()->CurrentStats.GetScanRange() + m_Signature + 200.0f))
				{
					g_PlayerMgr->UnSetIndex(p->GameID(), m_RangeList);
					p->RemoveObject(GameID());
				}
			}
			else
			{
				//see if this MOB has come into player 'p' scan range
				if (range < (float)(p->ShipIndex()->CurrentStats.GetScanRange() + m_Signature) && p->InSpace())
				{
					g_PlayerMgr->SetIndex(p->GameID(), m_RangeList);
					g_PlayerMgr->UnSetIndex(p->GameID(), m_ClickedList);
					SendMOBData(p); //send this MOB to 'p'
				}
			}

			if (in_mob_range)
			{
				//see if 'p' has gone out of MOB scan range
				if (range > (float)(m_ScanRange + p->ShipIndex()->CurrentStats.GetVisibility() + 200.0f))
				{
					g_PlayerMgr->UnSetIndex(p->GameID(), m_PlayerVisibleList);
					if (m_HostilityTargetID == p->GameID())
					{
						LostTarget(p);
						//LogMessage("%s no longer hostile to %s\n", Name(), p->Name());
					}
				}
				else if (range < 1000.0f) //you might be too close to a mob
				{
					//see if MOB fires a warning shot off
					CheckWarningShots(p);
				}
			}
			else
			{
				//see if 'p' has come into MOB scan range
				if (range < (float)(m_ScanRange + p->ShipIndex()->CurrentStats.GetVisibility()) && p->InSpace())
				{
					g_PlayerMgr->SetIndex(p->GameID(), m_PlayerVisibleList);
				}
			}
		}
	}

	//now scan other MOBs in sector, to see if any have come into our range
	//skip this scan for now - there's no MOB interaction
#if 0
	for (itrOList = mob_list->begin(); itrOList < mob_list->end(); ++itrOList)
	{
		if ( (*itrOList) != this)
		{
			o = (*itrOList);

			range    = o->RangeFrom(Position());
			in_mob_range = ObjectInRangeList(&m_MOBVisibleList, o);

			if (in_mob_range)
			{
				//see if MOB 'o' has gone out of this MOB scan range
				if (range > (float)(1500.0f + 3000.0f + 200.0f) )
				{
					RemoveObjectFromRangeList(&m_MOBVisibleList, o);
				}
			}
			else
			{
				//see if MOB 'o' has come into this MOB scan range
				if (range < (float)(1500.0f + 3000.0f) )
				{
					AddObjectToRangeList(&m_MOBVisibleList, o);
				}
			}
		}
	}
#endif
}

void MOB::CheckWarningShots(Player *p)
{
	if (m_MOB_Data->m_Agressiveness > 0 && m_MOB_Data->m_Agressiveness < 5)
	{
		long hate = 0;
		long roll = rand()%100;
		switch (m_MOB_Data->m_Agressiveness)
		{
		case 1:
			//this MOB not likely to fire warning shot, if it does then not too many, just a 'back off'.
			if (roll > 96) hate = 1;
			break;

		case 2:
			if (roll > 95) hate = 5;
			break;

		case 3:
			if (roll > 93) hate = 20;
			break;

		case 4:
			if (roll > 90) hate = 100;
			break;

		default:
			break;
		}

		if (hate != 0)
		{
			AddHate(p->GameID(), hate);
		}
	}
}

void MOB::UpdateMOB(u32 current_tick, bool handle_attacks)
{
	if (RespawnTick() > 0 && RespawnTick() < current_tick)
	{
		LogMessage("Re-Spawning %s\n", Name());
		SetRespawnTick(0); //re-spawn mob
		SetActive(true);
		SetLastUpdateNow();
		OnRespawn();
	}
	else if (RespawnTick() == 0)
	{
		CalcNewPosition(current_tick);
		UpdateObjectVisibilityList();
		UpdateObject();
		IncrementMovementID(5);
		if (MovementID() % UpdateRate() == 0)
		{
			SendLocationAndSpeed(false);
		}
		if (handle_attacks)
		{
			if (HostilityTarget() == 0)
			{
				ChooseTarget();
			}
			else
			{
				HandleAttack();
			}
		}
	}
}

bool MOB::ObjectInRangeList(ObjectList *object_list, Object *obj)
{
	bool in_range = false;
	ObjectList::iterator itrOList;

	m_Mutex.Lock();

	for (itrOList = object_list->begin(); itrOList < object_list->end(); ++itrOList)
	{
		if (obj == (*itrOList))
		{
			in_range = true;
			break;
		}
	}

	m_Mutex.Unlock();

	return in_range;
}

void MOB::RemoveObjectFromRangeList(ObjectList *object_list, Object *obj)
{
	ObjectList::iterator itrOList;

	m_Mutex.Lock();

	for (itrOList = object_list->begin(); itrOList < object_list->end(); ++itrOList)
	{
		if (obj == (*itrOList))
		{
			object_list->erase(itrOList);
			//LogMessage("%s no longer sees %s\n", Name(), obj->Name());
			break;
		}
	}

	m_Mutex.Unlock();
}

void MOB::AddObjectToRangeList(ObjectList *object_list, Object *obj)
{
	m_Mutex.Lock();
	object_list->push_back(obj);
	//LogMessage("%s now sees %s\n", Name(), obj->Name());
	m_Mutex.Unlock();
}

void MOB::SetClickedBy(Player *p)
{
	g_PlayerMgr->SetIndex(p->GameID(), m_ClickedList);
}

void MOB::RemovePlayerFromRangeLists(Player *p)
{
	//RemovePlayerFromRangeList(p);
	//RemoveObjectFromRangeList(&m_MOBVisibleList, p);
	g_PlayerMgr->UnSetIndex(p->GameID(), m_RangeList);
	g_PlayerMgr->UnSetIndex(p->GameID(), m_PlayerVisibleList);

	RemoveHate(p->GameID());
}

void MOB::AddHate(int GameID, int Hate)
{
	// Add to hate list if required
	m_Mutex.Lock();

	long lowest_hate = 0;
	long lowest_hate_val = m_HateList[0].hate;
	bool inserted = false;

	//find spot on hate list
	for (int i = 0; i < HATE_SIZE; i++)
	{
		if (m_HateList[i].hate < lowest_hate_val) //track lowest hate value
		{
			lowest_hate = i;
			lowest_hate_val = m_HateList[i].hate;
		}

		if (m_HateList[i].GameID == GameID || m_HateList[i].hate == 0)
		{
			m_HateList[i].hate += Hate;
			m_HateList[i].GameID = GameID;
			inserted = true;
			break;
		}
	}

	if (!inserted && Hate > lowest_hate_val) //no empty slots, but player qualifies for shit list
	{
		m_HateList[lowest_hate].GameID = GameID;
		m_HateList[lowest_hate].hate = Hate;
	}

	m_Mutex.Unlock();
}

void MOB::SubtractHate(int GameID, int Hate)
{
	for (int i = 0; i < HATE_SIZE; i++)
	{
		if (m_HateList[i].GameID == GameID)
		{
			m_HateList[i].hate -= Hate;
			break;
		}
	}
}

void MOB::RemoveHate(int GameID)
{
	for (int i = 0; i < HATE_SIZE; i++)
	{
		if (m_HateList[i].GameID == GameID)
		{
			m_HateList[i].GameID = 0;
			m_HateList[i].hate = 0;
			break;
		}
	}
}

long MOB::GetMaxHateID()
{
	long max_hate = 0;
	long game_id = 0;

	m_Mutex.Lock();

	for (int i = 0; i < HATE_SIZE; i++)
	{
		if (m_HateList[i].hate > max_hate)
		{
			max_hate = m_HateList[i].hate;
			game_id = m_HateList[i].GameID;
		}
	}

	m_Mutex.Unlock();

	return game_id;
}

float MOB::GetStealthDetectionLevel()
{
	return (m_ScanSkill * 5.0f) + m_MOB_Data->m_Level * 3;
}

//TODO: expand so MOBs can target other MOBs
void MOB::ChooseTarget()
{
	Player *p = (0);
	short player_count = 0;

	while (g_PlayerMgr->GetNextPlayerOnList(p, m_PlayerVisibleList))
	{
		//do not allow players who are cloaked at a lvl insufficient for the mob's targeting to be added to the target list
		if(p->ShipIndex()->GetIsCloaked() && GetStealthDetectionLevel() < p->GetStealthLevel())
		{
			continue;
		}

		if (!p->ShipIndex()->GetIsIncapacitated() && !p->WarpDrive() && !p->Hijackee())
		{
			player_count++;
			break;
		}
	}

	if (player_count > 0)
	{
		LockTarget();
	}
}

void MOB::LockTarget()
{
	// Find target based on hate
	int AttackGameID = 0;
	int MaxHate = -1;

	//see if we hate someone the most
	AttackGameID = GetMaxHateID();

	Player *p = g_GlobMemMgr->GetPlayer(AttackGameID);

	// if the MOB doesn't hate anyone, but can see people, it might want to lock on anyway! //TODO: this should be influenced somehow by factions too maybe?
	if (!p)
	{
		if (m_MOB_Data->m_Agressiveness > 4) // is this an attack-on-sight MOB?
		{
			g_PlayerMgr->GetNextPlayerOnList(p, m_PlayerVisibleList);
			AttackGameID = p->GameID();
		}
	}

	if (p && p->ShipIndex()->GetIsIncapacitated())
	{
		RemoveHate(p->GameID());
		AttackGameID = 0;
	}

	//do not lock players that have cloaked
	if (p && p->ShipIndex()->GetIsCloaked() && GetStealthDetectionLevel() < p->GetStealthLevel())
	{
		RemoveHate(p->GameID());
		AttackGameID = 0;
	}

	if (HostilityTarget() != 0 && HostilityTarget() != AttackGameID)
	{
		//ok we switched targets, so we stopped locking onto the last target.
		Player *p2 = g_GlobMemMgr->GetPlayer(HostilityTarget());
		if (p2)
		{
			SendRelationship(p2);
			LostTarget(p2);
		}
	}

	if (AttackGameID != 0 && p)
	{
		if (HostilityTarget() != AttackGameID)
		{
			SendAggroRelationship(p);
		}
		SetHostilityTarget(AttackGameID);
		m_Mutex.Lock();
		m_Behaviour = PURSUE;
		SetUpdateRate(10);
		m_ArrivalTime = 0;
		m_Mutex.Unlock();
	}
}


#define MOB_WEAPON_READY_TIME 4

void MOB::HandleAttack()
{
	unsigned long current_tick = GetNet7TickCount();
	Player *p = g_GlobMemMgr->GetPlayer(m_HostilityTargetID);

	if (!m_MOB_Data)
	{
		return;
	}

	if(p && p->ShipIndex()->GetIsCloaked() && GetStealthDetectionLevel() < p->GetStealthLevel())
	{
		m_HostilityTargetID = 0;
		LostTarget(p);
		return;
	}

	if (p && !p->ShipIndex()->GetIsIncapacitated() && p->InSpace() && !p->Hijackee())
	{
		if ( current_tick > (m_LastAttackTime + (MOB_WEAPON_READY_TIME * 1000))
			&& RangeFrom(p->Position()) < 1500.0f) //only can attack every 4 secs for now
		{
			short weapon_fx = 0;
			long weapon_time = 0;
			float range = RangeFrom(p->Position());

			if (range > 5000.0f)
			{
				LostTarget(p);
				return;
			}

			if (m_Attacking == false)
			{
				m_Attacking = true;
				p->AttackMusicUpdate(true, GameID());
			}

			//Send Attack FX!!
			switch (m_MOB_Data->m_Type)
			{
			case STRUCTURAL:
				weapon_fx = 33;
				weapon_time = 1000;
				break;
			case CYBERNETIC:
				weapon_fx = 34;
				weapon_time = 1000;
				break;
			case ENERGY:
				weapon_fx = 35;
				weapon_time = 1000;
				break;
			case ROCK_BASED:
				weapon_fx = 38;
				weapon_time = (long)(range*3.0f);
				break;
			case ORGANIC_GREEN:
				weapon_fx = 41;
				weapon_time = (long)(range*3.0f);
				break;
			default:
				weapon_fx = 43;
				weapon_time = (long)(range*3.0f);
				break;
			};

			SendFXToVisibilityList(p->GameID(), GameID(), weapon_time, weapon_fx);

			float damage_fraction = (float)((rand() % 13) + (rand() % 13) + (rand() % 13) + 3)/30.0f;
			float damage = m_WeaponDPS * damage_fraction * MOB_WEAPON_READY_TIME;

			// Remove hate for every hit you take
			SubtractHate(p->GameID(), (int)(damage/10.0f));

			//this will also send the client-damage packet
			GetSectorManager()->AddTimedCallPNode(&m_DamageNode, B_SHIP_DAMAGE, weapon_time, 0, GameID(), m_DamageType, 0, 0, 0, damage);
			m_DamageNode.player_id = p->GameID();
			m_LastAttackTime = current_tick;

			// Look for a new target with the most hate
			LockTarget();
		}
	}
	else
	{
		m_HostilityTargetID = 0;
	}
}

void MOB::SendFXToVisibilityList(long target_id, long source_id, long time_delay, short effect_id)
{
	Player *p = (0);

	ObjectToObjectEffect object_fx;
	object_fx.Bitmask = 0x07;
	object_fx.GameID = source_id;
	object_fx.TargetID = target_id;
	object_fx.EffectDescID = effect_id;
	object_fx.Message = 0;
	object_fx.EffectID = m_ObjectMgr->GetAvailableSectorID();
	object_fx.TimeStamp = GetNet7TickCount();
	object_fx.Duration = short(time_delay);

	while (g_PlayerMgr->GetNextPlayerOnList(p, m_RangeList))
	{
		p->SendObjectToObjectEffect(&object_fx);
	}
}

//This sends to all players who can see this MOB
void MOB::SendToVisibilityList(bool force_update)
{
	Player *p = (0);
	PositionInformation pos;

	if ((m_Destination || m_DestinationFlag) && !force_update) return; //don't send regular impluses if we have a target

	m_Mutex.Lock();
	UpdatePositionInfo(&pos);
	m_Mutex.Unlock();

	while (g_PlayerMgr->GetNextPlayerOnList(p, m_RangeList))
	{
		p->SendAdvancedPositionalUpdate(GameID(), &pos);
	}
}

void MOB::SendMOBData(Player *p)
{
	if (p->DebugPlayer()) return;

	//TODO: remove this once MOB data is done, it's only here for live MOB basset refresh
	//SetMOBType(m_MOB_Type);

	SendObject(p);
}

//tentative aggro settings:
/*
0 - green
1 - yellow - MOB may fire a warning shot or two if you get too close.
2 - yellow - as above, but doing more damage.
3 - yellow - as above, but may call in mates if you attack first.
4 - yellow - as above but with stronger chance of summoning help if you attack first.
5 - red - will attack when it spots you + same rules as 3. above.
6 - red - patrols area for players/enemy mobs.
7 - red - hunt for players, will summon help if it's getting in trouble.
8 - red - hunt for players, will summon all other MOBs in spawn within 5k as soon as it attacks.
*/

void MOB::SendRelationship(Player *p)
{
    long reaction = RELATIONSHIP_FRIENDLY;

	if (m_MOB_Data)
	{
		if (m_MOB_Data->m_Agressiveness > 0)
		{
			reaction = RELATIONSHIP_SHUN;
		}
		if (m_MOB_Data->m_Agressiveness > 4)
		{
			reaction = RELATIONSHIP_ATTACK;
		}
	}

    p->SendRelationship(GameID(), reaction, 0);
}

void MOB::SendAggroRelationship(Player *p)
{
	p->SendRelationship(GameID(), RELATIONSHIP_ATTACK, 0);
}

void MOB::Remove()
{
	Player *p = (0);

	SetActive(false);

	while (g_PlayerMgr->GetNextPlayerOnList(p, m_RangeList))
	{
		p->RemoveObject(GameID());
	}

	if (m_HostilityTargetID)
	{
		p = g_GlobMemMgr->GetPlayer(m_HostilityTargetID);
		LostTarget(p);
	}

	memset (&m_RangeList, 0, sizeof(m_RangeList));
	memset (&m_PlayerVisibleList, 0, sizeof(m_PlayerVisibleList));
	memset (&m_ClickedList, 0, sizeof(m_ClickedList));
	memset (&m_HateList, 0, sizeof(m_HateList));
}

void MOB::DamageMOB(long game_id, long damage_type, float damage, long inflicted)
{
	//TO-DO: allow mobs to have deflects, and calculate deflects properly
	/*float redux_percent;
	float damage;
	switch(damage_type)
	{
	case DAMAGE_IMPACT:
		//is this needed?
		redux_percent = m_Stats.GetStat(STAT_IMPACT_DEFLECT);
		damage = damage_orig * (1.0f - (redux_percent > 0.5f ? 0.5f : redux_percent));
		break;
	case DAMAGE_EXPLOSIVE:
		redux_percent = m_Stats.GetStat(STAT_EXPLOSIVE_DEFLECT);
		damage = damage_orig * (1.0f - (redux_percent > 0.5f ? 0.5f : redux_percent));
		break;
	case DAMAGE_PLASMA:
		redux_percent = m_Stats.GetStat(STAT_PLASMA_DEFLECT);
		damage = damage_orig * (1.0f - (redux_percent > 0.5f ? 0.5f : redux_percent));
		break;
	case DAMAGE_ENERGY:
		redux_percent = m_Stats.GetStat(STAT_ENERGY_DEFLECT);
		damage = damage_orig * (1.0f - (redux_percent > 0.5f ? 0.5f : redux_percent));
		break;
	case DAMAGE_EMP:
		redux_percent = m_Stats.GetStat(STAT_EMP_DEFLECT);
		damage = damage_orig * (1.0f - (redux_percent > 0.5f ? 0.5f : redux_percent));
		break;
	case DAMAGE_CHEMICAL:
		redux_percent = m_Stats.GetStat(STAT_CHEM_DEFLECT);
		damage = damage_orig * (1.0f - (redux_percent > 0.5f ? 0.5f : redux_percent));
		break;
	}*/

	//first take away the points
	m_ShieldLevel -= damage;
	if (m_ShieldLevel < 0.0f)
	{
		m_HullLevel += m_ShieldLevel;
		m_ShieldLevel = 0.0f;
	}
	AddHate(game_id, (int)damage);

	u32 tick = GetNet7TickCount();

	//issue mob hit FX - limit this to 1 per half second, pointless sending more
	if (tick > (m_DamageTime + 500))
	{
		SendMobDamage(damage, damage_type, game_id, inflicted);
		m_DamageTime = tick;
	}

	//is the MOB dead now?
	if (m_ShieldLevel <= 0.0f && m_HullLevel <= 0.0f && Active())
	{
		DestroyMOB(game_id);
	}
}

short husk_bassets[] =
{
	1963,
	1960,
	1961,
	1966,
	1962,
	1964,
	1965
};

void MOB::DestroyMOB(long game_id)
{
	//send explosion to everyone on rangelist
	SendExplosion();
	//remove MOB
	Remove();
	LostTarget();
	CalcOrientation(1.0f, 0.0f);
	SetHeading();

	SetRespawnTick(GetNet7TickCount() + 120000);
	//create & populate Husk
	Resource *husk = (Resource*) m_ObjectMgr->AddNewObject(OT_HUSK, false);
	husk->SetActive(false);

	if (!m_MOB_Data)
	{
		return;
	}

	if (husk)
	{
		husk->SetBasset(husk_bassets[m_MOB_Data->m_Type]);
		husk->SetHuskName(Name());
		husk->SetPosition(Position());
		husk->SetLevel((Level() / 8) + 1);
		husk->SetScale(20.0f);
		husk->ResetResource();
		husk->PopulateHusk(m_MOB_Type);
		husk->SendObjectReset(); //ensure contents are re-sent
		husk->SetCreateType(25);
	}

	//set target of destroying player to husk and give XP
	Player *p = (Player*)m_ObjectMgr->GetObjectFromID(game_id);

	if (p)
	{
		p->SendClientSound("On_Destroy",0,0);
		p->AddMOBDestroyExperience(Level(), Name());
		p->CheckMissionMOBKill(m_MOB_Type);
		husk->SetTargetOnCreate(p->GameID());
	}

	husk->SetActive(true);
}

void MOB::OnRespawn()
{
	m_ShieldLevel = m_StartShieldLevel;
	m_HullLevel   = m_StartHullLevel;
}

void MOB::SendExplosion()
{
	Player * p = (0);
	short explosion_basset;
	short explosion_fx;
	short explosion_sfx = 0;

	if (!m_MOB_Data)
	{
		return;
	}

	switch (m_MOB_Data->m_Type)
	{
	case STRUCTURAL:
		explosion_basset = 1976;
		explosion_fx = 1013;
		explosion_sfx = 0x2755; //TODO: bots only
		break;
	case ORGANIC_RED:
		explosion_basset = 1977;
		explosion_fx = 1014;
		break;
	case CRYSTALLINE:
		explosion_basset = 1978;
		explosion_fx = 1015;
		break;
	case CYBERNETIC:
		explosion_basset = 1979;
		explosion_fx = 1016;
		break;
	case ENERGY:
		explosion_basset = 1980;
		explosion_fx = 1017;
		if (m_MOB_Data->m_Name && strstr(m_MOB_Data->m_Name, "Manes") != 0)
		{
			LogMessage("exploding manes\n");
			explosion_sfx = 10167;
		}
		break;
	case ROCK_BASED:
		explosion_basset = 1981;
		explosion_fx = 1018;
		break;
	case ORGANIC_GREEN:
		explosion_basset = 1982;
		explosion_fx = 1019;
		break;
	default:
		explosion_basset = 1976;
		explosion_fx = 1013;
		break;
	}

	ObjectEffect ObjExplosion1;

	ObjExplosion1.Bitmask = 0x07;
	ObjExplosion1.GameID = GameID();
	ObjExplosion1.EffectDescID = explosion_sfx;
	ObjExplosion1.EffectID = m_ObjectMgr->GetAvailableSectorID();
	ObjExplosion1.TimeStamp = GetNet7TickCount();
	ObjExplosion1.Duration = 4000;

	ObjectEffect ObjExplosion2;

	ObjExplosion2.Bitmask = 0x07;
	ObjExplosion2.GameID = GameID();
	ObjExplosion2.EffectDescID = 0x018d; //red shockwave
	ObjExplosion2.EffectID = m_ObjectMgr->GetAvailableSectorID();
	ObjExplosion2.TimeStamp = GetNet7TickCount();
	ObjExplosion1.Duration = 4000;

	//PointEffect(Position(), explosion_fx);

	while (g_PlayerMgr->GetNextPlayerOnList(p, m_RangeList))
	{
		p->PointEffect(Position(), explosion_fx);
		p->UnSetTarget(GameID());
		if (explosion_sfx != 0)
		{
			p->SendObjectEffect(&ObjExplosion1);
		}
		p->SendObjectEffect(&ObjExplosion2);
	}
}


void MOB::UpdateObject()
{
	RangeListVec::iterator itrRList;
	Player * p = (0);

	//unsigned long tick = GetNet7TickCount();

	//do the MOB shield level
	if (m_LastTickShieldLevel != m_ShieldLevel || m_LastTickHullLevel != m_HullLevel)
	{
		bool shields = false;
		bool hull = false;
		if (m_LastTickShieldLevel != m_ShieldLevel)
		{
			m_ShipIndex.Shield.SetStartValue(m_ShieldLevel/m_StartShieldLevel);
			shields = true;
		}
		if (m_LastTickHullLevel != m_HullLevel)
		{
			m_ShipIndex.SetHullPoints(m_HullLevel);
			hull = true;
		}

		m_ShipIndex.Buffer()->Lock();
		m_ShipIndex.BuildDiffPacket(shields, hull);

		//shield level has changed, update clients in range ... maybe only do this for clients who are targetting this MOB?
		while (g_PlayerMgr->GetNextPlayerOnList(p, m_RangeList))
		{
			p->SendOpcode(ENB_OPCODE_001B_AUX_DATA, m_ShipIndex.m_DiffPacket, m_ShipIndex.m_DiffSize);
		}

		/* Unblock buffer */
		m_ShipIndex.Buffer()->Unlock();
		m_LastTickShieldLevel = m_ShieldLevel;
		m_LastTickHullLevel = m_HullLevel;
		m_ShieldFXSent = 0; //reset the shield FX send
	}
}

void MOB::SendMobDamage(float damage, long damage_type, long game_id, long inflicted)
{
	RangeListVec::iterator itrRList;
	Player * p = (0);

	ObjectEffect ObjExplosion;

	ObjExplosion.Bitmask = 0x07;
	ObjExplosion.GameID = GameID();
	ObjExplosion.EffectDescID = 566;
	ObjExplosion.EffectID = m_ObjectMgr->GetAvailableSectorID();
	ObjExplosion.TimeStamp = GetNet7TickCount();
	ObjExplosion.Duration = 1000;

	while (g_PlayerMgr->GetNextPlayerOnList(p, m_RangeList))
	{
		//if this is first couple of explosions, send everything
		if (m_ShieldFXSent < 2)
		{
			p->SendObjectToObjectLinkedEffect(this, p, 0x0021, 0x0003, 2.0f);
			p->SendObjectEffect(&ObjExplosion);
			m_ShieldFXSent++;
		}
		else if (p->GameID() == game_id && p->AttacksThisTick() == 0) //always send at least one effect for the attacking player
		{
			p->SendObjectToObjectLinkedEffect(this, p, 0x0021, 0x0003, 2.0f);
			p->IncAttacksThisTick();
		}
		//always send client damage
		p->SendClientDamage(GameID(), p->GameID(), damage, 0, damage_type, inflicted);
	}
}

void MOB::SendShieldLevel(Player *p)
{
	// TODO: Use shield regen when needed

	m_ShipIndex.Shield.SetStartValue(m_ShieldLevel/m_StartShieldLevel);

	SendAuxShip(p);
}

//defunct now
void MOB::SendShieldUpdate(Player *p)
{
	SendAuxDiff(p, SHIELD);
}

void MOB::SendAuxDiff(Player * p, long type)
{
    m_ShipIndex.Buffer()->Lock();

	bool shields = (type & SHIELD) ? true : false;
	bool hull = (type & HULL) ? true : false;

	m_ShipIndex.BuildDiffPacket(shields, hull);

    if (p)
    {
		p->SendOpcode(ENB_OPCODE_001B_AUX_DATA, m_ShipIndex.m_DiffPacket, m_ShipIndex.m_DiffSize);
    }

    /* Unblock buffer */
    m_ShipIndex.Buffer()->Unlock();
}

void MOB::SendAuxShip(Player * other)
{
    /* Block anyone else from using the buffer */
    m_ShipIndex.Buffer()->Lock();

	m_ShipIndex.BuildClickPacket();

    if (other)
    {
		other->SendOpcode(ENB_OPCODE_001B_AUX_DATA, m_ShipIndex.m_ClickPacket, m_ShipIndex.m_ClickSize);
    }

    /* Unblock buffer */
    m_ShipIndex.Buffer()->Unlock();
}

//also defunct
void MOB::SendHullUpdate(Player *p)
{
	// Update hull
	SendAuxDiff(p, HULL);
}

void MOB::UpdatePositionInfo(PositionInformation *pos)
{
	memcpy(pos, &m_Position_info, sizeof(m_Position_info));

	if (m_DestinationFlag)
	{
		pos->Bitmask |= 0x0100;
		pos->UpdatePeriod = m_ArrivalTime - GetNet7TickCount();
		pos->Position[0] = m_DestinationPos[0];
		pos->Position[1] = m_DestinationPos[1];
		pos->Position[2] = m_DestinationPos[2];
	}
}

void MOB::SendPosition(Player *player)
{
	PositionInformation pos;

	UpdatePositionInfo(&pos);

	player->SendAdvancedPositionalUpdate(
		GameID(),
		&pos);
}

void MOB::SendAuxDataPacket(Player *player)
{
	//  Build and send aux packet
    m_ShipIndex.Buffer()->Lock();

	m_ShipIndex.BuildCreatePacket();

	if (m_ShipIndex.m_CreateSize > 0)
	{
        player->SendOpcode(ENB_OPCODE_001B_AUX_DATA,
			m_ShipIndex.m_CreatePacket,
			(u16)m_ShipIndex.m_CreateSize);
	}


    m_ShipIndex.Buffer()->Unlock();
}

//called on creation of MOB graphic object for Player
void MOB::OnCreate(Player *player)
{

}

//Called when this MOB is targeted by player
void MOB::OnTargeted(Player *player)
{
	if (!IsClickedBy(player))
	{
		SendShieldLevel(player);
		SetClickedBy(player);
	}

	player->BlankVerbs();
	if (player->ShipIndex()->GetIsIncapacitated())
	{
		player->AddVerb(VERBID_FOLLOW, -1.0f);
	}
	else
	{
		player->AddVerb(VERBID_FOLLOW, 1500.0f);
	}
}

//this returns true if Player 'player' has clicked on the MOB
bool MOB::IsClickedBy(Player *player)
{
	return g_PlayerMgr->GetIndex(player->GameID(), m_ClickedList);
}

void MOB::SetBehaviour(short new_behaviour)
{
	m_Behaviour = new_behaviour;
}

void MOB::AddBehaviourObject(Object *obj)
{
	m_Mutex.Lock();
	m_BehaviourList.push_back(obj);
	m_Mutex.Unlock();
}

void MOB::AddBehaviourPosition(float *pos)
{
	m_HuntPosition[0] = pos[0];
	m_HuntPosition[1] = pos[1];
	m_HuntPosition[2] = pos[2];
}

void MOB::LostTarget(Player *p)
{
	if (p && m_HostilityTargetID == p->GameID())
	{
		if (m_Attacking == true) p->AttackMusicUpdate(false, GameID());
		//RemoveHate(p->GameID()); //Don't remove hate for now ... let MOBs keep a grudge!
		m_Attacking = false;
	}

	m_HostilityTargetID = 0;
	Turn(m_DefaultTurn);
	m_YInput = 0;
	m_Position_info.RotY = 0;

	m_Behaviour = m_DefaultBehaviour;
	m_Velocity = m_DefaultVelocity;
	m_Destination = (0);
	SetUpdateRate(m_DefaultUpdateRate);
}

void MOB::CalcNewHeading(float tdiff)
{
	float rot_Z[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	float rot_Y[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	float result[4];
	bool change = false;

	if (m_ZInput != 0.0f)
	{
		rot_Z[2] = -m_ZInput*tdiff*0.42f/883.0f*1000.0f;
		change = true;
	}

	if (m_YInput != 0.0f)
	{
		rot_Y[1] = -m_YInput*tdiff*0.42f/883.0f*1000.0f;
		change = true;
	}

	if (change)
	{
		Quat4fMul(rot_Z, rot_Y, result);
		Quat4fMul(Orientation(), result, Orientation());
		Quat4fNormalize(Orientation());
		SetHeading();
	}

	//work out behaviour
	MOBBehaviour();
}

void MOB::MOBBehaviour()
{
	m_DestinationFlag = false;
	switch(m_Behaviour)
	{
	case INVALID_BEHAVIOUR:
		{
			//see if this MOB is part of a field
			if (m_SpawnGroup)
			{
				Object *obj = m_SpawnGroup;
				if (obj->ObjectType() == OT_FIELD)
				{
					if (m_BehaviourList.size() == 0)
					{
						m_BehaviourList.push_back(obj);
					}
					m_Behaviour = PATROL_FIELD;
					m_DefaultBehaviour = PATROL_FIELD;
				}
			}
			else
			{
				m_Behaviour = CURIOUS;
				m_DefaultBehaviour = CURIOUS;
			}
		}
		break;

	case PATROL_FIELD:
		{
			//select a target to check out. TODO: If the MOB sees gaps in the field he'll sometimes check them out
			Field *field = (0);
			if (m_BehaviourList.size() > 0)
			{
				Object *obj = m_BehaviourList[0];
				if (obj->ObjectType() == OT_FIELD)
				{
					field = (Field*)m_BehaviourList[0];
				}
			}
			if (!field) return;

			//have we reached the current destination?
			if (m_Destination)
			{
				if (GetNet7TickCount() > m_ArrivalTime)
				{
					m_Destination = field->SetDestination(m_Destination);
					TravelToDestination();
				}
			}
			else
			{
				m_Destination = field->SetDestination(0);
				TravelToDestination();
			}
		}
		break;

	case PURSUE:
		//set off after Hostility target
		PursueTarget();
		break;
	default:
		break;
	}
}

void MOB::PursueTarget()
{
	Object *obj = m_ObjectMgr->GetObjectFromID(HostilityTarget());
	if (obj && GetNet7TickCount() > m_ArrivalTime)
	{
		Face(obj);
		m_Velocity = m_DefaultVelocity;
		// work out position to target, with distance to travel before next update
		float *_heading = Heading();
		float range = RangeFrom(obj->Position());
		float travel_distance = m_Velocity*((float)UpdateRate()*0.1f);

		if (range < travel_distance)
		{
			travel_distance = range - 350.0f; //aim for within a good fair distance
			m_Velocity = (travel_distance)/((float)UpdateRate()*0.1f);
			if (range < 500.0f)
			{
				m_Velocity = 0.0f;
				travel_distance = 0.0f;
			}
			else if (m_Velocity > m_DefaultVelocity * 1.2f)
			{
				m_Velocity = m_DefaultVelocity * 1.2f;
			}
		}

		//do we exceed range to host field
		if (m_DefaultBehaviour == PATROL_FIELD)
		{
			if (m_BehaviourList.size() > 0)
			{
				Object *f = m_BehaviourList[0];
				if (RangeFrom(f->Position(), true) > (5000.0f + f->Signature()))
				{
					LogMessage("Break off pursuit, range to field centre = %.2f.\n", RangeFrom(f->Position(), true));
					m_Behaviour = m_DefaultBehaviour; //break off pursuit
				}
			}
			else
			{
				m_DefaultBehaviour = CURIOUS;
				m_Behaviour = CURIOUS;
			}
		}

		m_DestinationFlag = true;

		//calculate destination
		m_DestinationPos[0] = PosX() + ( travel_distance * _heading[0] );
		m_DestinationPos[1] = PosY() + ( travel_distance * _heading[1] );
		m_DestinationPos[2] = PosZ() + ( travel_distance * _heading[2] );

		m_ArrivalTime = GetNet7TickCount() + UpdateRate()*100;

		SendLocationAndSpeed(true);
	}
}

void MOB::SetDefaultStats(float turn, short behaviour, float velocity, long update_rate)
{
	m_DefaultTurn = turn;
	m_DefaultBehaviour = behaviour;
	m_DefaultVelocity = velocity;
	m_DefaultUpdateRate = update_rate;
}

void MOB::TravelToDestination()
{
	m_ZInput = 0;
	m_Position_info.RotZ = 0;

	if (m_Destination == 0) return;

	Face(m_Destination);
	//calculate arrival time
	m_ArrivalTime = GetNet7TickCount() + (u32)((m_Destination->RangeFrom(Position())/m_Velocity)*1000.0f);
	/*m_DestinationPos[0] = m_Destination->PosX();
	m_DestinationPos[1] = m_Destination->PosY();
	m_DestinationPos[2] = m_Destination->PosZ();*/
	SendLocationAndSpeed(true);
}
