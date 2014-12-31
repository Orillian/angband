/**
 * \file player-birth.c
 * \brief Character creation
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cmd-core.h"
#include "cmds.h"
#include "dungeon.h"
#include "game-event.h"
#include "init.h"
#include "mon-lore.h"
#include "monster.h"
#include "obj-gear.h"
#include "obj-identify.h"
#include "obj-ignore.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-randart.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-birth.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "player.h"
#include "savefile.h"
#include "store.h"
#include "ui-input.h" /* msg_flag */

/*
 * Overview
 * ========
 * This file contains the game-mechanical part of the birth process.
 * To follow the code, start at player_birth towards the bottom of
 * the file - that is the only external entry point to the functions
 * defined here.
 *
 * Player (in the Angband sense of character) birth is modelled as a
 * a series of commands from the UI to the game to manipulate the
 * character and corresponding events to inform the UI of the outcomes
 * of these changes.
 *
 * The current aim of this section is that after any birth command
 * is carried out, the character should be left in a playable state.
 * In particular, this means that if a savefile is supplied, the
 * character will be set up according to the "quickstart" rules until
 * another race or class is chosen, or until the stats are reset by
 * the UI.
 *
 * Once the UI signals that the player is happy with the character, the
 * game does housekeeping to ensure the character is ready to start the
 * game (clearing the history log, making sure options are set, etc)
 * before returning control to the game proper.
 */


/* These functions are defined at the end of the file */
static int roman_to_int(const char *roman);
static int int_to_roman(int n, char *roman, size_t bufsize);


/* 
 * Maximum amount of starting equipment, and starting gold
 */
#define STARTING_GOLD 600


/*
 * Forward declare
 */
typedef struct birther /*lovely*/ birther; /*sometimes we think she's a dream*/

/*
 * A structure to hold "rolled" information, and any
 * other useful state for the birth process.
 *
 * XXX Demand Obama's birth certificate
 */
struct birther
{
	byte sex;
	const struct player_race *race;
	const struct player_class *class;

	s16b age;
	s16b wt;
	s16b ht;
	s16b sc;

	s32b au;

	s16b stat[STAT_MAX];

	char *history;
};


/*** All of these should be in some kind of 'birth state' struct somewhere else ***/

static int stats[STAT_MAX];
static int points_spent[STAT_MAX];
static int points_left;

static bool quickstart_allowed;
static bool rolled_stats = FALSE;

/*
 * The last character displayed, to allow the user to flick between two.
 * We rely on prev.age being zero to determine whether there is a stored
 * character or not, so initialise it here.
 */
static birther prev;

/*
 * If quickstart is allowed, we store the old character in this,
 * to allow for it to be reloaded if we step back that far in the
 * birth process.
 */
static birther quickstart_prev;




/*
 * Save the currently rolled data into the supplied 'player'.
 */
static void save_roller_data(birther *tosave)
{
	int i;

	/* Save the data */
	tosave->sex = player->psex;
	tosave->race = player->race;
	tosave->class = player->class;
	tosave->age = player->age;
	tosave->wt = player->wt_birth;
	tosave->ht = player->ht_birth;
	tosave->au = player->au_birth;

	/* Save the stats */
	for (i = 0; i < STAT_MAX; i++)
		tosave->stat[i] = player->stat_birth[i];

	tosave->history = player->history;
}


/*
 * Load stored player data from 'player' as the currently rolled data,
 * optionally placing the current data in 'prev_player' (if 'prev_player'
 * is non-NULL).
 *
 * It is perfectly legal to specify the same "birther" for both 'player'
 * and 'prev_player'.
 */
static void load_roller_data(birther *saved, birther *prev_player)
{
	int i;

     /* The initialisation is just paranoia - structure assignment is
        (perhaps) not strictly defined to work with uninitialised parts
        of structures. */
	birther temp;
	WIPE(&temp, birther);

	/*** Save the current data if we'll need it later ***/
	if (prev_player) save_roller_data(&temp);

	/*** Load the previous data ***/

	/* Load the data */
	player->psex     = saved->sex;
	player->race     = saved->race;
	player->class    = saved->class;
	player->age      = saved->age;
	player->wt       = player->wt_birth = player->wt;
	player->ht       = player->ht_birth = player->ht;
	player->au_birth = saved->au;
	player->au       = STARTING_GOLD;

	/* Load the stats */
	for (i = 0; i < STAT_MAX; i++)
	{
		player->stat_max[i] = player->stat_cur[i] = player->stat_birth[i] = saved->stat[i];
	}

	/* Load the history */
	player->history = saved->history;

	/*** Save the current data if the caller is interested in it. ***/
	if (prev_player) *prev_player = temp;
}


