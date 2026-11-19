#include <modtopia/modtopia.h>
#include <stdatomic.h>
#include <string.h>
#include "eligible_items.h"
#include "waits_data.h"

MT_DECLARE_MOD()

static const MtHost *host;
static MtMod *self;
static _Atomic float sprint_factor = 1.5f;
static _Atomic float run_factor = 1.5f;
static _Atomic unsigned craft_factor = 3;
static _Atomic unsigned collection_factor = 3;
static _Atomic float food_buff_factor = 2.0f;
static _Atomic int magnet_rise_enabled = 1;
static _Atomic float magnet_rise_pp_factor = 0.5f;
static uint64_t game_base;
static _Atomic float wait_factor = 0.5f;
static _Atomic unsigned config_revision = 1;
static _Atomic int save_loaded = 0;
static unsigned applied_revision;

static void settings_changed(MtMod *mod, const char *key, void *user) {
    (void)mod;
    (void)key;
    (void)user;
    double sprint = host->config_number(self, "sprint");
    double run = host->config_number(self, "run");
    double craft = host->config_number(self, "craft");
    double collection = host->config_number(self, "collection");
    double food_buff = host->config_number(self, "food_buff");
    double magnet_rise_pp = host->config_number(self, "magnet_rise_pp");
    double wait = host->config_number(self, "wait");
    if (!(sprint >= 1.0 && sprint <= 3.0)) sprint = 1.5;
    if (!(run >= 1.0 && run <= 3.0)) run = 1.5;
    if (!(craft >= 1.0 && craft <= 5.0)) craft = 3.0;
    if (!(collection >= 1.0 && collection <= 5.0)) collection = 3.0;
    if (!(food_buff >= 1.0 && food_buff <= 5.0)) food_buff = 2.0;
    if (!(magnet_rise_pp >= 0.1 && magnet_rise_pp <= 1.0)) magnet_rise_pp = 0.5;
    if (!(wait >= 0.1 && wait <= 1.0)) wait = 0.5;
    atomic_store_explicit(&sprint_factor, (float)sprint, memory_order_relaxed);
    atomic_store_explicit(&run_factor, (float)run, memory_order_relaxed);
    atomic_store_explicit(&craft_factor, (unsigned)(craft + 0.5), memory_order_relaxed);
    atomic_store_explicit(&collection_factor, (unsigned)(collection + 0.5), memory_order_relaxed);
    atomic_store_explicit(&food_buff_factor, (float)food_buff, memory_order_relaxed);
    atomic_store_explicit(&magnet_rise_enabled, host->config_bool(self, "magnet_rise"), memory_order_relaxed);
    atomic_store_explicit(&magnet_rise_pp_factor, (float)magnet_rise_pp, memory_order_relaxed);
    atomic_store_explicit(&wait_factor, (float)wait, memory_order_relaxed);
    atomic_fetch_add_explicit(&config_revision, 1, memory_order_release);
}

static uint64_t read_pointer(uint64_t address) {
    uint64_t result = 0;
    if (!address || host->mem_read(address, &result, sizeof result) != MT_OK) return 0;
    return result;
}

enum { MAGNET_RISE = 13 };

static int read_magnet_rise_flags(uint64_t save, uint8_t *flags) {
    return save && host->mem_read(save + 0x6d82f0 + MAGNET_RISE * 8, flags, sizeof *flags) == MT_OK;
}

static int magnet_rise_locked(void) {
    if (atomic_load_explicit(&magnet_rise_enabled, memory_order_relaxed)) return 0;
    uint64_t save = read_pointer(game_base + 0x3561930);
    uint8_t flags;
    return read_magnet_rise_flags(save, &flags) && !(flags & 1);
}

static int magnet_rise_list(MtHookCtx *ctx, void *user) {
    (void)user;
    /* Pretend this move is learned while building the wheel's assignment menu. */
    if (host->reg_get(ctx, 21) == MAGNET_RISE &&
        atomic_load_explicit(&magnet_rise_enabled, memory_order_relaxed))
        host->reg_set(ctx, 8, host->reg_get(ctx, 8) | 1);
    return MT_CONTINUE;
}

static int magnet_rise_wheel(MtHookCtx *ctx, void *user) {
    (void)user;
    /* Hide a mod-assigned slot while the unlock is switched off. */
    if (host->reg_get(ctx, 0) == MAGNET_RISE && magnet_rise_locked())
        host->reg_set(ctx, 0, 0);
    return MT_CONTINUE;
}

