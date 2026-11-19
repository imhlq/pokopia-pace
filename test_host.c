/* Native ABI tests: load the shipped DLL, emulate only Modtopia's host calls. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <modtopia/modtopia.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eligible_items.h"

struct MtHookCtx { uint64_t x[36]; uint8_t v[32][16]; };
typedef struct { char name[64]; MtHookFn fn; void *user; } Hook;
typedef struct { char name[64]; MtEventFn fn; void *user; } Event;
static Hook hooks[32];
static Event events[16];
static unsigned nhooks, nevents, checks, writes;
static MtConfigFn changed;
static void *changed_user;
static double sprint_setting = 1.5, run_setting = 1.5, craft_setting = 3, wait_setting = 0.5;
static double collection_setting = 3.0;
static double food_buff_setting = 2.0;
static double magnet_rise_pp_setting = 0.5;
static int magnet_rise_setting = 1;
static MtMod *const mod_handle = (MtMod *)(uintptr_t)0x1234;
static const uint64_t memory_base = UINT64_C(0x10000000);
static unsigned char guest_memory[0x700000];
static uint64_t manager_pointer;
static uint64_t save_pointer;
static unsigned guest_calls;
static uint64_t const manager_address = UINT64_C(0x10003000);
static uint64_t const table_function = UINT64_C(0x7100f00100);
static uint64_t const row_function = UINT64_C(0x7100f00200);
static uint8_t original_records[1500][44];
static uint8_t eligible_recipe[1500];
static uint8_t original_construction[128][40];
/* Native case data and intended inclusion are independent of eligible_items.h.
   This makes selection omissions and category overreach observable in tests. */
typedef struct {
    unsigned recipe_id;
    uint16_t item_id;
    uint8_t base_count, boosted;
    uint16_t ingredient_ids[2];
    uint8_t ingredient_counts[2];
    const char *name;
} CraftCase;
static const CraftCase craft_cases[] = {
    {124, 1624, 10, 0, {407, 116}, {2, 2}, "gold handrail"},
    {125, 1625, 10, 1, {407, 107}, {2, 2}, "boat railing"},
    {285, 1463, 10, 1, {115, 404}, {1, 1}, "railway track"},
    {403, 1709, 10, 1, {407, 0}, {2, 0}, "wooden handrail"},
    {102, 1384, 10, 1, {107, 101}, {2, 2}, "harbor pole"},
    {139, 1527, 5, 1, {101, 0}, {2, 0}, "rope"},
    {418, 1976, 10, 1, {404, 108}, {1, 1}, "skylight"},
    {539, 2079, 5, 1, {115, 0}, {2, 0}, "metal chain"},
    {618, 2108, 5, 1, {115, 0}, {2, 0}, "wire"},
    {837, 603, 5, 1, {101, 110}, {1, 1}, "netting"},
    {477, 1686, 1, 1, {110, 0}, {1, 0}, "simple pillar quarter"},
    {512, 1817, 1, 1, {115, 0}, {2, 0}, "iron scaffold"},
    {777, 48, 1, 1, {115, 0}, {1, 0}, "iron-scaffold plank"},
    {782, 1857, 1, 1, {404, 0}, {2, 0}, "arch bridge"},
    {89, 1562, 1, 0, {407, 0}, {2, 0}, "wooden partition"},
    {797, 401, 1, 0, {110, 0}, {5, 0}, "house partition (big)"},
    {921, 843, 1, 0, {110, 0}, {2, 0}, "house partition (medium)"},
    {922, 844, 1, 0, {110, 0}, {1, 0}, "house partition (small)"},
    {371, 1472, 5, 0, {407, 0}, {2, 0}, "underground hatch"},
    {322, 1177, 10, 0, {117, 0}, {2, 0}, "crystal wall"},
    {784, 764, 1, 0, {404, 0}, {2, 0}, "workbench"},
    {98, 1380, 5, 0, {102, 107}, {2, 2}, "string lights"},
    {1206, 119, 3, 0, {115, 0}, {1, 0}, "Tinkagear"},
    {1184, 221, 5, 0, {2340, 0}, {1, 0}, "red paint"},
};
/* Distinct guest objects route the same virtual method to different tables. */
typedef struct {
    uint64_t table, rows, wrappers, records;
    unsigned row_count, stride;
} TableFixture;
static TableFixture tables[8];
static unsigned ntables;
static TableFixture *construction_table;
static uint64_t fixture_record(const TableFixture *t, unsigned id) { return t->records + id * t->stride; }
static uint64_t fixture_wrapper(const TableFixture *t, unsigned id) { return t->wrappers + id * 16; }
static uint64_t record_address(unsigned id) { return fixture_record(&tables[0], id); }

#define CHECK(condition, label) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", label, __LINE__); exit(1); } } while (0)