/*
 * Roll for a characters stats
 *
 * For efficiency, we include a chunk of "calc_bonuses()".
 */
static void get_stats(int stat_use[STAT_MAX])
{
	int i, j;

	int dice[18];


	/* Roll and verify some stats */
	while (TRUE)
	{
		/* Roll some dice */
		for (j = i = 0; i < 18; i++)
		{
			/* Roll the dice */
			dice[i] = randint1(3 + i % 3);

			/* Collect the maximum */
			j += dice[i];
		}

		/* Verify totals */
		if ((j > 42) && (j < 54)) break;
	}

	/* Roll the stats */
	for (i = 0; i < STAT_MAX; i++)
	{
		int bonus;

		/* Extract 5 + 1d3 + 1d4 + 1d5 */
		j = 5 + dice[3*i] + dice[3*i+1] + dice[3*i+2];

		/* Save that value */
		player->stat_max[i] = j;

		/* Obtain a "bonus" for "race" and "class" */
		bonus = player->race->r_adj[i] + player->class->c_adj[i];

		/* Variable stat maxes */

		/* Start fully healed */
		player->stat_cur[i] = player->stat_max[i];

		/* Efficiency -- Apply the racial/class bonuses */
		stat_use[i] = modify_stat_value(player->stat_max[i], bonus);

		player->stat_birth[i] = player->stat_max[i];
	}
}


static void roll_hp(void)
{
	int i, j, min_value, max_value;

	/* Minimum hitpoints at highest level */
	min_value = (PY_MAX_LEVEL * (player->hitdie - 1) * 3) / 8;
	min_value += PY_MAX_LEVEL;

	/* Maximum hitpoints at highest level */
	max_value = (PY_MAX_LEVEL * (player->hitdie - 1) * 5) / 8;
	max_value += PY_MAX_LEVEL;

	/* Roll out the hitpoints */
	while (TRUE)
	{
		/* Roll the hitpoint values */
		for (i = 1; i < PY_MAX_LEVEL; i++)
		{
			j = randint1(player->hitdie);
			player->player_hp[i] = player->player_hp[i-1] + j;
		}

		/* XXX Could also require acceptable "mid-level" hitpoints */

		/* Require "valid" hitpoints at highest level */
		if (player->player_hp[PY_MAX_LEVEL-1] < min_value) continue;
		if (player->player_hp[PY_MAX_LEVEL-1] > max_value) continue;

		/* Acceptable */
		break;
	}
}


static void get_bonuses(void)
{
	/* Calculate the bonuses and hitpoints */
	player->upkeep->update |= (PU_BONUS | PU_HP);

	/* Update stuff */
	update_stuff(player->upkeep);

	/* Fully healed */
	player->chp = player->mhp;

	/* Fully rested */
	player->csp = player->msp;
}


/*
 * Get the racial history, and social class, using the "history charts".
 */
char *get_history(struct history_chart *chart)
{
	struct history_entry *entry;
	char *res = NULL;

	while (chart) {
		int roll = randint1(100);
		for (entry = chart->entries; entry; entry = entry->next)
			if (roll <= entry->roll)
				break;
		assert(entry);

		res = string_append(res, entry->text);
		chart = entry->succ;
	}

	return res;
}


/*
 * Computes character's age, height, and weight
 */
static void get_ahw(struct player *p)
{
	/* Calculate the age */
	p->age = p->race->b_age + randint1(p->race->m_age);

	/* Calculate the height/weight for males */
	if (p->psex == SEX_MALE)
	{
		p->ht = p->ht_birth = Rand_normal(p->race->m_b_ht, p->race->m_m_ht);
		p->wt = p->wt_birth = Rand_normal(p->race->m_b_wt, p->race->m_m_wt);
	}

	/* Calculate the height/weight for females */
	else if (p->psex == SEX_FEMALE)
	{
		p->ht = p->ht_birth = Rand_normal(p->race->f_b_ht, p->race->f_m_ht);
		p->wt = p->wt_birth = Rand_normal(p->race->f_b_wt, p->race->f_m_wt);
	}

	/* For neither, go inbetween */
	else 
	{
		p->ht = p->ht_birth = Rand_normal(
			(p->race->f_b_ht + p->race->m_b_ht) / 2,
			(p->race->f_m_ht + p->race->m_m_ht) / 2);
		p->wt = p->wt_birth = Rand_normal(
			(p->race->f_b_wt + p->race->m_b_wt) / 2,
			(p->race->f_m_wt + p->race->m_m_wt) / 2);
	}
}