static int magnet_rise_wheel_slots(MtHookCtx *ctx, void *user) {
    (void)user;
    if (!atomic_load_explicit(&magnet_rise_enabled, memory_order_relaxed)) return MT_CONTINUE;
    uint64_t save = read_pointer(game_base + 0x3561930);
    uint8_t flags;
    if (!read_magnet_rise_flags(save, &flags) || (flags & 1)) return MT_CONTINUE;

    /* The wheel menu has copied seven native slots and is about to store the
       eighth. This buffer also supplies the native move-selection callback. */
    uint64_t wheel = host->reg_get(ctx, 21);
    uint8_t slots[8];
    if (host->mem_read(wheel, slots, sizeof slots - 1) != MT_OK) return MT_CONTINUE;
    slots[7] = (uint8_t)host->reg_get(ctx, 0);
    if (memchr(slots, MAGNET_RISE, sizeof slots)) return MT_CONTINUE;
    uint8_t *empty = memchr(slots, 0, sizeof slots);
    if (empty && empty != &slots[7]) {
        const uint8_t move = MAGNET_RISE;
        host->mem_write(wheel + (uint64_t)(empty - slots), &move, sizeof move);
    } else {
        host->reg_set(ctx, 0, MAGNET_RISE);
    }
    return MT_CONTINUE;
}

static int magnet_rise_select(MtHookCtx *ctx, void *user) {
    (void)user;
    /* The native selector also serves the controller's move cycling path. */
    if ((uint8_t)host->reg_get(ctx, 1) == MAGNET_RISE && magnet_rise_locked())
        return MT_HANDLED;
    return MT_CONTINUE;
}

static int magnet_rise_drain(MtHookCtx *ctx, void *user) {
    (void)user;
    float factor = atomic_load_explicit(&magnet_rise_pp_factor, memory_order_relaxed);
    if ((uint8_t)host->reg_get(ctx, 20) == MAGNET_RISE && factor < 1.0f) {
        /* Scale the elapsed step of the PP timer before its native subtraction. */
        float elapsed = mt_vreg_f32(host, ctx, 8);
        if (elapsed > 0.0f) mt_vreg_set_f32(host, ctx, 8, elapsed * factor);
    }
    return MT_CONTINUE;
}

static int magnet_rise_action_pp(MtHookCtx *ctx, void *user) {
    (void)user;
    float factor = atomic_load_explicit(&magnet_rise_pp_factor, memory_order_relaxed);
    if ((uint8_t)host->reg_get(ctx, 20) == MAGNET_RISE && factor < 1.0f) {
        /* w8 is the charge in native integer PP units. Keep a positive debit. */
        uint32_t cost = (uint32_t)host->reg_get(ctx, 8);
        if (cost && cost <= UINT16_MAX) {
            uint32_t reduced = (uint32_t)(cost * factor);
            host->reg_set(ctx, 8, reduced ? reduced : 1);
        }
    }
    return MT_CONTINUE;
}

static int collection_item(uint32_t id) {
    switch (id) {
    case 6: case 7: case 8: case 14: case 20: case 21: /* mushrooms, seaweed, vegetables */
    case 61: case 63: case 64: case 65: case 67: case 2340: /* berries */
    case 101: case 102: case 109: case 110: case 111: case 112: /* raw materials */
    case 106: case 107: case 113: /* copper, iron and gold ore */
    case 290: case 404: case 520: case 565: case 708: /* shell, stone, watermelon, leaf, apple */
    case 1090: case 1205: case 1206: case 1216: case 1419: case 1482: case 1801: case 2035:
        return 1;
    default:
        return 0;
    }
}

static void boost_collection_stack(uint64_t item_address, uint64_t count_address, unsigned factor) {
    uint32_t item = 0;
    uint8_t count = 0;
    if (host->mem_read(item_address, &item, sizeof item) != MT_OK ||
        !collection_item(item & 0x1fff) ||
        host->mem_read(count_address, &count, sizeof count) != MT_OK || !count)
        return;
    unsigned total = count * factor;
    count = total > UINT8_MAX ? UINT8_MAX : (uint8_t)total;
    host->mem_write(count_address, &count, sizeof count);
}