static int mem_read(uint64_t addr, void *out, size_t n) {
    if (addr == UINT64_C(0x71035618f0) && n == sizeof manager_pointer) {
        memcpy(out, &manager_pointer, n); return MT_OK;
    }
    if (addr == UINT64_C(0x7103561930) && n == sizeof save_pointer) {
        memcpy(out, &save_pointer, n); return MT_OK;
    }
    if (addr < memory_base || addr - memory_base > sizeof guest_memory ||
        n > sizeof guest_memory - (size_t)(addr - memory_base)) return MT_ERR_FAULT;
    memcpy(out, guest_memory + (size_t)(addr - memory_base), n);
    return MT_OK;
}
static int mem_write(uint64_t addr, const void *in, size_t n) {
    if (addr < memory_base || addr - memory_base > sizeof guest_memory ||
        n > sizeof guest_memory - (size_t)(addr - memory_base)) return MT_ERR_FAULT;
    memcpy(guest_memory + (size_t)(addr - memory_base), in, n);
    ++writes;
    return MT_OK;
}
static void put(uint64_t addr, const void *value, size_t n) {
    CHECK(mem_write(addr, value, n) == MT_OK, "fixture memory write");
}
static int hook(MtMod *mod, const char *name, MtHookFn fn, void *user) {
    CHECK(mod == mod_handle, "hook mod identity");
    CHECK(nhooks < sizeof hooks / sizeof hooks[0], "hook count bounded");
    snprintf(hooks[nhooks].name, sizeof hooks[nhooks].name, "%s", name);
    hooks[nhooks].fn = fn; hooks[nhooks++].user = user;
    return MT_OK;
}
static uint64_t reg_get(MtHookCtx *ctx, int reg) { return ctx->x[reg]; }
static int reg_set(MtHookCtx *ctx, int reg, uint64_t value) { ctx->x[reg] = value; return MT_OK; }
static int vreg_get(MtHookCtx *ctx, int reg, void *out) { memcpy(out, ctx->v[reg], 16); return MT_OK; }
static int vreg_set(MtHookCtx *ctx, int reg, const void *in) { memcpy(ctx->v[reg], in, 16); return MT_OK; }
static double config_number(MtMod *mod, const char *key) {
    CHECK(mod == mod_handle, "config mod identity");
    if (!strcmp(key, "sprint")) return sprint_setting;
    if (!strcmp(key, "run")) return run_setting;
    if (!strcmp(key, "craft")) return craft_setting;
    if (!strcmp(key, "collection")) return collection_setting;
    if (!strcmp(key, "food_buff")) return food_buff_setting;
    if (!strcmp(key, "magnet_rise_pp")) return magnet_rise_pp_setting;
    if (!strcmp(key, "wait")) return wait_setting;
    fprintf(stderr, "Unexpected setting %s\n", key); exit(1);
}
static int config_bool(MtMod *mod, const char *key) {
    CHECK(mod == mod_handle && !strcmp(key, "magnet_rise"), "Magnet Rise config identity");
    return magnet_rise_setting;
}
static int config_changed(MtMod *mod, MtConfigFn fn, void *user) {
    CHECK(mod == mod_handle, "config subscription mod identity");
    changed = fn; changed_user = user;
    return MT_OK;
}
static int subscribe(MtMod *mod, const char *name, int priority, MtEventFn fn, void *user) {
    (void)priority;
    CHECK(mod == mod_handle, "event mod identity");
    CHECK(nevents < sizeof events / sizeof events[0], "event count bounded");
    snprintf(events[nevents].name, sizeof events[nevents].name, "%s", name);
    events[nevents].fn = fn; events[nevents++].user = user;
    return MT_OK;
}
static void log_line(MtMod *mod, int level, const char *line) {
    CHECK(mod == mod_handle, "log mod identity");
    (void)level;
    printf("DLL: %s\n", line);
}
static uint64_t module_base(const char *name) {
    return !strcmp(name, "main") ? UINT64_C(0x7100000000) : 0;
}
static uint64_t resolve(MtMod *mod, const char *name) {
    CHECK(mod == mod_handle, "resolve mod identity");
    if (!strncmp(name, "main+", 5)) return module_base("main") + strtoull(name + 5, NULL, 0);
    return 0;
}
static int call_guest(uint64_t addr, MtCall *call) {
    ++guest_calls;
    if (addr == table_function) {
        for (unsigned i = 0; i < ntables; ++i) if (call->x[0] == tables[i].table) {
            CHECK(call->x[1] == 0, "table lookup calling convention");
            call->x[0] = tables[i].rows; return MT_OK;
        }
        CHECK(0, "table lookup object belongs to a fixture");
    }
    if (addr == row_function) {
        for (unsigned i = 0; i < ntables; ++i) if (call->x[0] == tables[i].rows) {
            CHECK(call->x[1] < tables[i].row_count, "table row lookup ID bounded");
            call->x[0] = fixture_wrapper(&tables[i], (unsigned)call->x[1]); return MT_OK;
        }
        CHECK(0, "row lookup object belongs to a fixture");
    }
    return MT_ERR_STATE;
}
static uint64_t frame(void) { return 0; }