/*
 * Get the player's starting money
 */
static void get_money(void)
{
/*	if (OPT(birth_money))
	{
		player->au_birth = 200;
		player->au = 500;
	}
	else
	{                                              */
		player->au = player->au_birth = STARTING_GOLD;
}

void player_init(struct player *p)
{
	int i;

	if (p->upkeep) {
		if (p->upkeep->inven)
			mem_free(p->upkeep->inven);
		if (p->upkeep->quiver)
			mem_free(p->upkeep->quiver);
		mem_free(p->upkeep);
	}
	if (p->timed)
		mem_free(p->timed);

	/* Wipe the player */
	memset(p, 0, sizeof(struct player));

	/* Start with no artifacts made yet */
	for (i = 0; z_info && i < z_info->a_max; i++)
	{
		artifact_type *a_ptr = &a_info[i];
		a_ptr->created = FALSE;
		a_ptr->seen = FALSE;
	}

	/* Start with no quests */
	player_quests_reset(p);

	for (i = 1; z_info && i < z_info->k_max; i++) {
		object_kind *k_ptr = &k_info[i];
		k_ptr->tried = FALSE;
		k_ptr->aware = FALSE;
	}

	for (i = 1; z_info && i < z_info->r_max; i++)
	{
		monster_race *r_ptr = &r_info[i];
		monster_lore *l_ptr = &l_list[i];
		r_ptr->cur_num = 0;
		r_ptr->max_num = 100;
		if (rf_has(r_ptr->flags, RF_UNIQUE))
			r_ptr->max_num = 1;
		l_ptr->pkills = 0;
	}

	/* Always start with a well fed player (this is surely in the wrong fn) */
	p->food = PY_FOOD_FULL - 1;

	p->upkeep = mem_zalloc(sizeof(player_upkeep));
	p->upkeep->inven = mem_zalloc((z_info->pack_size + 1) * sizeof(struct object *));
	p->upkeep->quiver = mem_zalloc(z_info->quiver_size * sizeof(struct object *));
	p->timed = mem_zalloc(TMD_MAX * sizeof(s16b));

	/* First turn. */
	turn = 1;
	p->total_energy = 0;
	p->resting_turn = 0;
	/* XXX default race/class */
	p->race = races;
	p->class = classes;
}

/**
 * Try to wield everything wieldable in the inventory.
 */
void wield_all(struct player *p)
{
	struct object *obj, *new_pile = NULL;
	int slot;

	/* Scan through the slots */
	for (obj = p->gear; obj; obj = obj->next) {
		struct object *obj_temp;

		/* Skip non-objects */
		assert(obj);

		/* Make sure we can wield it */
		slot = wield_slot(obj);
		if (slot < 0 || slot >= p->body.count)
			continue;

		obj_temp = slot_object(p, slot);
		if (obj_temp)
			continue;

		/* Split if necessary */
		if (obj->number > 1) {
			/* All but one go to the new object */
			struct object *new = object_split(obj, obj->number - 1);

			/* Add to the pile of new objects to carry */
			pile_insert(&new_pile, new);
		}

		/* Wear the new stuff */
		p->body.slots[slot].obj = obj;

		/* Increment the equip counter by hand */
		p->upkeep->equip_cnt++;
	}

	/* Now add the unwielded split objects to the gear */
	if (new_pile)
		pile_insert_end(&player->gear, new_pile);

	return;
}


/*
 * Init players with some belongings
 *
 * Having an item identifies it and makes the player "aware" of its purpose.
 */
