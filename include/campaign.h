#ifndef WEACT_CAMPAIGN_H
#define WEACT_CAMPAIGN_H

#include <stdbool.h>
#include <stdint.h>

enum { CHECKPOINT_SLOTS = 8, CHECKPOINT_AUTO_SLOT = CHECKPOINT_SLOTS - 1 };

typedef struct {
    int32_t episode, map, skill, carry_player;
    int32_t total_time, health, armor, armor_type, ready_weapon, backpack, did_secret;
    int32_t weapon_owned[9], ammo[4], max_ammo[4];
} Checkpoint;

unsigned campaign_boot(bool force);
int campaign_mode(void);
bool campaign_has_map(int episode, int map);
bool campaign_needs_pack(int episode, int map);
bool campaign_request(const Checkpoint *entry);
const Checkpoint *campaign_pending(void);
const Checkpoint *campaign_slot(unsigned slot);
bool campaign_save_slot(unsigned slot, const Checkpoint *entry);
bool campaign_save_auto(const Checkpoint *entry);
void campaign_read_settings(void *data, unsigned length);
bool campaign_write_settings(const void *data, unsigned length);

#endif
