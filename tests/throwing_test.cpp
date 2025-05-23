#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "damage.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "projectile.h"
#include "ranged.h"
#include "test_statistics.h"
#include "type_id.h"

static const itype_id itype_grenade( "grenade" );
static const itype_id itype_javelin_iron( "javelin_iron" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_throwing_stick( "throwing_stick" );

static const skill_id skill_throw( "throw" );

TEST_CASE( "throwing_distance_test", "[throwing], [balance]" )
{
    const standard_npc thrower( "Thrower", { 60, 60, 0 }, {}, 4, 10, 10, 10, 10 );
    item grenade( itype_grenade );
    CHECK( thrower.throw_range( grenade ) >= 30 );
    CHECK( thrower.throw_range( grenade ) <= 35 );
}

struct throw_test_data {
    statistics<bool> hits;
    statistics<double> dmg;

    throw_test_data() : dmg( Z95 ) {}
};

struct throw_test_pstats {
    int skill_lvl;
    int str;
    int dex;
    int per;
};

static std::ostream &operator<<( std::ostream &stream, const throw_test_pstats &pstats )
{
    return stream << "STR: " << pstats.str << " DEX: " << pstats.dex <<
           " PER: " << pstats.per << " SKL: " << pstats.skill_lvl;
}

static void reset_player( Character &you, const throw_test_pstats &pstats,
                          const tripoint_bub_ms &pos )
{
    map &here = get_map();
    clear_character( you );
    CHECK( !you.in_vehicle );
    you.setpos( here, pos );
    you.str_max = pstats.str;
    you.dex_max = pstats.dex;
    you.per_max = pstats.per;
    you.set_str_bonus( 0 );
    you.set_per_bonus( 0 );
    you.set_dex_bonus( 0 );
    you.set_skill_level( skill_throw, pstats.skill_lvl );
}

// If tests are routinely failing you should:
//  1. Make sure some change hasn't caused some regression
//  2. Make sure test is accurate by testing with a large minimum iterations (min > 5000)
//  3. Increase bounds on thresholds
//  4. Increase max iterations which will make the CI smaller and more likely to
//     fit inside the threshold but also increase the average test length
// In that order.
static constexpr int min_throw_test_iterations = 100;
static constexpr int max_throw_test_iterations = 10000;

// tighter thresholds here will increase accuracy but also increase average test
// time since more samples are required to get a more accurate test
static void test_throwing_player_versus(
    Character &you, const std::string &mon_id, const itype_id &throw_id,
    const int range, const throw_test_pstats &pstats,
    const epsilon_threshold &hit_thresh, const epsilon_threshold &dmg_thresh,
    const int min_throws = min_throw_test_iterations,
    int max_throws = max_throw_test_iterations )
{
    const tripoint_bub_ms monster_start = { 30 + range, 30, 0 };
    const tripoint_bub_ms player_start = { 30, 30, 0 };
    bool hit_thresh_met = false;
    bool dmg_thresh_met = false;
    throw_test_data data;
    item it( throw_id );

    max_throws = std::max( min_throws, max_throws );
    do {
        reset_player( you, pstats, player_start );
        you.set_moves( 1000 );
        you.set_stamina( you.get_stamina_max() );

        you.wield( it );
        monster &mon = spawn_test_monster( mon_id, monster_start, false );
        mon.set_moves( 0 );

        dealt_projectile_attack atk = you.throw_item( mon.pos_bub(), it );
        data.hits.add( atk.last_hit_critter != nullptr );
        data.dmg.add( atk.dealt_dam.total_damage() );

        if( data.hits.n() >= min_throws ) {
            // ideally we should actually still checking the threshold after we
            // meet it but we're busy people and don't have time for that
            if( !hit_thresh_met ) {
                hit_thresh_met = data.hits.test_threshold( hit_thresh );
            }
            // don't do an else here because it's possible we just made
            // hit_thresh_met true
            if( hit_thresh_met ) {
                // commenting this out is a super easy way to force all the
                // test to fail if you want to reset the baseline after
                // making balance changes or if many of the tests are failing
                dmg_thresh_met = data.dmg.test_threshold( dmg_thresh );
            }
        }
        g->remove_zombie( mon );
        you.remove_weapon();
        // only need to check dmg_thresh_met because it can only be true if
        // hit_thresh_met first
    } while( !dmg_thresh_met && data.hits.n() < max_throws );

    INFO( "Monster: '" << mon_id << "' Item: '" << throw_id.c_str() );
    INFO( "Range: " << range << " Pstats: " << pstats );
    INFO( "Total throws: " << data.hits.n() );
    INFO( "Ratio: " << data.hits.avg() * 100 << "%" );
    INFO( "Hit Lower: " << data.hits.lower() * 100 << "% Hit Upper: " << data.hits.upper() * 100 <<
          "%" );
    INFO( "Hit Thresh: " << ( hit_thresh.midpoint - hit_thresh.epsilon ) * 100 << "% - " <<
          ( hit_thresh.midpoint + hit_thresh.epsilon ) * 100 << "%" );
    INFO( "Adj Wald error: " << data.hits.margin_of_error() );
    INFO( "Avg total damage: " << data.dmg.avg() );
    INFO( "Dmg Lower: " << data.dmg.lower() << " Dmg Upper: " << data.dmg.upper() );
    INFO( "Dmg Thresh: " << dmg_thresh.midpoint - dmg_thresh.epsilon << " - " <<
          dmg_thresh.midpoint + dmg_thresh.epsilon );
    INFO( "Margin of error: " << data.hits.margin_of_error() );
    CHECK( dmg_thresh_met );
}

// Debugging function to force a large sample count.  This will fail but will
// dump an extremely accurate population sample that can be used to tune a
// broken test.
// WARNING: these will take a long time likely
/*
static void test_throwing_player_versus(
    player &p, const std::string &mon_id, const itype_id &throw_id, const int range,
    const throw_test_pstats &pstats )
{
    test_throwing_player_versus( p, mon_id, throw_id, range, pstats, { 0, 0 }, { 0, 0 }, 5000, 5000 );
}
*/

static constexpr throw_test_pstats lo_skill_base_stats = { 0, 8, 8, 8 };
static constexpr throw_test_pstats mid_skill_base_stats = { MAX_SKILL / 2, 8, 8, 8 };
static constexpr throw_test_pstats hi_skill_base_stats = { MAX_SKILL, 8, 8, 8 };
static constexpr throw_test_pstats hi_skill_athlete_stats = { MAX_SKILL, 12, 12, 12 };

TEST_CASE( "basic_throwing_sanity_tests", "[throwing],[balance]" )
{
    avatar &p = get_avatar();
    clear_map();

    SECTION( "test_player_vs_zombie_rock_basestats" ) {
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 1, lo_skill_base_stats, { 0.78, 0.10 }, { 5, 3 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 5, lo_skill_base_stats, { 0.07, 0.10 }, { 0.7, 2 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 10, lo_skill_base_stats, { 0.04, 0.10 }, { 0.5, 2 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 15, lo_skill_base_stats, { 0.03, 0.10 }, { 0.5, 2 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 20, lo_skill_base_stats, { 0.03, 0.10 }, { 0.5, 2 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 25, lo_skill_base_stats, { 0.03, 0.10 }, { 0.5, 2 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 30, lo_skill_base_stats, { 0.03, 0.10 }, { 0.5, 2 } );
    }

    SECTION( "test_player_vs_zombie_javelin_iron_basestats" ) {
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 1, lo_skill_base_stats, { 0.64, 0.10 }, { 11, 5 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 5, lo_skill_base_stats, { 0.05, 0.10 }, { 1.5, 3 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 10, lo_skill_base_stats, { 0.04, 0.10 }, { 1.50, 2 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 15, lo_skill_base_stats, { 0.03, 0.10 }, { 1.29, 3 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 20, lo_skill_base_stats, { 0.03, 0.10 }, { 1.66, 2 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 25, lo_skill_base_stats, { 0.03, 0.10 }, { 1.0, 2 } );
    }

    SECTION( "test_player_vs_zombie_rock_athlete" ) {
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 1, hi_skill_athlete_stats, { 1.00, 0.10 }, { 16.5, 8 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 5, hi_skill_athlete_stats, { 1.00, 0.10 }, { 16.5, 6 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 10, hi_skill_athlete_stats, { 1.00, 0.10 }, { 16.27, 6 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 15, hi_skill_athlete_stats, { 0.97, 0.10 }, { 12.83, 4 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 20, hi_skill_athlete_stats, { 0.82, 0.10 }, { 9.10, 4 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 25, hi_skill_athlete_stats, { 0.64, 0.10 }, { 6.54, 4 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 30, hi_skill_athlete_stats, { 0.47, 0.10 }, { 4.90, 3 } );
    }

    SECTION( "test_player_vs_zombie_javelin_iron_athlete" ) {
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 1, hi_skill_athlete_stats, { 1.00, 0.10 }, { 34.00, 8 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 5, hi_skill_athlete_stats, { 1.00, 0.10 }, { 34.00, 8 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 10, hi_skill_athlete_stats, { 1.00, 0.10 }, { 34.16, 8 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 15, hi_skill_athlete_stats, { 0.97, 0.10 }, { 25.21, 6 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 20, hi_skill_athlete_stats, { 0.82, 0.10 }, { 18.90, 5 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 25, hi_skill_athlete_stats, { 0.63, 0.10 }, { 13.59, 5 } );
        test_throwing_player_versus( p, "mon_zombie", itype_javelin_iron, 30, hi_skill_athlete_stats, { 0.48, 0.10 }, { 10.00, 4 } );
    }
}

TEST_CASE( "throwing_skill_impact_test", "[throwing],[balance]" )
{
    avatar &p = get_avatar();
    clear_map();

    // we already cover low stats in the sanity tests and we only cover a few
    // ranges here because what we're really trying to capture is the effect
    // the throwing skill has while the sanity tests are more explicit.
    SECTION( "mid_skill_basestats_rock" ) {
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 5, mid_skill_base_stats, { 1.00, 0.10 }, { 12, 6 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 10, mid_skill_base_stats, { 0.86, 0.10 }, { 7.0, 4 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 15, mid_skill_base_stats, { 0.52, 0.10 }, { 3, 2 } );
    }

    SECTION( "hi_skill_basestats_rock" ) {
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 5, hi_skill_base_stats, { 1.00, 0.10 }, { 18, 5 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 10, hi_skill_base_stats, { 1.00, 0.10 }, { 14.7, 5 } );
        test_throwing_player_versus( p, "mon_zombie", itype_rock, 15, hi_skill_base_stats, { 0.97, 0.10 }, { 10.5, 4 } );
    }
}

static void test_player_kills_monster(
    Character &you, const std::string &mon_id, const itype_id &item_id, const int range,
    const int dist_thresh, const throw_test_pstats &pstats, const int iterations )
{
    const tripoint_bub_ms monster_start = { 30 + range, 30, 0 };
    const tripoint_bub_ms player_start = { 30, 30, 0 };
    int failure_turns = -1;
    int failure_num_items = -1;
    int failure_last_range = -1;
    item it( item_id );
    int num_failures = 0;

    // We want to be real sure it isn't possible so do it a bunch of times
    // until we manage to make it happen or if we hit iterations then we're
    // good.
    for( int i = 0; i < iterations; i++ ) {
        bool mon_is_dead = false;
        int turns = 0;
        int num_items = 0;
        int last_range = -1;

        reset_player( you, pstats, player_start );

        monster &mon = spawn_test_monster( mon_id, monster_start, false );
        mon.set_moves( 0 );

        while( !mon_is_dead ) {

            ++turns;
            mon.process_turn();
            mon.set_dest( you.pos_abs() );
            while( mon.get_moves() > 0 ) {
                mon.move();
            }

            // zombie made it to player, we're done with this iteration
            if( ( last_range = rl_dist( you.pos_abs(), mon.pos_abs() ) ) <= dist_thresh ) {
                break;
            }

            you.mod_moves( you.get_speed() );
            while( you.get_moves() > 0 ) {
                you.wield( it );
                you.throw_item( mon.pos_bub(), it );
                you.remove_weapon();
                ++num_items;
            }
            mon_is_dead = mon.is_dead();
        }

        if( !mon_is_dead ) {
            g->remove_zombie( mon );
        } else {
            ++num_failures;
            failure_turns = turns;
            failure_num_items = num_items;
            failure_last_range = last_range;
        }
    }

    INFO( "You killed him :( He had kids you know." );
    INFO( "Distance - Start: " << range << " End: " << failure_last_range );
    INFO( "Turns: " << failure_turns );
    INFO( "# Items thrown: " << failure_num_items );
    CHECK( num_failures <= 1 );
}

TEST_CASE( "player_kills_zombie_before_reach", "[throwing],[balance][scenario]" )
{
    avatar &p = get_avatar();
    clear_map();

    SECTION( "test_player_kills_zombie_with_rock_basestats" ) {
        test_player_kills_monster( p, "mon_zombie", itype_rock, 15, 1, lo_skill_base_stats, 500 );
    }
}

TEST_CASE( "time_to_throw_independent_of_number_of_projectiles", "[throwing],[balance]" )
{
    Character &you = get_avatar();
    clear_avatar();

    item thrown( itype_throwing_stick, calendar::turn, 10 );
    REQUIRE( thrown.charges > 1 );
    you.wield( thrown );
    int initial_moves = -1;
    while( thrown.charges > 0 ) {
        const int cost = throw_cost( you, thrown );
        if( initial_moves < 0 ) {
            initial_moves = cost;
        } else {
            CHECK( initial_moves == cost );
        }
        thrown.charges--;
    }
}