static void player_outfit(struct player *p)
{
	char buf[80];
	int i;
	const struct start_item *si;

	/* Player needs a body */
	memcpy(&p->body, &bodies[p->race->body], sizeof(p->body));
	my_strcpy(buf, bodies[p->race->body].name, sizeof(buf));
	p->body.name = string_make(buf);
	p->body.slots = mem_zalloc(p->body.count * sizeof(struct equip_slot));
	for (i = 0; i < p->body.count; i++) {
		p->body.slots[i].type = bodies[p->race->body].slots[i].type;
		my_strcpy(buf, bodies[p->race->body].slots[i].name, sizeof(buf));
		p->body.slots[i].name = string_make(buf);
	}

	/* Currently carrying nothing */
	p->upkeep->total_weight = 0;

	/* Give the player starting equipment */
	for (si = p->class->start_items; si; si = si->next) {
		/* Get local object */
		struct object *obj = object_new();
		int num = rand_range(si->min, si->max);

		/* Without start_kit, only start with 1 food and 1 light */
		if (!OPT(birth_start_kit)) {
			if (!tval_is_food_k(si->kind) && !tval_is_light_k(si->kind))
				continue;

			num = 1;
		}

		/* Prepare the item */
		object_prep(obj, si->kind, 0, MINIMISE);
		obj->number = num;
		obj->origin = ORIGIN_BIRTH;

		object_flavor_aware(obj);
		object_notice_everything(obj);

		inven_carry(p, obj, FALSE);
		si->kind->everseen = TRUE;

		/* Deduct the cost of the item from starting cash */
		p->au -= object_value(obj, obj->number, FALSE);
	}

	/* Sanity check */
	if (p->au < 0)
		p->au = 0;

	/* Now try wielding everything */
	wield_all(p);
}


/*
 * Cost of each "point" of a stat.
 */
static const int birth_stat_costs[18 + 1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 4 };

/* It was feasible to get base 17 in 3 stats with the autoroller */
#define MAX_BIRTH_POINTS 20 /* 3 * (1+1+1+1+1+1+2) */

static void recalculate_stats(int *stats, int points_left)
{
	int i;

	/* Variable stat maxes */
	for (i = 0; i < STAT_MAX; i++)
		player->stat_cur[i] = player->stat_max[i] =
				player->stat_birth[i] = stats[i];

	/* Gold is inversely proportional to cost */
	player->au_birth = STARTING_GOLD + (50 * points_left);

	/* Update bonuses, hp, etc. */
	get_bonuses();

	/* Tell the UI about all this stuff that's changed. */
	event_signal(EVENT_GOLD);
	event_signal(EVENT_AC);
	event_signal(EVENT_HP);
	event_signal(EVENT_STATS);
}

static void reset_stats(int stats[STAT_MAX], int points_spent[STAT_MAX], int *points_left, bool update_display)
{
	int i;

	/* Calculate and signal initial stats and points totals. */
	*points_left = MAX_BIRTH_POINTS;

	for (i = 0; i < STAT_MAX; i++)
	{
		/* Initial stats are all 10 and costs are zero */
		stats[i] = 10;
		points_spent[i] = 0;
	}

	/* Use the new "birth stat" values to work out the "other"
	   stat values (i.e. after modifiers) and tell the UI things have 
	   changed. */
	if (update_display)
	{
		recalculate_stats(stats, *points_left);
		event_signal_birthpoints(points_spent, *points_left);	
	}
}

static bool buy_stat(int choice, int stats[STAT_MAX], int points_spent[STAT_MAX], int *points_left, bool update_display)
{
	/* Must be a valid stat, and have a "base" of below 18 to be adjusted */
	if (!(choice >= STAT_MAX || choice < 0) &&	(stats[choice] < 18))
	{
		/* Get the cost of buying the extra point (beyond what
		   it has already cost to get this far). */
		int stat_cost = birth_stat_costs[stats[choice] + 1];

		if (stat_cost <= *points_left)
		{
			stats[choice]++;
			points_spent[choice] += stat_cost;
			*points_left -= stat_cost;

			if (update_display)
			{
				/* Tell the UI the new points situation. */
				event_signal_birthpoints(points_spent, *points_left);

				/* Recalculate everything that's changed because
				   the stat has changed, and inform the UI. */
				recalculate_stats(stats, *points_left);
			}

			return TRUE;
		}
	}

	/* Didn't adjust stat. */
	return FALSE;
}


static bool sell_stat(int choice, int stats[STAT_MAX], int points_spent[STAT_MAX],
	int *points_left, bool update_display)
{
	/* Must be a valid stat, and we can't "sell" stats below the base of 10. */
	if (!(choice >= STAT_MAX || choice < 0) && (stats[choice] > 10))
	{
		int stat_cost = birth_stat_costs[stats[choice]];

		stats[choice]--;
		points_spent[choice] -= stat_cost;
		*points_left += stat_cost;

		if (update_display)
		{
			/* Tell the UI the new points situation. */
			event_signal_birthpoints(points_spent, *points_left);

			/* Recalculate everything that's changed because
			   the stat has changed, and inform the UI. */
			recalculate_stats(stats, *points_left);
	
			return TRUE;
		}
	}

	/* Didn't adjust stat. */
	return FALSE;
}