static Hook *find_hook(const char *name) {
    for (unsigned i = 0; i < nhooks; ++i) if (!strcmp(hooks[i].name, name)) return &hooks[i];
    fprintf(stderr, "Missing hook %s\n", name); exit(1);
}
static void settings(double sprint, double craft) {
    sprint_setting = sprint; craft_setting = craft;
    CHECK(changed != NULL, "settings callback registered");
    changed(mod_handle, "sprint", changed_user);
}
static void wait_settings(double wait) {
    wait_setting = wait;
    CHECK(changed != NULL, "waiting-time settings callback registered");
    changed(mod_handle, "wait", changed_user);
}
static void run_settings(double run) {
    run_setting = run;
    CHECK(changed != NULL, "stick-pushed speed settings callback registered");
    changed(mod_handle, "run", changed_user);
}
static void set_speed(MtHookCtx *ctx, float speed) {
    memset(ctx->v[8], 0xA5, 16); memcpy(ctx->v[8], &speed, sizeof speed);
}
static float get_speed(MtHookCtx *ctx) { float speed; memcpy(&speed, ctx->v[8], sizeof speed); return speed; }
static void test_sprint(void) {
    Hook *h = find_hook("sprint_speed");
    MtHookCtx ctx = {0};
    uint64_t actor = memory_base + 0x100, character = memory_base + 0x300;
    uint8_t flag = 1;
    put(actor + 8, &character, sizeof character); put(character + 0x10, &flag, sizeof flag);
    ctx.x[20] = actor;
    set_speed(&ctx, 4.0f);
    CHECK(h->fn(&ctx, h->user) == MT_CONTINUE, "sprint resumes native function");
    CHECK(fabsf(get_speed(&ctx) - 6.0f) < 0.00001f, "default sprint 1.5x");
    for (unsigned i = 4; i < 16; ++i) CHECK(ctx.v[8][i] == 0xA5, "sprint preserves other vector bytes");
    settings(2.0, 3); set_speed(&ctx, 4.0f); h->fn(&ctx, h->user);
    CHECK(get_speed(&ctx) == 8.0f, "live sprint setting 2x");
    settings(1.0, 3); set_speed(&ctx, 4.0f); h->fn(&ctx, h->user);
    CHECK(get_speed(&ctx) == 4.0f, "sprint 1x restores native value");
    settings(1.5, 3);
    flag = 0; put(character + 0x10, &flag, sizeof flag); set_speed(&ctx, 4.0f); h->fn(&ctx, h->user);
    CHECK(get_speed(&ctx) == 4.0f, "nonplayer movement stays native");
    ctx.x[20] = 0; set_speed(&ctx, 4.0f); h->fn(&ctx, h->user);
    CHECK(get_speed(&ctx) == 4.0f, "null actor stays native");
    ctx.x[20] = 0xDEADBEEF; set_speed(&ctx, 4.0f); h->fn(&ctx, h->user);
    CHECK(get_speed(&ctx) == 4.0f, "unreadable actor stays native");
}

static void movement_case(Hook *h, unsigned mode, uint8_t player, int target_reg, float native_speed, float expected_speed) {
    uint64_t actor = memory_base + 0x1000, character = memory_base + 0x1200;
    put(actor + 8, &character, sizeof character);
    put(character + 0x10, &player, sizeof player);
    MtHookCtx ctx = {0};
    for (unsigned i = 0; i < 36; ++i) ctx.x[i] = UINT64_C(0x23400000) + i;
    for (unsigned reg = 0; reg < 32; ++reg) memset(ctx.v[reg], (int)(0x80 + reg), 16);
    ctx.x[20] = actor; ctx.x[26] = mode;
    memcpy(ctx.v[target_reg], &native_speed, sizeof native_speed);
    MtHookCtx expected = ctx;
    memcpy(expected.v[target_reg], &expected_speed, sizeof expected_speed);
    unsigned previous_writes = writes, previous_calls = guest_calls;
    CHECK(h->fn(&ctx, h->user) == MT_CONTINUE, "movement handler continues the native branch");
    CHECK(!memcmp(&ctx, &expected, sizeof ctx), "movement changes only the intended low float and preserves walking, sprint and native copy registers");
    CHECK(writes == previous_writes && guest_calls == previous_calls, "movement scaling does not write game memory or call game functions");
}

static void test_run_speed(void) {
    Hook *run = find_hook("run_speed");
    Hook *sprint = find_hook("sprint_speed");
    movement_case(run, 2, 1, 0, 4.0f, 6.0f);
    run_settings(2.0);
    movement_case(run, 2, 1, 0, 4.0f, 8.0f);
    /* The ordinary speed factor must not also multiply stick-click sprint. */
    movement_case(sprint, 3, 1, 8, 4.0f, 6.0f);
    settings(2.0, 3);
    movement_case(run, 2, 1, 0, 4.0f, 8.0f);
    movement_case(sprint, 3, 1, 8, 4.0f, 8.0f);
    movement_case(run, 1, 1, 0, 4.0f, 4.0f);
    movement_case(run, 3, 1, 0, 4.0f, 4.0f);
    movement_case(run, 2, 0, 0, 4.0f, 4.0f);
    run_settings(1.0);
    movement_case(run, 2, 1, 0, 4.0f, 4.0f);
    settings(1.5, 3);
    run_settings(1.5);
}

static void emit(const char *name) {
    int found = 0;
    MtEvent ev = {0}; ev.name = name;
    for (unsigned i = 0; i < nevents; ++i) if (!strcmp(events[i].name, name)) {
        found = 1;
        CHECK(events[i].fn(mod_handle, &ev, events[i].user) == MT_EVENT_CONTINUE, "event continues");
    }
    CHECK(found, "expected event registered");
}

static TableFixture *add_table(unsigned slot, unsigned count, unsigned wrappers_offset, unsigned records_offset, unsigned stride) {
    CHECK(ntables < sizeof tables / sizeof tables[0], "fixture table count bounded");
    TableFixture *t = &tables[ntables];
    t->table = memory_base + 0x6000 + ntables * 0x400;
    t->rows = t->table + 0x80;
    t->wrappers = memory_base + wrappers_offset;
    t->records = memory_base + records_offset;
    t->row_count = count; t->stride = stride;
    uint64_t table_vtable = t->table + 0x100, rows_vtable = t->table + 0x180;
    put(manager_address + 0x10 + slot * 8, &t->table, sizeof t->table);
    put(t->table, &table_vtable, sizeof table_vtable);
    put(table_vtable + 0x38, &table_function, sizeof table_function);
    put(t->rows, &rows_vtable, sizeof rows_vtable);
    put(rows_vtable + 0x30, &row_function, sizeof row_function);
    for (unsigned id = 0; id < count; ++id) {
        uint64_t record = fixture_record(t, id);
        put(fixture_wrapper(t, id) + 8, &record, sizeof record);
    }
    ++ntables;
    return t;
}