static int collection_drops(MtHookCtx *ctx, void *user) {
    (void)user;
    unsigned factor = atomic_load_explicit(&collection_factor, memory_order_relaxed);
    if (factor == 1) return MT_CONTINUE;
    /* The crop preparation caller caches its roll. Scale at harvest instead.
       Drops::Roll saved the caller's return address in its frame prologue. */
    uint64_t caller = read_pointer(host->reg_get(ctx, MT_REG_SP) + 8);
    if (!caller || caller == game_base + 0x7a4cbc) return MT_CONTINUE;
    uint32_t count = 0;
    uint64_t stacks = host->reg_get(ctx, 20);
    if (!stacks || host->mem_read(host->reg_get(ctx, 19), &count, sizeof count) != MT_OK ||
        count > 4) return MT_CONTINUE;
    for (uint32_t i = 0; i < count; ++i)
        boost_collection_stack(stacks + 16 * i + 8, stacks + 16 * i + 12, factor);
    return MT_CONTINUE;
}

static int collection_harvest(MtHookCtx *ctx, void *user) {
    (void)user;
    unsigned factor = atomic_load_explicit(&collection_factor, memory_order_relaxed);
    if (factor != 1) {
        /* The fresh harvest reward is finalized before the inventory check. */
        uint64_t sp = host->reg_get(ctx, MT_REG_SP);
        boost_collection_stack(sp + 0x48, sp + 0x4c, factor);
    }
    return MT_CONTINUE;
}

static int collection_ripe_produce(MtHookCtx *ctx, void *user) {
    (void)user;
    unsigned factor = atomic_load_explicit(&collection_factor, memory_order_relaxed);
    /* The plant-coordinate lookup provides the produce item in w11. This
       branch creates a new stack of one without calling Drops::Roll. */
    if (factor != 1 && collection_item((uint32_t)host->reg_get(ctx, 11)))
        host->reg_set(ctx, 10, factor);
    return MT_CONTINUE;
}

static int food_buff(MtHookCtx *ctx, void *user) {
    (void)user;
    float factor = atomic_load_explicit(&food_buff_factor, memory_order_relaxed);
    if (factor == 1.0f) return MT_CONTINUE;

    /* A fresh consumption record contains three 20-byte effects. The game
       uses the same amounts to decide whether a meal can be eaten and to
       grant its power-up. Each selected effect has a u16 reserve limit. */
    uint64_t effects = host->reg_get(ctx, 19);
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t address = effects + i * 0x14;
        uint32_t type_and_amount[2];
        if (host->mem_read(address, type_and_amount, sizeof type_and_amount) != MT_OK)
            continue;
        uint32_t type = type_and_amount[0];
        uint32_t amount = type_and_amount[1];
        /* Leafage, Water Gun, Rock Smash/Rollout, Cut and Surf. */
        if (!amount || (type != 1 && type != 3 && type != 4 && type != 5 && type != 10))
            continue;
        float scaled = amount * factor;
        amount = scaled >= UINT16_MAX ? UINT16_MAX : (uint32_t)(scaled + 0.5f);
        host->mem_write(address + sizeof type, &amount, sizeof amount);
    }
    return MT_CONTINUE;
}

static uint64_t guest_lookup(uint64_t function, uint64_t object, uint64_t key) {
    if (!function || !object) return 0;
    MtCall args = {0};
    args.x[0] = object;
    args.x[1] = key;
    if (host->call_guest(function, &args) != MT_OK) return 0;
    return args.x[0];
}

static uint64_t table_rows(unsigned slot) {
    uint64_t base = host->module_base("main");
    if (!base) return 0;
    uint64_t manager = read_pointer(base + 0x35618f0);
    uint32_t count = 0;
    if (!manager || host->mem_read(manager + 0x2b70, &count, sizeof count) != MT_OK ||
        count <= slot) return 0;
    uint64_t table = read_pointer(manager + 0x10 + slot * 8);
    uint64_t vtable = read_pointer(table);
    return vtable ? guest_lookup(read_pointer(vtable + 0x38), table, 0) : 0;
}

static uint64_t table_record(uint64_t rows, unsigned id) {
    uint64_t vtable = read_pointer(rows);
    if (!vtable) return 0;
    uint64_t wrapper = guest_lookup(read_pointer(vtable + 0x30), rows, id);
    return wrapper ? read_pointer(wrapper + 8) : 0;
}