/*
 * This picks some reasonable starting values for stats based on the
 * current race/class combo, etc.  For now I'm disregarding concerns
 * about role-playing, etc, and using the simple outline from
 * http://angband.oook.cz/forum/showpost.php?p=17588&postcount=6:
 *
 * 0. buy base STR 17
 * 1. if possible buy adj DEX of 18/10
 * 2. spend up to half remaining points on each of spell-stat and con, 
 *    but only up to max base of 16 unless a pure class 
 *    [mage or priest or warrior]
 * 3. If there are any points left, spend as much as possible in order 
 *    on DEX and then the non-spell-stat.
 */
static void generate_stats(int stats[STAT_MAX], int points_spent[STAT_MAX], 
						   int *points_left)
{
	int step = 0;
	int maxed[STAT_MAX] = { 0 };
	int spell_stat = player->class->magic.spell_realm->stat;
	bool caster = FALSE, warrior = FALSE;

	/* Determine whether the class is warrior */
	if (player->class->max_attacks > 5) { 
		warrior = TRUE;
	}
	
	/* Determine whether the class is priest/mage */
	if (player->class->max_attacks < 5) {
		caster = TRUE;
	}

	while (*points_left && step >= 0) {
	
		switch (step) {
		
			/* Buy base STR 17 */
			case 0: {
			
				if (!maxed[STAT_STR] && stats[STAT_STR] < 17) {
				
					if (!buy_stat(STAT_STR, stats, points_spent, points_left, FALSE))
						maxed[STAT_STR] = TRUE;
						
				} else {
				
					step++;
					
					/* If pure caster skip to step 3 */
					if (caster){
						step = 3;
					}
				}

				break;
			}

			/* Try and buy adj DEX of 18/10 */
			case 1: {
							
				if (!maxed[STAT_DEX] && player->state.stat_top[STAT_DEX] < 18+10){
				
					if (!buy_stat(STAT_DEX, stats, points_spent, points_left, FALSE))
						maxed[STAT_DEX] = TRUE;
						
				} else {
				
					step++;
				}

				break;
			}

			/* If we can't get 18/10 dex, sell it back. */
			case 2: {
			
				if (player->state.stat_top[STAT_DEX] < 18+10){
				
					while (stats[STAT_DEX] > 10)
						sell_stat(STAT_DEX, stats, points_spent, points_left, FALSE);

					maxed[STAT_DEX] = FALSE;
				}
				
				step++;
			}

			/* 
			 * Spend up to half remaining points on each of spell-stat and 
			 * con, but only up to max base of 16 unless a pure class 
			 * [mage or priest or warrior]
			 */
			case 3: 
			{
				int points_trigger = *points_left / 2;
				
				if (warrior) {
					points_trigger = *points_left;
				}

				if (!warrior) {
				
					while (!maxed[spell_stat] &&
						   (caster || stats[spell_stat] < 16) &&
						   points_spent[spell_stat] < points_trigger) {
						   
						if (!buy_stat(spell_stat, stats, points_spent,
									  points_left, FALSE)) {
									  
							maxed[spell_stat] = TRUE;
						}

						if (points_spent[spell_stat] > points_trigger) {
						
							sell_stat(spell_stat, stats, points_spent, 
									  points_left, FALSE);
							maxed[spell_stat] = TRUE;
						}
					}
				}

				/* Skip CON for casters because DEX is more important early and is handled in 4 */
				while (!maxed[STAT_CON] &&
					   !(caster) && stats[STAT_CON] < 16 &&
					   points_spent[STAT_CON] < points_trigger) {
					   
					if (!buy_stat(STAT_CON, stats, points_spent,points_left, FALSE)) {
						maxed[STAT_CON] = TRUE;
					}
					
					if (points_spent[STAT_CON] > points_trigger) {
						sell_stat(STAT_CON, stats, points_spent, points_left, FALSE);
						maxed[STAT_CON] = TRUE;
					}
				}
				
				step++;
				break;
			}

			/* 
			 * If there are any points left, spend as much as possible in 
			 * order on DEX, and the non-spell-stat. 
			 */
			case 4:{
			
				int next_stat;

				if (!maxed[STAT_DEX]) {
					next_stat = STAT_DEX;
				} else if (!maxed[STAT_INT] && spell_stat != STAT_INT) {
					next_stat = STAT_INT;
				} else if (!maxed[STAT_WIS] && spell_stat != STAT_WIS) {
					next_stat = STAT_WIS;
				} else {
					step++;
					break;
				}

				/* Buy until we can't buy any more. */
				while (buy_stat(next_stat, stats, points_spent, points_left, FALSE));
				maxed[next_stat] = TRUE;

				break;
			}

			default: {
			
				step = -1;
				break;
			}
		}
	}
	/* Tell the UI the new points situation. */
	event_signal_birthpoints(points_spent, *points_left);

	/* Recalculate everything that's changed because
	   the stat has changed, and inform the UI. */
	recalculate_stats(stats, *points_left);
}