static void recipe_fixture(void) {
    uint32_t count = 103;
    manager_pointer = manager_address;
    put(manager_address + 0x2b70, &count, sizeof count);
    add_table(0x66, 1500, 0x10000, 0x20000, 64);
    for (unsigned id = 0; id < 1500; ++id) {
        uint16_t item = (uint16_t)(2000 + id);
        /* Native processing recipes 1176..1213 take 120 seconds, except
           recipe 1212 takes 60. Other recipes are instant (duration zero). */
        uint16_t seconds = id >= 1176 && id <= 1213 ? (id == 1212 ? 60 : 120) : 0;
        memset(original_records[id], 0x5A, sizeof original_records[id]);
        memcpy(original_records[id], &seconds, sizeof seconds);
        memcpy(original_records[id] + 0x12, &item, sizeof item);
        original_records[id][0x21] = 7;
    }
    /* Result IDs captured from native recipe rows 1176..1213. Keep these
       independent of the mod's timing baseline header to exercise validation. */
    static const uint16_t processing_results[38] = {
        407, 105, 107, 114, 115, 116, 108, 118, 221, 222,
        222, 223, 224, 225, 225, 226, 227, 227, 227, 227,
        228, 228, 228, 228, 120, 108, 108, 108, 108, 108,
        119, 126, 108, 108, 108, 121, 435, 521
    };
    for (unsigned i = 0; i < 38; ++i)
        memcpy(original_records[1176 + i] + 0x12, &processing_results[i], sizeof processing_results[i]);
    for (size_t i = 0; i < BUILDER_BOOST_ELIGIBLE_RECIPE_COUNT; ++i) {
        const BuilderBoostRecipe *r = &kBuilderBoostEligibleRecipes[i];
        CHECK(r->recipe_id < 1500, "eligible recipe in installed table");
        CHECK(builder_boost_item_allowed(r->item_id), "eligible recipe item in item list");
        memcpy(original_records[r->recipe_id] + 0x12, &r->item_id, sizeof r->item_id);
        original_records[r->recipe_id][0x21] = r->base_count;
        eligible_recipe[r->recipe_id] = 1;
    }
    for (size_t i = 0; i < sizeof craft_cases / sizeof craft_cases[0]; ++i) {
        const CraftCase *c = &craft_cases[i];
        unsigned id = c->recipe_id;
        memcpy(original_records[id] + 0x12, &c->item_id, sizeof c->item_id);
        original_records[id][0x21] = c->base_count;
        memset(original_records[id] + 4, 0, 12);
        memcpy(original_records[id] + 4, c->ingredient_ids, sizeof c->ingredient_ids);
        memset(original_records[id] + 0x1a, 0, 6);
        memcpy(original_records[id] + 0x1a, c->ingredient_counts, sizeof c->ingredient_counts);
    }
    for (unsigned id = 0; id < 1500; ++id)
        put(record_address(id), original_records[id], sizeof original_records[id]);
}

static void construction_fixture(void) {
    /* Native archive52/runtime slot52: a profile's first float is hours.
       Row0 is instant; every other row defaults to24h except these profiles. */
    static const unsigned quarter_hour_profiles[] = {10, 11, 12, 13, 17, 21, 23, 27};
    static const unsigned one_hour_profiles[] = {14, 18, 24, 28, 35, 36, 37, 51, 52};
    construction_table = add_table(0x34, 128, 0x40000, 0x42000, 40);
    for (unsigned id = 0; id < 128; ++id) {
        float hours = id ? 24.0f : 0.0f;
        for (size_t i = 0; i < sizeof quarter_hour_profiles / sizeof quarter_hour_profiles[0]; ++i)
            if (id == quarter_hour_profiles[i]) hours = 0.25f;
        for (size_t i = 0; i < sizeof one_hour_profiles / sizeof one_hour_profiles[0]; ++i)
            if (id == one_hour_profiles[i]) hours = 1.0f;
        memset(original_construction[id], 0x6B, sizeof original_construction[id]);
        memcpy(original_construction[id], &hours, sizeof hours);
        put(fixture_record(construction_table, id), original_construction[id], sizeof original_construction[id]);
    }
}

static void verify_construction(void) {
    uint8_t row[40];
    for (unsigned id = 0; id < 128; ++id) {
        CHECK(mem_read(fixture_record(construction_table, id), row, sizeof row) == MT_OK, "construction profile readable");
        CHECK(!memcmp(row, original_construction[id], sizeof row), "construction profiles remain unchanged; duration adjustment uses final-seconds hook");
    }
}