/* Run on the game's thread, after its save has loaded. Editing only each
   selected recipe's output byte makes its preview, capacity checks, batch
   crafting and actual awards all use the same count. Ingredients are separate
   fields and are never written. Baselines prevent multiplying twice. */
static int apply_crafting(unsigned factor) {
    uint64_t rows = table_rows(0x66);
    if (!rows) return 0;
    for (size_t i = 0; i < BUILDER_BOOST_ELIGIBLE_RECIPE_COUNT; ++i) {
        const BuilderBoostRecipe *entry = &kBuilderBoostEligibleRecipes[i];
        uint64_t record = table_record(rows, entry->recipe_id);
        uint16_t item = 0;
        if (!record || host->mem_read(record + 0x12, &item, sizeof item) != MT_OK ||
            item != entry->item_id || !builder_boost_item_allowed(item)) return 0;
        unsigned quantity = entry->base_count * factor;
        if (!quantity || quantity > UINT8_MAX) return 0;
        uint8_t output = (uint8_t)quantity;
        if (host->mem_write(record + 0x21, &output, sizeof output) != MT_OK) return 0;
    }
    mt_logf(host, self, MT_LOG_INFO, "Crafting output set to %ux for %zu common recipes",
            factor, (size_t)BUILDER_BOOST_ELIGIBLE_RECIPE_COUNT);
    return 1;
}

static int apply_processing_waits(float factor) {
    uint64_t rows = table_rows(0x66);
    if (!rows) return 0;
    for (size_t i = 0; i < BUILDER_BOOST_PROCESSING_WAIT_COUNT; ++i) {
        const BuilderBoostProcessingWait *entry = &kBuilderBoostProcessingWaits[i];
        uint64_t record = table_record(rows, entry->recipe_id);
        uint16_t item = 0;
        if (!record || host->mem_read(record + 0x12, &item, sizeof item) != MT_OK ||
            item != entry->item_id) return 0;
        unsigned seconds = (unsigned)(entry->base_seconds * factor + 0.5f);
        if (entry->base_seconds && !seconds) seconds = 1;
        uint16_t duration = (uint16_t)seconds;
        if (host->mem_write(record, &duration, sizeof duration) != MT_OK) return 0;
    }
    return 1;
}

static int frame_start(MtMod *mod, const MtEvent *event, void *user) {
    (void)mod;
    (void)event;
    (void)user;
    if (!atomic_load_explicit(&save_loaded, memory_order_acquire)) return MT_EVENT_CONTINUE;
    unsigned revision = atomic_load_explicit(&config_revision, memory_order_acquire);
    if (revision != applied_revision &&
        apply_crafting(atomic_load_explicit(&craft_factor, memory_order_relaxed)) &&
        apply_processing_waits(atomic_load_explicit(&wait_factor, memory_order_relaxed)))
        applied_revision = revision;
    return MT_EVENT_CONTINUE;
}

static int loaded(MtMod *mod, const MtEvent *event, void *user) {
    (void)mod;
    (void)event;
    (void)user;
    atomic_store_explicit(&save_loaded, 1, memory_order_release);
    atomic_fetch_add_explicit(&config_revision, 1, memory_order_release);
    return frame_start(mod, event, user);
}

static int player_movement(MtHookCtx *ctx) {
    uint64_t character = 0;
    uint8_t flags = 0;
    uint64_t actor = host->reg_get(ctx, 20);
    if (!actor || host->mem_read(actor + 8, &character, sizeof character) != MT_OK ||
        !character || host->mem_read(character + 0x10, &flags, sizeof flags) != MT_OK ||
        !(flags & 1)) return 0;
    return 1;
}

static int sprint_speed(MtHookCtx *ctx, void *user) {
    (void)user;
    /* Player::MoveSpeed, mode 3 only, after its native sprint acceleration.
       The ordinary walking/full-stick speed branches never reach this hook. */
    if (!player_movement(ctx)) return MT_CONTINUE;
    uint8_t value[16];
    if (host->vreg_get(ctx, 8, value) != MT_OK) return MT_CONTINUE;
    float speed;
    memcpy(&speed, value, sizeof speed);
    speed *= atomic_load_explicit(&sprint_factor, memory_order_relaxed);
    memcpy(value, &speed, sizeof speed);
    host->vreg_set(ctx, 8, value);
    return MT_CONTINUE;
}