/*
 * This fleshes out a full player based on the choices currently made,
 * and so is called whenever things like race or class are chosen.
 */
void player_generate(struct player *p, const player_sex *s,
		const struct player_race *r, const struct player_class *c)
{
	if (!s) s = &sex_info[p->psex];
	if (!c)
		c = p->class;
	if (!r)
		r = p->race;

	p->sex = s;
	p->class = c;
	p->race = r;

	/* Level 1 */
	p->max_lev = p->lev = 1;

	/* Experience factor */
	p->expfact = p->race->r_exp + p->class->c_exp;

	/* Hitdice */
	p->hitdie = p->race->r_mhp + p->class->c_mhp;

	/* Initial hitpoints */
	p->mhp = p->hitdie;

	/* Pre-calculate level 1 hitdice */
	p->player_hp[0] = p->hitdie;

	/* Roll for age/height/weight */
	get_ahw(p);

	p->history = get_history(p->race->history);
}


/* Reset everything back to how it would be on loading the game. */
static void do_birth_reset(bool use_quickstart, birther *quickstart_prev)
{
	/* If there's quickstart data, we use it to set default
	   character choices. */
	if (use_quickstart && quickstart_prev)
		load_roller_data(quickstart_prev, NULL);

	player_generate(player, NULL, NULL, NULL);

	player->depth = 0;

	/* Update stats with bonuses, etc. */
	get_bonuses();
}

void do_cmd_birth_init(struct command *cmd)
{
	char *buf;

	/* The dungeon is not ready */
	character_dungeon = FALSE;

	/*
	 * If there's a quickstart character, store it for later use.
	 * If not, default to whatever the first of the choices is.
	 */
	if (player->ht_birth) {
		save_roller_data(&quickstart_prev);
		quickstart_allowed = TRUE;
	} else {
		player_generate(player, &sex_info[player->psex], player_id2race(0), player_id2class(0));
		quickstart_allowed = FALSE;
	}

	/* Handle incrementing name suffix */
	buf = find_roman_suffix_start(op_ptr->full_name);
	if (buf) {
		/* Try to increment the roman suffix */
		int success = int_to_roman((roman_to_int(buf) + 1), buf,
			(sizeof(op_ptr->full_name) - (buf -
			(char *)&op_ptr->full_name)));
			
		if (!success) msg("Sorry, could not deal with suffix");
	}
	
	/* We're ready to start the birth process */
	event_signal_flag(EVENT_ENTER_BIRTH, quickstart_allowed);
}

void do_cmd_birth_reset(struct command *cmd)
{
	player_init(player);
	reset_stats(stats, points_spent, &points_left, FALSE);
	do_birth_reset(quickstart_allowed, &quickstart_prev);
	rolled_stats = FALSE;
}

void do_cmd_choose_sex(struct command *cmd)
{
	int choice;
	cmd_get_arg_choice(cmd, "choice", &choice);
	player->psex = choice;
	player_generate(player, NULL, NULL, NULL);
}

void do_cmd_choose_race(struct command *cmd)
{
	int choice;
	cmd_get_arg_choice(cmd, "choice", &choice);
	player_generate(player, NULL, player_id2race(choice), NULL);

	reset_stats(stats, points_spent, &points_left, FALSE);
	generate_stats(stats, points_spent, &points_left);
	rolled_stats = FALSE;
}

void do_cmd_choose_class(struct command *cmd)
{
	int choice;
	cmd_get_arg_choice(cmd, "choice", &choice);
	player_generate(player, NULL, NULL, player_id2class(choice));

	reset_stats(stats, points_spent, &points_left, FALSE);
	generate_stats(stats, points_spent, &points_left);
	rolled_stats = FALSE;
}