static void verify_rail_recipes(unsigned multiplier) {
    uint8_t row[44];
    CHECK(mem_read(record_address(285), row, sizeof row) == MT_OK, "railway track recipe readable");
    CHECK(row[0x21] == 10 * multiplier, "railway track yields 30 at 3x and native 10 at 1x, independent of allowlist-derived expectations");
    uint16_t ingredient_a, ingredient_b, duration;
    memcpy(&ingredient_a, row + 4, sizeof ingredient_a);
    memcpy(&ingredient_b, row + 6, sizeof ingredient_b);
    memcpy(&duration, row, sizeof duration);
    CHECK(ingredient_a == 115 && ingredient_b == 404 && row[0x1a] == 1 && row[0x1b] == 1,
          "railway track still consumes one iron ingot and one stone");
    CHECK(duration == 0, "railway track remains an instant recipe");
    CHECK(mem_read(record_address(124), row, sizeof row) == MT_OK, "gold handrail recipe readable");
    memcpy(&ingredient_b, row + 6, sizeof ingredient_b);
    CHECK(row[0x21] == 10 && ingredient_b == 116 && row[0x1b] == 2,
          "gold handrail output and two-gold-ingot cost remain native");
    CHECK(mem_read(record_address(125), row, sizeof row) == MT_OK, "boat railing recipe readable");
    CHECK(row[0x21] == 10 * multiplier, "common boat railing remains boosted");
    CHECK(mem_read(record_address(403), row, sizeof row) == MT_OK, "wooden handrail recipe readable");
    CHECK(row[0x21] == 10 * multiplier, "common wooden handrail remains boosted");
}

static void verify_independent_craft_cases(unsigned multiplier, double wait) {
    uint8_t row[44], expected[44];
    for (size_t i = 0; i < sizeof craft_cases / sizeof craft_cases[0]; ++i) {
        const CraftCase *c = &craft_cases[i];
        CHECK(mem_read(record_address(c->recipe_id), row, sizeof row) == MT_OK, "independent crafting case readable");
        uint8_t expected_count = (uint8_t)(c->base_count * (c->boosted ? multiplier : 1));
        if (row[0x21] != expected_count)
            fprintf(stderr, "Crafting case %s (recipe %u): expected %u outputs, got %u\n",
                    c->name, c->recipe_id, expected_count, row[0x21]);
        CHECK(row[0x21] == expected_count, "audited common outputs boosted and rare, special, paint, furniture and devices unchanged");
        memcpy(expected, original_records[c->recipe_id], sizeof expected);
        expected[0x21] = expected_count;
        uint16_t native_duration;
        memcpy(&native_duration, expected, sizeof native_duration);
        if (native_duration) {
            uint16_t duration = (uint16_t)(native_duration * wait + 0.5);
            memcpy(expected, &duration, sizeof duration);
        }
        CHECK(!memcmp(row, expected, sizeof row), "audited case preserves exact ingredient costs and every field except intended output/duration");
    }
}

static void verify_tables(unsigned multiplier, double wait) {
    uint8_t row[44], expected[44];
    for (unsigned id = 0; id < 1500; ++id) {
        CHECK(mem_read(record_address(id), row, sizeof row) == MT_OK, "recipe readable");
        memcpy(expected, original_records[id], sizeof expected);
        if (eligible_recipe[id]) expected[0x21] = (uint8_t)(expected[0x21] * multiplier);
        if (id >= 1176 && id <= 1213) {
            uint16_t seconds = (uint16_t)((id == 1212 ? 60 : 120) * wait + 0.5);
            memcpy(expected, &seconds, sizeof seconds);
        }
        CHECK(!memcmp(row, expected, sizeof row), "only selected output quantities and processing durations change; all ingredients/other bytes preserved");
    }
    verify_construction();
    verify_rail_recipes(multiplier);
    verify_independent_craft_cases(multiplier, wait);
}

static void construction_case(Hook *h, float seconds, float expected_seconds) {
    MtHookCtx ctx = {0};
    for (unsigned i = 0; i < 36; ++i) ctx.x[i] = UINT64_C(0x12300000) + i;
    memset(ctx.v, 0x74, sizeof ctx.v);
    set_speed(&ctx, seconds);
    MtHookCtx expected = ctx;
    memcpy(expected.v[8], &expected_seconds, sizeof expected_seconds);
    CHECK(h->fn(&ctx, h->user) == MT_CONTINUE, "construction returns through native helper");
    CHECK(!memcmp(&ctx, &expected, sizeof ctx), "construction scales final seconds only and preserves all other register bytes");
}

static void test_construction_wait(void) {
    Hook *h = find_hook("construction_wait");
    wait_settings(0.5);
    construction_case(h, 900.0f, 450.0f);
    construction_case(h, 3600.0f, 1800.0f);
    construction_case(h, 86400.0f, 43200.0f);
    construction_case(h, 0.0f, 0.0f);
    construction_case(h, -10.0f, -10.0f);
    wait_settings(1.0);
    construction_case(h, 900.0f, 900.0f);
    construction_case(h, 86400.0f, 86400.0f);
    wait_settings(0.1);
    construction_case(h, 900.0f, 90.0f);
    construction_case(h, 3600.0f, 360.0f);
    wait_settings(0.5);
    /* Each real helper call computes fresh native seconds before this stop. */
    construction_case(h, 900.0f, 450.0f);
    construction_case(h, 900.0f, 450.0f);
}

static void spawn_case(Hook *h, int threshold_reg, float seconds, float expected_seconds) {
    MtHookCtx ctx = {0};
    for (unsigned i = 0; i < 36; ++i) ctx.x[i] = UINT64_C(0xC0FFEE0000000000) + i;
    for (unsigned reg = 0; reg < 32; ++reg) memset(ctx.v[reg], (int)(0x60 + reg), 16);
    float elapsed = 12345.25f;
    memcpy(ctx.v[0], &elapsed, sizeof elapsed);
    memcpy(ctx.v[threshold_reg], &seconds, sizeof seconds);
    MtHookCtx expected = ctx;
    memcpy(expected.v[threshold_reg], &expected_seconds, sizeof expected_seconds);
    unsigned previous_writes = writes, previous_calls = guest_calls;
    CHECK(h->fn(&ctx, h->user) == MT_CONTINUE, "Pokemon timing hook resumes the native comparison/callee");
    CHECK(!memcmp(&ctx, &expected, sizeof ctx), "Pokemon threshold changes only its low float; elapsed time, RNG arguments and all other registers preserved");
    CHECK(writes == previous_writes, "Pokemon timing hooks do not write guest memory or population caps");
    CHECK(guest_calls == previous_calls, "Pokemon timing hooks leave guest calls and random selection to native code");
}