static int run_speed(MtHookCtx *ctx, void *user) {
    (void)user;
    /* Mode 2 interpolates from walking to full-stick movement. Mode 1 shares
       this stop but must retain its native walking speed. Sprint bypasses it. */
    if ((uint32_t)host->reg_get(ctx, 26) != 2 || !player_movement(ctx))
        return MT_CONTINUE;
    uint8_t value[16];
    if (host->vreg_get(ctx, 0, value) != MT_OK) return MT_CONTINUE;
    float speed;
    memcpy(&speed, value, sizeof speed);
    speed *= atomic_load_explicit(&run_factor, memory_order_relaxed);
    memcpy(value, &speed, sizeof speed);
    host->vreg_set(ctx, 0, value);
    return MT_CONTINUE;
}

static int scale_wait_register(MtHookCtx *ctx, int reg) {
    uint8_t value[16];
    if (host->vreg_get(ctx, reg, value) != MT_OK) return MT_CONTINUE;
    float seconds;
    memcpy(&seconds, value, sizeof seconds);
    if (seconds > 0.0f) {
        seconds *= atomic_load_explicit(&wait_factor, memory_order_relaxed);
        memcpy(value, &seconds, sizeof seconds);
        host->vreg_set(ctx, reg, value);
    }
    return MT_CONTINUE;
}

static int construction_wait(MtHookCtx *ctx, void *user) {
    (void)user;
    /* Final construction seconds, after native helper/engineer adjustments. */
    return scale_wait_register(ctx, 8);
}

static int pokemon_spawn_check(MtHookCtx *ctx, void *user) {
    (void)user;
    /* Fresh native habitat-check interval in s1, immediately before comparing
       it with elapsed time in s0. Candidate selection remains native. */
    return scale_wait_register(ctx, 1);
}

static int pokemon_spawn_wait(MtHookCtx *ctx, void *user) {
    (void)user;
    /* Conditional appearance delay in s9. The game has already checked the
       habitat conditions and still performs its normal queue/cap checks. */
    return scale_wait_register(ctx, 9);
}

MT_EXPORT int mod_init(const MtHost *h, MtMod *mod) {
    host = h;
    self = mod;
    game_base = host->module_base("main");
    settings_changed(mod, NULL, NULL);
    if (host->on_config_changed(mod, settings_changed, NULL) != MT_OK)
        return MT_ERR;
    if (host->hook(mod, "sprint_speed", sprint_speed, NULL) != MT_OK)
        return MT_ERR;
    if (host->hook(mod, "run_speed", run_speed, NULL) != MT_OK)
        return MT_ERR;
    if (host->hook(mod, "construction_wait", construction_wait, NULL) != MT_OK)
        return MT_ERR;
    if (host->hook(mod, "collection_drops", collection_drops, NULL) != MT_OK ||
        host->hook(mod, "collection_harvest", collection_harvest, NULL) != MT_OK ||
        host->hook(mod, "collection_ripe_produce", collection_ripe_produce, NULL) != MT_OK)
        return MT_ERR;
    if (host->hook(mod, "pokemon_spawn_check", pokemon_spawn_check, NULL) != MT_OK ||
        host->hook(mod, "pokemon_spawn_wait", pokemon_spawn_wait, NULL) != MT_OK)
        return MT_ERR;
    if (host->hook(mod, "food_buff", food_buff, NULL) != MT_OK)
        return MT_ERR;
    if (host->hook(mod, "magnet_rise_list", magnet_rise_list, NULL) != MT_OK ||
        host->hook(mod, "magnet_rise_wheel", magnet_rise_wheel, NULL) != MT_OK ||
        host->hook(mod, "magnet_rise_wheel_slots", magnet_rise_wheel_slots, NULL) != MT_OK ||
        host->hook(mod, "magnet_rise_select", magnet_rise_select, NULL) != MT_OK ||
        host->hook(mod, "magnet_rise_drain", magnet_rise_drain, NULL) != MT_OK ||
        host->hook(mod, "magnet_rise_action_pp", magnet_rise_action_pp, NULL) != MT_OK)
        return MT_ERR;
    if (host->subscribe(mod, "SaveLoaded", MT_PRIORITY_NORMAL, loaded, NULL) != MT_OK ||
        host->subscribe(mod, "FrameStart", MT_PRIORITY_NORMAL, frame_start, NULL) != MT_OK)
        return MT_ERR;
    host->log(mod, MT_LOG_INFO, "Sprint, common crafting and shorter waits loaded");
    return MT_OK;
}