void do_cmd_buy_stat(struct command *cmd)
{
	/* .choice is the stat to sell */
	if (!rolled_stats) {
		int choice;
		cmd_get_arg_choice(cmd, "choice", &choice);
		buy_stat(choice, stats, points_spent, &points_left, TRUE);
	}
}

void do_cmd_sell_stat(struct command *cmd)
{
	/* .choice is the stat to sell */
	if (!rolled_stats) {
		int choice;
		cmd_get_arg_choice(cmd, "choice", &choice);
		sell_stat(choice, stats, points_spent, &points_left, TRUE);
	}
}

void do_cmd_reset_stats(struct command *cmd)
{
	/* .choice is whether to regen stats */
	int choice;

	reset_stats(stats, points_spent, &points_left, TRUE);

	cmd_get_arg_choice(cmd, "choice", &choice);
	if (choice)
		generate_stats(stats, points_spent, &points_left);

	rolled_stats = FALSE;
}

void do_cmd_roll_stats(struct command *cmd)
{
	int i;

	save_roller_data(&prev);

	/* Get a new character */
	get_stats(stats);

	/* Update stats with bonuses, etc. */
	get_bonuses();

	/* There's no real need to do this here, but it's tradition. */
	get_ahw(player);
	player->history = get_history(player->race->history);

	event_signal(EVENT_GOLD);
	event_signal(EVENT_AC);
	event_signal(EVENT_HP);
	event_signal(EVENT_STATS);

	/* Give the UI some dummy info about the points situation. */
	points_left = 0;
	for (i = 0; i < STAT_MAX; i++)
		points_spent[i] = 0;

	event_signal_birthpoints(points_spent, points_left);

	/* Lock out buying and selling of stats based on rolled stats. */
	rolled_stats = TRUE;
}

void do_cmd_prev_stats(struct command *cmd)
{
	/* Only switch to the stored "previous"
	   character if we've actually got one to load. */
	if (prev.age) {
		load_roller_data(&prev, &prev);
		get_bonuses();
	}

	event_signal(EVENT_GOLD);
	event_signal(EVENT_AC);
	event_signal(EVENT_HP);
	event_signal(EVENT_STATS);	
}

void do_cmd_choose_name(struct command *cmd)
{
	const char *str;
	cmd_get_arg_string(cmd, "name", &str);

	/* Set player name */
	my_strcpy(op_ptr->full_name, str, sizeof(op_ptr->full_name));

	string_free((char *) str);
}

void do_cmd_accept_character(struct command *cmd)
{
	int i;

	/* Reset score options from cheat options */
	for (i = 0; i < OPT_MAX; i++) {
		if (option_type(i) == OP_CHEAT)
			op_ptr->opt[i + 1] = op_ptr->opt[i];
	}

	roll_hp();

	ignore_birth_init();

	/* Clear old messages, add new starting message */
	history_clear();
	history_add("Began the quest to destroy Morgoth.", HIST_PLAYER_BIRTH, 0);

	/* Reset message prompt (i.e. no extraneous -more-s) */
	/* XXX-AS remove and then remove ui-input include */
	msg_flag = TRUE;

	/* Note player birth in the message recall */
	message_add(" ", MSG_GENERIC);
	message_add("  ", MSG_GENERIC);
	message_add("====================", MSG_GENERIC);
	message_add("  ", MSG_GENERIC);
	message_add(" ", MSG_GENERIC);

	/* Give the player some money */
	get_money();

	/* Outfit the player, if they can sell the stuff */
	player_outfit(player);

	/* Initialise the spells */
	player_spells_init(player);

	/* Initialise the stores */
	store_reset();

	/* Seed for random artifacts */
	if (!seed_randart || !OPT(birth_keep_randarts))
		seed_randart = randint0(0x10000000);

	/* Randomize the artifacts if required */
	if (OPT(birth_randarts))
		do_randart(seed_randart, TRUE);

	/* Set the savefile name if it's not already set */
	if (!savefile[0])
		savefile_set_name(player_safe_name(player, TRUE));

	/* Seed for flavors */
	seed_flavor = randint0(0x10000000);
	flavor_init();

	/* Stop the player being quite so dead */
	player->is_dead = FALSE;

	/* Character is now "complete" */
	character_generated = TRUE;
	player->upkeep->playing = TRUE;

	/* Now we're really done.. */
	event_signal(EVENT_LEAVE_BIRTH);

	/* Generate a dungeon level if needed */
	cave_generate(&cave, player);
}