static void test_pokemon_appearance(void) {
    Hook *check = find_hook("pokemon_spawn_check");
    Hook *wait = find_hook("pokemon_spawn_wait");
    /* The cadence comparison uses v1; the conditional delay uses v9.
       Both retain v0, which already holds the native elapsed duration. */
    wait_settings(0.5);
    spawn_case(check, 1, 180.0f, 90.0f);
    spawn_case(wait, 9, 600.0f, 300.0f);
    spawn_case(check, 1, 0.0f, 0.0f);
    spawn_case(wait, 9, 0.0f, 0.0f);
    spawn_case(check, 1, -5.0f, -5.0f);
    spawn_case(wait, 9, -5.0f, -5.0f);
    wait_settings(1.0);
    spawn_case(check, 1, 180.0f, 180.0f);
    spawn_case(wait, 9, 600.0f, 600.0f);
    wait_settings(0.1);
    spawn_case(check, 1, 180.0f, 18.0f);
    spawn_case(wait, 9, 600.0f, 60.0f);
    wait_settings(0.5);
    /* A fresh native threshold is supplied at each stop; factors never persist. */
    spawn_case(check, 1, 180.0f, 90.0f);
    spawn_case(check, 1, 180.0f, 90.0f);
    spawn_case(wait, 9, 600.0f, 300.0f);
    spawn_case(wait, 9, 600.0f, 300.0f);
}

static void test_crafting(void) {
    recipe_fixture();
    construction_fixture();
    CHECK(!eligible_recipe[1181] && !eligible_recipe[322] && !eligible_recipe[1200], "gold/crystal/Pokemetal recipes excluded");
    CHECK(eligible_recipe[4] && eligible_recipe[38] && eligible_recipe[1176] && eligible_recipe[1177], "wood wall/road/lumber/brick recipes included");
    writes = 0; guest_calls = 0;
    emit("FrameStart");
    CHECK(writes == 0 && guest_calls == 0, "table untouched before SaveLoaded");
    uint64_t saved_manager = manager_pointer;
    manager_pointer = 0;
    emit("SaveLoaded");
    CHECK(writes == 0, "unavailable table deferred");
    manager_pointer = saved_manager;
    emit("FrameStart");
    CHECK(writes > 0, "common recipe and waiting-time patches applied");
    verify_tables(3, 0.5);
    unsigned previous_writes = writes, previous_calls = guest_calls;
    emit("FrameStart");
    CHECK(writes == previous_writes && guest_calls == previous_calls, "steady frames perform no table work");
    emit("SaveLoaded"); verify_tables(3, 0.5);
    settings(1.5, 5); emit("FrameStart"); verify_tables(5, 0.5);
    settings(1.5, 1); emit("FrameStart"); verify_tables(1, 0.5);
    settings(1.5, 3); emit("FrameStart"); verify_tables(3, 0.5);
    emit("SaveLoaded"); verify_tables(3, 0.5);
    wait_settings(1); emit("FrameStart"); verify_tables(3, 1);
    wait_settings(0.1); emit("FrameStart"); verify_tables(3, 0.1);
    wait_settings(0.5); emit("FrameStart"); verify_tables(3, 0.5);
    emit("SaveLoaded"); verify_tables(3, 0.5);
}

