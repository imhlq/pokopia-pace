/* SPDX-License-Identifier: MIT */
/* Verified processing duration baselines; research/waits_data.md. */
#ifndef BUILDER_BOOST_WAITS_DATA_H
#define BUILDER_BOOST_WAITS_DATA_H
#include <stdint.h>
typedef struct BuilderBoostProcessingWait { uint16_t recipe_id, item_id, base_seconds; } BuilderBoostProcessingWait;
static const BuilderBoostProcessingWait kBuilderBoostProcessingWaits[] = {
    {1176, 407, 120}, /* lumber */
    {1177, 105, 120}, /* brick */
    {1178, 107, 120}, /* iron ore */
    {1179, 114, 120}, /* copper ingot */
    {1180, 115, 120}, /* iron ingot */
    {1181, 116, 120}, /* gold ingot */
    {1182, 108, 120}, /* glass */
    {1183, 118, 120}, /* concrete */
    {1184, 221, 120}, /* red paint */
    {1185, 222, 120}, /* blue paint */
    {1186, 222, 120}, /* blue paint */
    {1187, 223, 120}, /* green paint */
    {1188, 224, 120}, /* yellow paint */
    {1189, 225, 120}, /* pink paint */
    {1190, 225, 120}, /* pink paint */
    {1191, 226, 120}, /* cyan paint */
    {1192, 227, 120}, /* white paint */
    {1193, 227, 120}, /* white paint */
    {1194, 227, 120}, /* white paint */
    {1195, 227, 120}, /* white paint */
    {1196, 228, 120}, /* black paint */
    {1197, 228, 120}, /* black paint */
    {1198, 228, 120}, /* black paint */
    {1199, 228, 120}, /* black paint */
    {1200, 120, 120}, /* Pokémetal */
    {1201, 108, 120}, /* glass */
    {1202, 108, 120}, /* glass */
    {1203, 108, 120}, /* glass */
    {1204, 108, 120}, /* glass */
    {1205, 108, 120}, /* glass */
    {1206, 119, 120}, /* Tinkagear */
    {1207, 126, 120}, /* paper */
    {1208, 108, 120}, /* glass */
    {1209, 108, 120}, /* glass */
    {1210, 108, 120}, /* glass */
    {1211, 121, 120}, /* rare Pokémetal */
    {1212, 435, 60}, /* shiny pearl */
    {1213, 521, 120}, /* watermelon slice */
};
#define BUILDER_BOOST_PROCESSING_WAIT_COUNT (sizeof(kBuilderBoostProcessingWaits) / sizeof(kBuilderBoostProcessingWaits[0]))
#endif