/*** Roman numeral functions, for dynastic successions ***/

/*
 * Find the start of a possible Roman numerals suffix by going back from the
 * end of the string to a space, then checking that all the remaining chars
 * are valid Roman numerals.
 * 
 * Return the start position, or NULL if there isn't a valid suffix. 
 */
char *find_roman_suffix_start(const char *buf)
{
	const char *start = strrchr(buf, ' ');
	const char *p;
	
	if (start)
	{
		start++;
		p = start;
		while (*p)
		{
			if (*p != 'I' && *p != 'V' && *p != 'X' && *p != 'L' &&
			    *p != 'C' && *p != 'D' && *p != 'M')
			{
				start = NULL;
				break;
			}
			++p;			    
		}
	}
	return (char *)start;
}

/*
 * Converts an arabic numeral (int) to a roman numeral (char *).
 *
 * An arabic numeral is accepted in parameter `n`, and the corresponding
 * upper-case roman numeral is placed in the parameter `roman`.  The
 * length of the buffer must be passed in the `bufsize` parameter.  When
 * there is insufficient room in the buffer, or a roman numeral does not
 * exist (e.g. non-positive integers) a value of 0 is returned and the
 * `roman` buffer will be the empty string.  On success, a value of 1 is
 * returned and the zero-terminated roman numeral is placed in the
 * parameter `roman`.
 */
static int int_to_roman(int n, char *roman, size_t bufsize)
{
	/* Roman symbols */
	char roman_symbol_labels[13][3] =
		{"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX",
		 "V", "IV", "I"};
	int  roman_symbol_values[13] =
		{1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};

	/* Clear the roman numeral buffer */
	roman[0] = '\0';

	/* Roman numerals have no zero or negative numbers */
	if (n < 1)
		return 0;

	/* Build the roman numeral in the buffer */
	while (n > 0)
	{
		int i = 0;

		/* Find the largest possible roman symbol */
		while (n < roman_symbol_values[i])
			i++;

		/* No room in buffer, so abort */
		if (strlen(roman) + strlen(roman_symbol_labels[i]) + 1
			> bufsize)
			break;

		/* Add the roman symbol to the buffer */
		my_strcat(roman, roman_symbol_labels[i], bufsize);

		/* Decrease the value of the arabic numeral */
		n -= roman_symbol_values[i];
	}

	/* Ran out of space and aborted */
	if (n > 0)
	{
		/* Clean up and return */
		roman[0] = '\0';

		return 0;
	}

	return 1;
}


/*
 * Converts a roman numeral (char *) to an arabic numeral (int).
 *
 * The null-terminated roman numeral is accepted in the `roman`
 * parameter and the corresponding integer arabic numeral is returned.
 * Only upper-case values are considered. When the `roman` parameter
 * is empty or does not resemble a roman numeral, a value of -1 is
 * returned.
 *
 * XXX This function will parse certain non-sense strings as roman
 *     numerals, such as IVXCCCVIII
 */
static int roman_to_int(const char *roman)
{
	size_t i;
	int n = 0;
	char *p;

	char roman_token_chr1[] = "MDCLXVI";
	const char *roman_token_chr2[] = {0, 0, "DM", 0, "LC", 0, "VX"};

	int roman_token_vals[7][3] = {{1000},
	                              {500},
	                              {100, 400, 900},
	                              {50},
	                              {10, 40, 90},
	                              {5},
	                              {1, 4, 9}};

	if (strlen(roman) == 0)
		return -1;

	/* Check each character for a roman token, and look ahead to the
	   character after this one to check for subtraction */
	for (i = 0; i < strlen(roman); i++)
	{
		char c1, c2;
		int c1i, c2i;

		/* Get the first and second chars of the next roman token */
		c1 = roman[i];
		c2 = roman[i + 1];

		/* Find the index for the first character */
		p = strchr(roman_token_chr1, c1);
		if (p)
		{
			c1i = p - roman_token_chr1;
		} else {
			return -1;
		}

		/* Find the index for the second character */
		c2i = 0;
		if (roman_token_chr2[c1i] && c2)
		{
			p = strchr(roman_token_chr2[c1i], c2);
			if (p)
			{
				c2i = (p - roman_token_chr2[c1i]) + 1;
				/* Two-digit token, so skip a char on the next pass */
				i++;
			}
		}

		/* Increase the arabic numeral */
		n += roman_token_vals[c1i][c2i];
	}

	return n;
}