static void test_collection(void) {
    Hook *drop = find_hook("collection_drops");
    Hook *harvest = find_hook("collection_harvest");
    Hook *ripe_produce = find_hook("collection_ripe_produce");
    MtHookCtx ctx = {0};
    ctx.x[MT_REG_SP] = memory_base + 0x800;
    ctx.x[19] = memory_base + 0x900;
    ctx.x[20] = memory_base + 0xa00;
    const uint32_t count = 4;
    uint64_t caller = module_base("main") + 0x22004c;
    put(ctx.x[MT_REG_SP] + 8, &caller, sizeof caller);
    put(ctx.x[19], &count, sizeof count);
    const uint32_t items[] = {0x80000000u | 106, 123, 8, 55};
    const uint8_t native[] = {2, 1, 1, 1};
    const uint8_t expected[] = {6, 1, 3, 1};
    for (unsigned i = 0; i < count; ++i) {
        put(ctx.x[20] + 16 * i + 8, &items[i], sizeof items[i]);
        put(ctx.x[20] + 16 * i + 12, &native[i], 1);
    }
    CHECK(drop->fn(&ctx, drop->user) == MT_CONTINUE, "drop roll resumes");
    for (unsigned i = 0; i < count; ++i) {
        uint8_t qty;
        uint32_t item;
        CHECK(mem_read(ctx.x[20] + 16 * i + 8, &item, sizeof item) == MT_OK && item == items[i],
              "collection preserves packed item identity");
        CHECK(mem_read(ctx.x[20] + 16 * i + 12, &qty, 1) == MT_OK && qty == expected[i],
              "ore and produce scale while rare fragments and seeds retain their yields");
    }
    caller = module_base("main") + 0x7a4cbc;
    put(ctx.x[MT_REG_SP] + 8, &caller, sizeof caller);
    unsigned prior = writes;
    drop->fn(&ctx, drop->user);
    CHECK(writes == prior, "crop preparation leaves cached rolls unscaled");
    uint32_t tomato = 8;
    uint8_t qty = 1;
    put(ctx.x[MT_REG_SP] + 0x48, &tomato, sizeof tomato);
    put(ctx.x[MT_REG_SP] + 0x4c, &qty, 1);
    CHECK(harvest->fn(&ctx, harvest->user) == MT_CONTINUE, "harvest resumes");
    CHECK(mem_read(ctx.x[MT_REG_SP] + 0x4c, &qty, 1) == MT_OK && qty == 3,
          "pulling reward scales exactly once");
    ctx.x[11] = 8;
    ctx.x[10] = 1;
    CHECK(ripe_produce->fn(&ctx, ripe_produce->user) == MT_CONTINUE &&
          ctx.x[10] == 3 && ctx.x[11] == 8, "direct tomato harvest stores three instead of one");
    ctx.x[11] = 55;
    ctx.x[10] = 1;
    ripe_produce->fn(&ctx, ripe_produce->user);
    CHECK(ctx.x[10] == 1, "direct harvest leaves excluded tomato seeds at one");
    collection_setting = 1;
    changed(mod_handle, "collection", changed_user);
    prior = writes;
    harvest->fn(&ctx, harvest->user);
    CHECK(writes == prior, "1x leaves native collection alone");
    ctx.x[11] = 8;
    ripe_produce->fn(&ctx, ripe_produce->user);
    CHECK(ctx.x[10] == 1, "1x restores native direct tomato harvest");
    collection_setting = 3;
    changed(mod_handle, "collection", changed_user);
}

static void test_food_buff(void) {
    Hook *food = find_hook("food_buff");
    MtHookCtx ctx = {0};
    ctx.x[19] = memory_base + 0x1000;
    static const struct { uint32_t type, amount, stronger; } meals[] = {
        {1, 40, 0}, {3, 40, 0}, {4, 500, 1}, {5, 60, 0}, {10, 1000, 0},
    };
    for (unsigned i = 0; i < sizeof meals / sizeof *meals; ++i) {
        uint32_t effects[3][5] = {
            {meals[i].type, meals[i].amount, meals[i].stronger, 0, 3},
            {3000, 0, 0, 0, 3},
            {0, 0, 0, 0, 3},
        };
        uint32_t actual[3][5];
        put(ctx.x[19], effects, sizeof effects);
        CHECK(food->fn(&ctx, food->user) == MT_CONTINUE, "food effect processing resumes");
        effects[0][1] *= 2;
        CHECK(mem_read(ctx.x[19], actual, sizeof actual) == MT_OK &&
              !memcmp(actual, effects, sizeof effects), "double the meal reserve and preserve strength and PP effects");
    }

    uint32_t effects[3][5] = {{6, 40}, {10, 0}, {5, 501}};
    uint32_t actual[3][5];
    food_buff_setting = 1.5;
    changed(mod_handle, "food_buff", changed_user);
    put(ctx.x[19], effects, sizeof effects);
    food->fn(&ctx, food->user);
    effects[2][1] = 752;
    CHECK(mem_read(ctx.x[19], actual, sizeof actual) == MT_OK && !memcmp(actual, effects, sizeof effects),
          "fractional factor rounds the reserve and leaves zero and other effects alone");

    food_buff_setting = 5;
    changed(mod_handle, "food_buff", changed_user);
    effects[2][1] = 20000;
    put(ctx.x[19], effects, sizeof effects);
    food->fn(&ctx, food->user);
    effects[2][1] = UINT16_MAX;
    CHECK(mem_read(ctx.x[19], actual, sizeof actual) == MT_OK && !memcmp(actual, effects, sizeof effects),
          "the meal reserve fits the game's storage");

    food_buff_setting = 1;
    changed(mod_handle, "food_buff", changed_user);
    unsigned prior = writes;
    food->fn(&ctx, food->user);
    CHECK(writes == prior, "1x keeps the game-provided food effects");
}

static void test_magnet_rise(void) {
    Hook *list = find_hook("magnet_rise_list");
    Hook *wheel = find_hook("magnet_rise_wheel");
    Hook *wheel_slots = find_hook("magnet_rise_wheel_slots");
    Hook *select = find_hook("magnet_rise_select");
    Hook *drain = find_hook("magnet_rise_drain");
    Hook *action = find_hook("magnet_rise_action_pp");
    MtHookCtx ctx = {0};
    save_pointer = memory_base;
    uint64_t learned_address = save_pointer + 0x6d8358;
    uint8_t locked = 0x80;
    put(learned_address, &locked, sizeof locked);
    const uint64_t wheel_address = memory_base + 0x100;
    uint8_t slots[] = {1, 2, 0, 4, 5, 6, 7};
    put(wheel_address, slots, sizeof slots);
    ctx.x[21] = wheel_address;
    ctx.x[0] = 8;
    wheel_slots->fn(&ctx, wheel_slots->user);
    CHECK(mem_read(wheel_address, slots, sizeof slots) == MT_OK &&
          slots[2] == 13 && ctx.x[0] == 8, "the first empty menu slot gets Magnet Rise");
    unsigned writes_after_slot = writes;
    wheel_slots->fn(&ctx, wheel_slots->user);
    CHECK(writes == writes_after_slot && ctx.x[0] == 8, "an existing wheel slot is not duplicated");
    slots[2] = 3;
    put(wheel_address, slots, sizeof slots);
    wheel_slots->fn(&ctx, wheel_slots->user);
    CHECK(ctx.x[0] == 13, "a full menu uses the last slot");
    ctx.x[0] = 0;
    wheel_slots->fn(&ctx, wheel_slots->user);
    CHECK(ctx.x[0] == 13, "an empty last menu slot also gets Magnet Rise");
    unsigned prior = writes;

    ctx.x[21] = 12;
    ctx.x[8] = 0x80;
    list->fn(&ctx, list->user);
    CHECK(ctx.x[8] == 0x80, "early unlock is exclusive to Magnet Rise");
    ctx.x[21] = 13;
    CHECK(list->fn(&ctx, list->user) == MT_CONTINUE && ctx.x[8] == 0x81,
          "Magnet Rise appears in the assignment list");
    ctx.x[0] = 13;
    wheel->fn(&ctx, wheel->user);
    ctx.x[1] = 13;
    CHECK(ctx.x[0] == 13 && select->fn(&ctx, select->user) == MT_CONTINUE,
          "an assigned Magnet Rise can be selected with early unlock enabled");

    magnet_rise_setting = 0;
    changed(mod_handle, "magnet_rise", changed_user);
    ctx.x[21] = wheel_address;
    ctx.x[0] = 8;
    wheel_slots->fn(&ctx, wheel_slots->user);
    CHECK(ctx.x[0] == 8, "the off switch restores the regular last wheel slot");
    ctx.x[21] = 13;
    ctx.x[0] = 13;
    ctx.x[8] = 0x80;
    list->fn(&ctx, list->user);
    CHECK(ctx.x[8] == 0x80, "switching off hides unlearned Magnet Rise from the list");
    wheel->fn(&ctx, wheel->user);
    CHECK(ctx.x[0] == 0 && select->fn(&ctx, select->user) == MT_HANDLED,
          "switching off hides and blocks a previously assigned unlearned move");
    CHECK(writes == prior, "early access leaves the game's save data untouched");

    uint8_t learned = 0x81;
    put(learned_address, &learned, sizeof learned);
    ctx.x[0] = 13;
    wheel->fn(&ctx, wheel->user);
    CHECK(ctx.x[0] == 13 && select->fn(&ctx, select->user) == MT_CONTINUE,
          "Magnet Rise learned normally remains usable with early unlock off");

    ctx.x[20] = 13;
    set_speed(&ctx, 0.25f);
    drain->fn(&ctx, drain->user);
    CHECK(get_speed(&ctx) == 0.125f, "Magnet Rise drains at half the rate independently of the unlock");
    ctx.x[8] = 12;
    action->fn(&ctx, action->user);
    CHECK(ctx.x[8] == 6, "Magnet Rise action cost halves");
    ctx.x[8] = 1;
    action->fn(&ctx, action->user);
    CHECK(ctx.x[8] == 1, "a nonzero action charge keeps its minimum unit");

    ctx.x[20] = 12;
    set_speed(&ctx, 0.25f);
    ctx.x[8] = 16;
    drain->fn(&ctx, drain->user);
    action->fn(&ctx, action->user);
    CHECK(get_speed(&ctx) == 0.25f && ctx.x[8] == 16, "other moves use their native PP paths");
    magnet_rise_pp_setting = 1.0;
    changed(mod_handle, "magnet_rise_pp", changed_user);
    ctx.x[20] = 13;
    drain->fn(&ctx, drain->user);
    action->fn(&ctx, action->user);
    CHECK(get_speed(&ctx) == 0.25f && ctx.x[8] == 16, "1x restores native Magnet Rise drain and action cost");
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: test_host path/to/mod.dll\n"); return 2; }
    HMODULE dll = LoadLibraryA(argv[1]);
    CHECK(dll != NULL, "DLL loads");
    const uint32_t *api = (const uint32_t *)(uintptr_t)GetProcAddress(dll, "modtopia_api_version");
    CHECK(api != NULL && *api == MT_API_VERSION, "DLL SDK version matches host");
    MtModInitFn init = (MtModInitFn)(uintptr_t)GetProcAddress(dll, "mod_init");
    CHECK(init != NULL, "DLL exports mod_init");
    MtHost host = {0};
    host.size = sizeof host; host.api_version = MT_API_VERSION;
    host.host_name = "Native ABI test"; host.game_version = "2.0.1";
    host.log = log_line; host.hook = hook; host.reg_get = reg_get; host.reg_set = reg_set;
    host.vreg_get = vreg_get; host.vreg_set = vreg_set; host.mem_read = mem_read; host.mem_write = mem_write;
    host.module_base = module_base; host.resolve = resolve; host.call_guest = call_guest;
    host.config_number = config_number; host.on_config_changed = config_changed;
    host.config_bool = config_bool;
    host.subscribe = subscribe; host.frame = frame;
    CHECK(init(&host, mod_handle) == MT_OK, "DLL initializes with official ABI");
    CHECK(nhooks == 15, "movement, waits, collection, food and Magnet Rise hooks registered");
    test_sprint();
    test_run_speed();
    test_crafting();
    test_construction_wait();
    test_pokemon_appearance();
    test_collection();
    test_food_buff();
    test_magnet_rise();
    printf("PASS: %u checks using actual DLL\n", checks);
    FreeLibrary(dll);
    return 0;
}
