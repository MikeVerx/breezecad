#include "document/StandardParts.h"

#include "core/Expression.h"
#include "document/ExtrudeFeature.h"
#include "document/Origin.h"
#include "document/RevolveFeature.h"
#include "document/SketchFeature.h"
#include "document/StandardPacks.h"
#include "document/ThreadFeature.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <format>
#include <optional>

namespace bz::doc::parts {
namespace {

// ===========================================================================
// Size tables. Values in mm. Typed from the standards' tables; geometry
// details that the tables leave to the drawing (tip chamfer, ring width)
// are derived in the builders below.
// ===========================================================================

/// ISO 261 coarse pitch.
double Pitch(double d) {
    static constexpr std::array<std::pair<double, double>, 10> kPitch{
        {{3, 0.5}, {4, 0.7}, {5, 0.8}, {6, 1.0}, {8, 1.25}, {10, 1.5}, {12, 1.75}, {16, 2.0}, {20, 2.5}, {24, 3.0}}};
    for (const auto& [size, pitch] : kPitch)
        if (std::abs(size - d) < 1e-9) return pitch;
    return CoarsePitch(d);
}

/// Preferred lengths of screws and bolts.
constexpr std::array<double, 33> kIsoLengths{6,  8,  10, 12, 16,  20,  25,  30,  35,  40,  45,  50,  55,  60,  65,  70, 80,
                                             90, 100, 110, 120, 130, 140, 150, 160, 180, 200, 220, 240, 260, 280, 300, 5};
constexpr std::array<double, 38> kGostLengths{8,  10, 12, 14,  16,  20,  22,  25,  28,  30,  32,  35,  38,  40,  45,  50,  55, 60,  65,
                                              70, 75, 80, 85,  90,  95,  100, 110, 120, 130, 140, 150, 160, 180, 200, 220, 240, 260, 5};

struct BoltRow {   // hexagon head: across flats, head height, lengths
    double d, s, k, lMin, lMax;
};
struct SocketRow {   // cylinder or countersunk head with a hexagon socket
    double d, dk, k, s, t, b, lMin, lMax;
};
struct SlotRow {   // slotted / cross recess head
    double d, dk, k, n, t, lMin, lMax;
};
struct NutRow {
    double d, s, m, h, dw;   // h, dw: lock nut collar (0 for a plain nut)
};
struct WasherRow {
    double d, inner, outer, h;
};
struct BearingRow {
    int    number;   // 6204
    double d, D, B;
};
struct RingRow {
    double d1, s, d3;   // nominal (shaft or bore), thickness, free diameter
};

// --- hexagon head bolts ---------------------------------------------------
constexpr BoltRow kIso4014[] = {{3, 5.5, 2, 20, 30},   {4, 7, 2.8, 25, 40},  {5, 8, 3.5, 25, 50},     {6, 10, 4, 30, 60},
                                {8, 13, 5.3, 35, 80},  {10, 16, 6.4, 40, 100}, {12, 18, 7.5, 45, 120}, {16, 24, 10, 55, 160},
                                {20, 30, 12.5, 65, 200}, {24, 36, 15, 80, 240}};
constexpr BoltRow kIso4017[] = {{3, 5.5, 2, 6, 30},    {4, 7, 2.8, 8, 40},    {5, 8, 3.5, 10, 50},    {6, 10, 4, 12, 60},
                                {8, 13, 5.3, 16, 80},  {10, 16, 6.4, 20, 100}, {12, 18, 7.5, 25, 120}, {16, 24, 10, 30, 150},
                                {20, 30, 12.5, 40, 150}, {24, 36, 15, 50, 150}};
constexpr BoltRow kGost7798[] = {{6, 10, 4, 10, 90},     {8, 13, 5.5, 12, 100},  {10, 17, 7, 16, 150}, {12, 19, 8, 20, 200},
                                 {16, 24, 10, 25, 200},  {20, 30, 13, 30, 200},  {24, 36, 15, 40, 200}};
constexpr BoltRow kGost7805[] = {{3, 5.5, 2, 6, 30},     {4, 7, 2.8, 8, 40},     {5, 8, 3.5, 10, 50},    {6, 10, 4, 10, 90},
                                 {8, 13, 5.3, 12, 100},  {10, 17, 6.4, 16, 150}, {12, 19, 7.5, 20, 200}, {16, 24, 10, 25, 200},
                                 {20, 30, 12.5, 30, 200}, {24, 36, 15, 40, 200}};

// --- socket head cap screws (k = d) ------------------------------------------
constexpr SocketRow kIso4762[] = {
    {3, 5.5, 3, 2.5, 1.3, 18, 5, 30},     {4, 7, 4, 3, 2, 20, 6, 40},       {5, 8.5, 5, 4, 2.5, 22, 8, 50},
    {6, 10, 6, 5, 3, 24, 10, 60},         {8, 13, 8, 6, 4, 28, 12, 80},     {10, 16, 10, 8, 5, 32, 16, 100},
    {12, 18, 12, 10, 6, 36, 20, 120},     {16, 24, 16, 14, 8, 44, 25, 160}, {20, 30, 20, 17, 10, 52, 30, 200},
    {24, 36, 24, 19, 12, 60, 40, 200}};

// --- countersunk, hexagon socket (head 90°) -------------------------------------
constexpr SocketRow kIso10642[] = {
    {3, 6.72, 1.86, 2, 1.1, 18, 8, 30},     {4, 8.96, 2.48, 2.5, 1.5, 20, 8, 40},  {5, 11.2, 3.1, 3, 1.9, 22, 8, 50},
    {6, 13.44, 3.72, 4, 2.2, 24, 8, 60},    {8, 17.92, 4.96, 5, 3, 28, 10, 80},    {10, 22.4, 6.2, 6, 3.6, 32, 12, 100},
    {12, 26.88, 7.44, 8, 4.3, 36, 20, 100}, {16, 33.6, 8.8, 10, 5.5, 44, 30, 100}, {20, 40.32, 10.16, 12, 6.6, 52, 35, 100}};

// --- slotted / cross recess heads ----------------------------------------------
constexpr SlotRow kIso2009[] = {{3, 5.5, 1.65, 0.8, 0.85, 5, 30}, {4, 8.4, 2.7, 1.2, 1.3, 6, 40}, {5, 9.3, 2.7, 1.2, 1.4, 8, 50},
                                {6, 11.3, 3.3, 1.6, 1.6, 8, 60},  {8, 15.8, 4.65, 2, 2.3, 10, 80}, {10, 18.3, 5, 2.5, 2.6, 12, 80}};
constexpr SlotRow kIso1580[] = {{3, 5.6, 1.8, 0.8, 0.7, 4, 30}, {4, 8, 2.4, 1.2, 1, 5, 40},     {5, 9.5, 3, 1.2, 1.2, 6, 50},
                                {6, 12, 3.6, 1.6, 1.4, 8, 60},  {8, 16, 4.8, 2, 1.9, 10, 80},   {10, 20, 6, 2.5, 2.4, 12, 80}};
constexpr SlotRow kIso7045[] = {{3, 6, 2.4, 0.9, 1.4, 4, 30},  {4, 8, 3.1, 1.1, 1.9, 5, 40},   {5, 10, 3.8, 1.3, 2.3, 6, 45},
                                {6, 12, 4.6, 1.6, 2.8, 8, 60}, {8, 16, 6, 2.1, 3.6, 10, 60},   {10, 20, 7.5, 2.6, 4.5, 12, 60}};
constexpr SlotRow kGost17473[] = {{3, 5.6, 2.1, 0.8, 1.2, 4, 30}, {4, 7.4, 2.8, 1.2, 1.6, 5, 40}, {5, 9.2, 3.5, 1.2, 2, 6, 50},
                                  {6, 11, 4.2, 1.6, 2.4, 8, 60},  {8, 14.5, 5.6, 2, 3.2, 10, 80}, {10, 18, 7, 2.5, 4, 12, 80}};

// --- nuts ----------------------------------------------------------------------
constexpr NutRow kIso4032[] = {{3, 5.5, 2.4, 0, 0},  {4, 7, 3.2, 0, 0},   {5, 8, 4.7, 0, 0},     {6, 10, 5.2, 0, 0},
                               {8, 13, 6.8, 0, 0},   {10, 16, 8.4, 0, 0}, {12, 18, 10.8, 0, 0},  {16, 24, 14.8, 0, 0},
                               {20, 30, 18, 0, 0},   {24, 36, 21.5, 0, 0}};
constexpr NutRow kGost5915[] = {{3, 5.5, 2.4, 0, 0}, {4, 7, 3.2, 0, 0},  {5, 8, 4, 0, 0},     {6, 10, 5, 0, 0},
                                {8, 13, 6.5, 0, 0},  {10, 17, 8, 0, 0},  {12, 19, 10, 0, 0},  {16, 24, 13, 0, 0},
                                {20, 30, 16, 0, 0},  {24, 36, 19, 0, 0}};
constexpr NutRow kIso7040[] = {{3, 5.5, 2.4, 4, 5},     {4, 7, 2.9, 5, 6.3},     {5, 8, 3.2, 5, 7.2},    {6, 10, 4, 6, 9},
                               {8, 13, 5.5, 8, 11.7},   {10, 16, 6.5, 10, 14.5}, {12, 18, 8, 12, 16.5},  {16, 24, 10.4, 16, 22},
                               {20, 30, 14.1, 20, 27.7}, {24, 36, 16.9, 24, 33.2}};

// --- washers ---------------------------------------------------------------------
constexpr WasherRow kIso7089[] = {{3, 3.2, 7, 0.5},   {4, 4.3, 9, 0.8},  {5, 5.3, 10, 1},   {6, 6.4, 12, 1.6},
                                  {8, 8.4, 16, 1.6},  {10, 10.5, 20, 2}, {12, 13, 24, 2.5}, {16, 17, 30, 3},
                                  {20, 21, 37, 3},    {24, 25, 44, 4}};
constexpr WasherRow kGost11371[] = {{3, 3.2, 7, 0.5},  {4, 4.3, 9, 0.8},  {5, 5.3, 10, 1},   {6, 6.4, 12.5, 1.6},
                                    {8, 8.4, 17, 1.6}, {10, 10.5, 21, 2}, {12, 13, 24, 2.5}, {16, 17, 30, 3},
                                    {20, 21, 37, 3},   {24, 25, 44, 4}};
constexpr WasherRow kGost6958[] = {{3, 3.2, 9, 0.8},  {4, 4.3, 12, 1},    {5, 5.3, 15, 1.2}, {6, 6.4, 18, 1.6},
                                   {8, 8.4, 24, 2},   {10, 10.5, 30, 2.5}, {12, 13, 37, 3},   {16, 17, 50, 3},
                                   {20, 21, 60, 4},   {24, 25, 72, 5}};
constexpr WasherRow kDin9021[] = {{3, 3.2, 9, 0.8},  {4, 4.3, 12, 1},     {5, 5.3, 15, 1.2}, {6, 6.4, 18, 1.6},
                                  {8, 8.4, 24, 2},   {10, 10.5, 30, 2.5}, {12, 13, 37, 3},   {16, 17, 50, 3},
                                  {20, 22, 60, 4},   {24, 26, 72, 5}};
// Spring washers: `h` is the section (square for ГОСТ 6402 Н: s = b).
constexpr WasherRow kGost6402[] = {{3, 3.1, 4.7, 0.8},  {4, 4.1, 6.1, 1},     {5, 5.1, 7.5, 1.2},   {6, 6.1, 8.9, 1.4},
                                   {8, 8.2, 12.2, 2},   {10, 10.2, 15.2, 2.5}, {12, 12.2, 18.2, 3}, {16, 16.3, 23.3, 3.5},
                                   {20, 20.5, 29.5, 4.5}, {24, 24.5, 35.5, 5.5}};
constexpr WasherRow kDin127[] = {{3, 3.1, 6.2, 0.8},   {4, 4.1, 7.6, 0.9},   {5, 5.1, 9.2, 1.2},   {6, 6.1, 11.8, 1.6},
                                 {8, 8.2, 14.8, 2},    {10, 10.2, 18.1, 2.2}, {12, 12.2, 21.1, 2.5}, {16, 16.2, 27.4, 3.5},
                                 {20, 20.2, 33.6, 4},  {24, 24.5, 40, 5}};

// --- deep groove ball bearings (series 60, 62, 63) ---------------------------------
constexpr BearingRow kBearings[] = {
    {6000, 10, 26, 8},  {6001, 12, 28, 8},  {6002, 15, 32, 9},   {6003, 17, 35, 10},  {6004, 20, 42, 12}, {6005, 25, 47, 12},
    {6006, 30, 55, 13}, {6007, 35, 62, 14}, {6008, 40, 68, 15},  {6009, 45, 75, 16},  {6010, 50, 80, 16},
    {6200, 10, 30, 9},  {6201, 12, 32, 10}, {6202, 15, 35, 11},  {6203, 17, 40, 12},  {6204, 20, 47, 14}, {6205, 25, 52, 15},
    {6206, 30, 62, 16}, {6207, 35, 72, 17}, {6208, 40, 80, 18},  {6209, 45, 85, 19},  {6210, 50, 90, 20},
    {6300, 10, 35, 11}, {6301, 12, 37, 12}, {6302, 15, 42, 13},  {6303, 17, 47, 14},  {6304, 20, 52, 15}, {6305, 25, 62, 17},
    {6306, 30, 72, 19}, {6307, 35, 80, 21}, {6308, 40, 90, 23},  {6309, 45, 100, 25}, {6310, 50, 110, 27}};

// --- retaining rings ------------------------------------------------------------------
constexpr RingRow kRingsExternal[] = {{10, 1, 9.3},    {12, 1, 11},     {15, 1, 13.8},   {16, 1, 14.7},  {17, 1, 15.7},
                                      {18, 1.2, 16.5}, {20, 1.2, 18.5}, {22, 1.2, 20.5}, {25, 1.2, 23.2}, {28, 1.5, 25.9},
                                      {30, 1.5, 27.9}, {32, 1.5, 29.6}, {35, 1.5, 32.2}, {40, 1.75, 36.5}, {45, 1.75, 41.5},
                                      {50, 2, 45.8},   {55, 2, 50.8},   {60, 2, 55.8}};
constexpr RingRow kRingsInternal[] = {{10, 1, 10.8},   {12, 1, 13},     {15, 1, 16.2},   {16, 1, 17.3},  {17, 1, 18.3},
                                      {18, 1, 19.5},   {20, 1, 21.5},   {22, 1, 23.5},   {25, 1.2, 26.9}, {28, 1.2, 30.1},
                                      {30, 1.2, 32.1}, {32, 1.2, 34.4}, {35, 1.5, 37.8}, {40, 1.75, 43.5}, {45, 1.75, 48.5},
                                      {50, 2, 54.2},   {55, 2, 59.2},   {60, 2, 64.2}};

// --- pins and cotter pins ------------------------------------------------------
struct PinRow {   // cylindrical: diameter; taper: the small end's diameter (taper 1:50)
    double d, lMin, lMax;
};
struct CotterRow {   // nominal = hole diameter; wire across both halves; eye outer diameter
    double d, wire, eye, lMin, lMax;
};
constexpr PinRow kPinsCylindrical[] = {{2, 6, 20},   {3, 8, 30},    {4, 8, 40},     {5, 10, 50},    {6, 12, 60},
                                       {8, 14, 80},  {10, 18, 100}, {12, 22, 120},  {16, 26, 160},  {20, 35, 200}};
constexpr PinRow kPinsTaper[] = {{2, 10, 35},  {3, 12, 45},   {4, 14, 55},   {5, 18, 60},   {6, 22, 90},
                                 {8, 22, 120}, {10, 26, 160}, {12, 32, 180}, {16, 40, 200}, {20, 45, 200}};
constexpr CotterRow kCotters[] = {{1.6, 1.4, 3.2, 8, 32},   {2, 1.8, 4, 10, 40},   {2.5, 2.3, 5, 12, 50},
                                  {3.2, 2.9, 6.4, 14, 63},  {4, 3.7, 8, 18, 80},   {5, 4.6, 10, 22, 100},
                                  {6.3, 5.9, 12.6, 32, 125}, {8, 7.5, 16, 40, 160}, {10, 9.5, 20, 45, 200}};
constexpr double kPinLengths[] = {6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26,  28,  30,  32,  35,  40, 45,
                                  50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 120, 140, 160, 180, 200};
constexpr double kCotterLengths[] = {4,  5,  6,  8,  10, 12, 14, 16, 18, 20,  22,  25,  28,  32,  36,
                                     40, 45, 50, 56, 63, 71, 80, 90, 100, 112, 125, 140, 160, 180, 200};

// ===========================================================================
// The catalogue.
// ===========================================================================

constexpr KindInfo kKinds[] = {
    {PartKind::HexBolt, "С шестигранной головкой", "Болт с шестигранной головкой", "Болт"},
    {PartKind::SocketScrew, "С цилиндрической головкой", "Винт с цилиндрической головкой и шестигранником", "Винт"},
    {PartKind::CountersunkScrew, "С потайной головкой", "Винт с потайной головкой", "Винт"},
    {PartKind::PanScrew, "С полукруглой головкой", "Винт с полукруглой головкой", "Винт"},
    {PartKind::HexNut, "Шестигранные", "Гайка шестигранная", "Гайка"},
    {PartKind::LockNut, "Самоконтрящиеся", "Гайка самоконтрящаяся", "Гайка"},
    {PartKind::FlatWasher, "Плоские", "Шайба плоская", "Шайба"},
    {PartKind::SpringWasher, "Пружинные", "Шайба пружинная", "Шайба"},
    {PartKind::PinCylindrical, "Цилиндрические", "Штифт цилиндрический", "Штифт"},
    {PartKind::PinTaper, "Конические", "Штифт конический", "Штифт"},
    {PartKind::CotterPin, "Шплинты", "Шплинт", "Шплинт"},
    {PartKind::BallBearing, "Шариковые радиальные", "Подшипник шариковый радиальный однорядный", "Подшипник"},
    {PartKind::RingExternal, "Наружные (на вал)", "Кольцо стопорное наружное", "Кольцо"},
    {PartKind::RingInternal, "Внутренние (в отверстие)", "Кольцо стопорное внутреннее", "Кольцо"},
};

constexpr PartKind kBolts[] = {PartKind::HexBolt};
constexpr PartKind kScrews[] = {PartKind::SocketScrew, PartKind::CountersunkScrew, PartKind::PanScrew};
constexpr PartKind kNuts[] = {PartKind::HexNut, PartKind::LockNut};
constexpr PartKind kWashers[] = {PartKind::FlatWasher, PartKind::SpringWasher};
constexpr PartKind kPins[] = {PartKind::PinCylindrical, PartKind::PinTaper};
constexpr PartKind kCotterPins[] = {PartKind::CotterPin};
constexpr PartKind kBearings_[] = {PartKind::BallBearing};
constexpr PartKind kRings[] = {PartKind::RingExternal, PartKind::RingInternal};

constexpr GroupInfo kGroups[] = {
    {"Болты", kBolts},     {"Винты", kScrews},         {"Гайки", kNuts},           {"Шайбы", kWashers},
    {"Штифты", kPins},     {"Шплинты", kCotterPins},   {"Подшипники", kBearings_}, {"Кольца стопорные", kRings},
};

constexpr StandardInfo kStandards[] = {
    {"gost7798", "ГОСТ 7798-70", PartKind::HexBolt, "нормальной точности"},
    {"gost7805", "ГОСТ 7805-70", PartKind::HexBolt, "повышенной точности"},
    {"iso4014", "ISO 4014", PartKind::HexBolt, "неполная резьба"},
    {"iso4017", "ISO 4017", PartKind::HexBolt, "резьба до головки"},
    {"gost11738", "ГОСТ 11738-84", PartKind::SocketScrew, ""},
    {"iso4762", "ISO 4762", PartKind::SocketScrew, ""},
    {"iso10642", "ISO 10642", PartKind::CountersunkScrew, "шестигранник под ключ"},
    {"gost17475", "ГОСТ 17475-80", PartKind::CountersunkScrew, "прямой шлиц"},
    {"iso2009", "ISO 2009", PartKind::CountersunkScrew, "прямой шлиц"},
    {"gost17473", "ГОСТ 17473-80", PartKind::PanScrew, "прямой шлиц"},
    {"iso7045", "ISO 7045", PartKind::PanScrew, "крестовой шлиц"},
    {"iso1580", "ISO 1580", PartKind::PanScrew, "прямой шлиц"},
    {"gost5915", "ГОСТ 5915-70", PartKind::HexNut, ""},
    {"iso4032", "ISO 4032", PartKind::HexNut, ""},
    {"iso7040", "ISO 7040", PartKind::LockNut, "с неметаллической вставкой"},
    {"gost11371", "ГОСТ 11371-78", PartKind::FlatWasher, "обычная"},
    {"gost6958", "ГОСТ 6958-78", PartKind::FlatWasher, "увеличенная"},
    {"iso7089", "ISO 7089", PartKind::FlatWasher, "обычная"},
    {"din9021", "DIN 9021", PartKind::FlatWasher, "увеличенная"},
    {"gost6402", "ГОСТ 6402-70", PartKind::SpringWasher, "нормальная (Н)"},
    {"din127", "DIN 127", PartKind::SpringWasher, "исполнение B"},
    {"gost8338", "ГОСТ 8338-75", PartKind::BallBearing, "серии 100, 200, 300"},
    {"iso15", "ISO 15", PartKind::BallBearing, "серии 60, 62, 63"},
    {"gost13942", "ГОСТ 13942-86", PartKind::RingExternal, ""},
    {"din471", "DIN 471", PartKind::RingExternal, ""},
    {"gost13943", "ГОСТ 13943-86", PartKind::RingInternal, ""},
    {"din472", "DIN 472", PartKind::RingInternal, ""},
    {"gost3128", "ГОСТ 3128-70", PartKind::PinCylindrical, ""},
    {"iso2338", "ISO 2338", PartKind::PinCylindrical, ""},
    {"din7", "DIN 7", PartKind::PinCylindrical, ""},
    {"gost3129", "ГОСТ 3129-70", PartKind::PinTaper, "конусность 1:50"},
    {"iso2339", "ISO 2339", PartKind::PinTaper, "конусность 1:50"},
    {"gost397", "ГОСТ 397-79", PartKind::CotterPin, ""},
    {"iso1234", "ISO 1234", PartKind::CotterPin, ""},
};

// ===========================================================================
// Kinds and standards of the loaded packs (StandardPacks.h), next to the
// built-in ones. Rebuilt when the packs change.
// ===========================================================================

struct Catalogue {
    unsigned                          generation = 0;
    std::vector<KindInfo>             kinds;           ///< built-in, then the packs'
    std::vector<const PackKind*>      packKinds;       ///< by PartKind - kFirstPackKind
    std::vector<StandardInfo>         standards;       ///< the packs'
    std::vector<const PackStandard*>  packStandards;   ///< parallel to `standards`
    std::vector<GroupInfo>            groups;          ///< built-in folders, then new ones of the packs
    std::deque<std::vector<PartKind>> groupKinds;      ///< what `groups` point into
};

const Catalogue& Cat() {
    static Catalogue c;
    if (c.generation == PacksGeneration()) return c;
    c = Catalogue{};
    c.generation = PacksGeneration();
    c.kinds.assign(std::begin(kKinds), std::end(kKinds));
    for (const GroupInfo& g : kGroups) {
        c.groupKinds.emplace_back(g.kinds.begin(), g.kinds.end());
        c.groups.push_back({g.name, {}});
    }
    for (const auto& pack : LoadedPacks())
        for (const PackKind& k : pack->kinds) {
            if (kFirstPackKind + c.packKinds.size() > 255) break;   // PartKind is a byte
            const auto kind = static_cast<PartKind>(kFirstPackKind + c.packKinds.size());
            c.packKinds.push_back(&k);
            c.kinds.push_back({kind, k.variety, k.title, k.noun});
            auto g = std::ranges::find(c.groups, std::string_view(k.group), &GroupInfo::name);
            if (g == c.groups.end()) {
                c.groupKinds.emplace_back();
                c.groups.push_back({k.group, {}});
                g = c.groups.end() - 1;
            }
            c.groupKinds[static_cast<std::size_t>(g - c.groups.begin())].push_back(kind);
        }
    for (std::size_t i = 0; i < c.groups.size(); ++i) c.groups[i].kinds = c.groupKinds[i];
    for (const auto& pack : LoadedPacks())
        for (const PackStandard& s : pack->standards) {
            const auto k = std::ranges::find_if(c.packKinds, [&](const PackKind* p) { return p->id == s.kind; });
            if (k == c.packKinds.end()) continue;
            c.standards.push_back({s.id, s.title, static_cast<PartKind>(kFirstPackKind + (k - c.packKinds.begin())), s.note});
            c.packStandards.push_back(&s);
        }
    return c;
}

/// The pack data of a standard; nullptr for a built-in one.
const PackStandard* PackOf(const StandardInfo& s) {
    if (!IsPackKind(s.kind)) return nullptr;
    const Catalogue& c = Cat();
    for (std::size_t i = 0; i < c.standards.size(); ++i)
        if (c.standards[i].id == s.id) return c.packStandards[i];
    return nullptr;
}

const PackKind* PackKindOf(PartKind kind) {
    if (!IsPackKind(kind)) return nullptr;
    const std::size_t i = static_cast<unsigned>(kind) - kFirstPackKind;
    const Catalogue& c = Cat();
    return i < c.packKinds.size() ? c.packKinds[i] : nullptr;
}

/// Built-in standards, then the packs'.
template <class F>
void ForEachStandard(F&& f) {
    for (const StandardInfo& s : kStandards) f(s);
    for (const StandardInfo& s : Cat().standards) f(s);
}

/// "40x20x2", "40 х 20 х 2,5" -> "40×20×2.5": how a size of a pack may be typed.
std::string NormalSize(std::string_view size) {
    std::string out;
    for (std::size_t i = 0; i < size.size(); ++i) {
        const char c = size[i];
        if (c == ' ') continue;
        if (c == 'x' || c == 'X' || c == '*') out += "\xC3\x97";
        else if (c == ',') out += '.';
        else if (size.substr(i).starts_with("\xD1\x85") || size.substr(i).starts_with("\xD0\xA5")) {   // Cyrillic х, Х
            out += "\xC3\x97";
            ++i;
        } else out += c;
    }
    return out;
}

/// The row of a pack standard with this size, or nullopt.
std::optional<std::size_t> PackRow(const PackStandard& p, std::string_view size) {
    const std::string want = NormalSize(size);
    for (std::size_t i = 0; i < p.rows.size(); ++i)
        if (NormalSize(PackSize(p, i)) == want) return i;
    return std::nullopt;
}

bool IsGost(const StandardInfo& s) { return FamilyOf(s) == Family::Gost; }

/// A number as written in a size or a designation: "10", "2.5".
std::string Num(double v) {
    std::string s = std::format("{:.2f}", v);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s;
}

/// "M10" with the Latin M for ISO/DIN, the Cyrillic М for ГОСТ.
std::string ThreadName(const StandardInfo& s, double d) { return (IsGost(s) ? "М" : "M") + Num(d); }

template <class Row, std::size_t N>
std::span<const Row> Rows(const Row (&rows)[N]) {
    return {rows, N};
}

std::span<const BoltRow> BoltRows(std::string_view id) {
    if (id == "gost7798") return Rows(kGost7798);
    if (id == "gost7805") return Rows(kGost7805);
    if (id == "iso4014") return Rows(kIso4014);
    return Rows(kIso4017);
}
std::span<const SlotRow> SlotRows(std::string_view id) {
    if (id == "iso1580") return Rows(kIso1580);
    if (id == "iso7045") return Rows(kIso7045);
    if (id == "gost17473") return Rows(kGost17473);
    return Rows(kIso2009);   // also ГОСТ 17475
}
std::span<const NutRow> NutRows(std::string_view id) {
    if (id == "gost5915") return Rows(kGost5915);
    if (id == "iso7040") return Rows(kIso7040);
    return Rows(kIso4032);
}
std::span<const WasherRow> WasherRows(std::string_view id) {
    if (id == "gost11371") return Rows(kGost11371);
    if (id == "gost6958") return Rows(kGost6958);
    if (id == "din9021") return Rows(kDin9021);
    if (id == "gost6402") return Rows(kGost6402);
    if (id == "din127") return Rows(kDin127);
    return Rows(kIso7089);
}

/// The diameter key of a size string: "M10" / "М10" -> 10, "30" -> 30.
std::optional<double> SizeNumber(std::string_view size) {
    if (size.starts_with("M")) size.remove_prefix(1);
    else if (size.starts_with("М")) size.remove_prefix(std::string_view("М").size());
    double v = 0;
    try {
        std::size_t used = 0;
        v = std::stod(std::string(size), &used);
        if (used != size.size()) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
    return v;
}

template <class Row>
const Row* FindRow(std::span<const Row> rows, double d) {
    for (const Row& r : rows)
        if (std::abs(r.d - d) < 1e-9) return &r;
    return nullptr;
}

/// ГОСТ 8338 names a bearing by its old number: 6204 -> 204, 6000 -> 100.
int GostBearingNumber(int iso) {
    const int series = iso / 100 % 10;   // 0, 2, 3
    return (series == 0 ? 100 : series * 100) + iso % 100;
}

/// Lengths from the preferred series within [lo, hi] (screws: the ГОСТ or
/// the ISO series; pins and cotter pins their own).
std::vector<double> LengthsBetween(const StandardInfo& s, double lo, double hi) {
    std::vector<double> out;
    const auto add = [&](const auto& series) {
        for (double l : series)
            if (l >= lo - 1e-9 && l <= hi + 1e-9) out.push_back(l);
    };
    if (s.kind == PartKind::PinCylindrical || s.kind == PartKind::PinTaper) add(kPinLengths);
    else if (s.kind == PartKind::CotterPin) add(kCotterLengths);
    else if (IsGost(s)) add(kGostLengths);
    else add(kIsoLengths);
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ===========================================================================
// Building the document.
// ===========================================================================

/// A length with the expression that gives it ("dk/2", 5.0).
struct X {
    std::string text;
    double      v = 0;
};
X N(double v) { return {Num(v), v}; }
X operator+(const X& a, const X& b) { return {a.text + " + " + b.text, a.v + b.v}; }
X operator-(const X& a, const X& b) { return {a.text + " - (" + b.text + ")", a.v - b.v}; }
X operator*(const X& a, double k) { return {"(" + a.text + ") * " + Num(k), a.v * k}; }
X operator/(const X& a, double k) { return {"(" + a.text + ") / " + Num(k), a.v / k}; }
X Half(const X& a) { return a.text.find_first_of("+-*/ ") == std::string::npos ? X{a.text + "/2", a.v / 2} : a / 2; }

/// Signed sketch coordinates: a magnitude with a direction, so the
/// dimensions stay positive ("L", not "-L").
struct P {
    X    x, y;
    bool negX = false, negY = false;
};

class Builder {
public:
    Builder(Document& d) : myDoc(d) {}

    /// A user variable; returns it for use in expressions.
    X Var(const std::string& name, double value, Quantity q = Quantity::Length) { return Var(name, value, Num(value), q); }
    /// The same with the value written as `text` (a pack's table: every digit kept).
    X Var(const std::string& name, double value, const std::string& text, Quantity q = Quantity::Length) {
        myDoc.variables.push_back(Parameter{.name = name, .expression = text, .quantity = q, .value = value, .error = {}});
        return {name, value};
    }

    SketchFeature& Sketch(ObjectId plane, const char* base = "Эскиз") {
        auto sk = std::make_unique<SketchFeature>();
        sk->id = myDoc.Ids().Next();
        sk->name = myDoc.UniqueFeatureName(base);
        sk->placement = {plane, origin::PlaneFrame(plane).value_or(Frame{}), false, {}};
        myOrigin = sk->sketch.EnsureOrigin(myDoc.Ids());
        auto* raw = sk.get();
        myDoc.Insert(std::move(sk), myDoc.FeatureCount());
        return *raw;
    }

    /// A sketch on a flat face of the body `support` made (normal +Z at height z).
    SketchFeature& SketchOnTop(ObjectId support, const X& z) {
        SketchFeature& sk = Sketch(origin::kPlaneXY);
        const Vec3 at{0, 0, z.v};
        sk.placement = {support, Frame{at, {1, 0, 0}, {0, 0, 1}}, true, FaceReference{{0, 0, 1}, at, false}};
        return sk;
    }

    /// A point held by dimensions from the sketch origin.
    ObjectId Point(SketchFeature& sk, const P& p) {
        const double x = p.negX ? -p.x.v : p.x.v, y = p.negY ? -p.y.v : p.y.v;
        if (std::abs(x) < 1e-12 && std::abs(y) < 1e-12) return myOrigin;
        const ObjectId id = sk.sketch.AddPoint(myDoc.Ids(), {x, y});
        if (std::abs(x) < 1e-12) Geometric(sk, sketch::ConstraintType::Vertical, myOrigin, id);
        else Dimension(sk, sketch::ConstraintType::DistanceX, myOrigin, id, p.x);
        if (std::abs(y) < 1e-12) Geometric(sk, sketch::ConstraintType::Horizontal, myOrigin, id);
        else Dimension(sk, sketch::ConstraintType::DistanceY, myOrigin, id, p.y);
        return id;
    }

    /// A closed polyline through `points`; returns the line ids in order.
    std::vector<ObjectId> Polygon(SketchFeature& sk, const std::vector<P>& points) {
        std::vector<ObjectId> ids;
        for (const P& p : points) ids.push_back(Point(sk, p));
        std::vector<ObjectId> lines;
        for (std::size_t i = 0; i < ids.size(); ++i) lines.push_back(sk.sketch.AddLine(myDoc.Ids(), ids[i], ids[(i + 1) % ids.size()]));
        return lines;
    }

    /// A regular hexagon `s` across flats, centred on the origin, corners on X.
    void Hexagon(SketchFeature& sk, const X& s) {
        const X r = s / std::sqrt(3.0), h = Half(r), f = Half(s);
        Polygon(sk, {{r, N(0)}, {h, f}, {h, f, true}, {r, N(0), true}, {h, f, true, true}, {h, f, false, true}});
    }

    /// A centred rectangle `w` wide (along sketch X) from height `y0` to `y1`.
    void Slot(SketchFeature& sk, const X& w, const P& y0, const P& y1) {
        const X hw = Half(w);
        Polygon(sk, {{hw, y0.y, true, y0.negY}, {hw, y0.y, false, y0.negY}, {hw, y1.y, false, y1.negY}, {hw, y1.y, true, y1.negY}});
    }

    /// A circle on the origin with a diameter dimension.
    void Circle(SketchFeature& sk, const X& diameter) {
        const ObjectId c = sk.sketch.AddCircle(myDoc.Ids(), myOrigin, diameter.v / 2);
        Dimension(sk, sketch::ConstraintType::Diameter, c, {}, diameter);
    }

    /// The осевая along sketch Y between two points of it (made if needed).
    ObjectId Axis(SketchFeature& sk, const P& a, const P& b) {
        const ObjectId line = sk.sketch.AddLine(myDoc.Ids(), Point(sk, a), Point(sk, b));
        MarkAxis(sk, line);
        return line;
    }
    static void MarkAxis(SketchFeature& sk, ObjectId line) {
        for (auto& l : sk.sketch.lines)
            if (l.id == line) l.centerline = true;
    }

    /// An arc counter-clockwise from `start` to `end` with its centre on the axis.
    void Arc(SketchFeature& sk, ObjectId start, ObjectId end, double centreY) {
        const ObjectId c = sk.sketch.AddPoint(myDoc.Ids(), {0, centreY});
        Geometric(sk, sketch::ConstraintType::Vertical, myOrigin, c);
        sk.sketch.AddArc(myDoc.Ids(), c, start, end);
    }

    RevolveFeature& Revolve(const SketchFeature& sk, ObjectId axis, BooleanOp op = BooleanOp::NewBody) {
        auto f = std::make_unique<RevolveFeature>();
        f->sketch = sk.id;
        f->axis = {RevolveAxisKind::SketchLine, axis};
        f->extent = RevolveExtent::Full;
        f->operation = op;
        f->angle.name = myDoc.NextParameterName();
        RevolveFeature& made = Add(std::move(f), "Вращение");
        if (op == BooleanOp::NewBody && !myFirstBody) myFirstBody = made.id;
        return made;
    }

    ExtrudeFeature& Extrude(const SketchFeature& sk, const X& distance, BooleanOp op, bool reversed = false,
                            ExtentType extent = ExtentType::Distance) {
        auto f = std::make_unique<ExtrudeFeature>();
        f->sketch = sk.id;
        f->operation = op;
        f->reversed = reversed;
        f->extent = extent;
        f->distance.name = myDoc.NextParameterName();
        f->distance.expression = distance.text;
        f->distance.value = distance.v;
        ExtrudeFeature& made = Add(std::move(f), op == BooleanOp::Cut ? "Вырез" : "Выдавливание");
        if (op == BooleanOp::NewBody && !myFirstBody) myFirstBody = made.id;
        return made;
    }

    /// Cosmetic thread on the cylinder of radius `d`/2 around Z.
    void Thread(const X& d, const X& pitch, double midZ, std::optional<X> length, double size) {
        auto f = std::make_unique<ThreadFeature>();
        f->face = CylinderTouch{{0, 0, midZ}, {0, 0, 1}, d.v / 2, {}};
        f->cosmetic = !myRealThread;
        f->size = size;
        f->pitch.name = myDoc.NextParameterName();
        f->pitch.expression = pitch.text;
        f->pitch.value = pitch.v;
        // The feature is not in the document yet: the next free name after the pitch's.
        f->length.name = "d" + std::to_string(std::stoi(f->pitch.name.substr(1)) + 1);
        f->fullLength = !length;
        if (length) {
            f->length.expression = length->text;
            f->length.value = length->v;
        }
        Add(std::move(f), "Резьба");
    }

private:
    template <class F>
    F& Add(std::unique_ptr<F> f, const char* base) {
        f->id = myDoc.Ids().Next();
        f->name = myDoc.UniqueFeatureName(base);
        auto* raw = f.get();
        myDoc.Insert(std::move(f), myDoc.FeatureCount());
        return *raw;
    }

    void Geometric(SketchFeature& sk, sketch::ConstraintType type, ObjectId a, ObjectId b) {
        sketch::Constraint c;
        c.type = type;
        c.refs = {a, b, {}};
        sk.sketch.AddConstraint(myDoc.Ids(), c);
    }

    void Dimension(SketchFeature& sk, sketch::ConstraintType type, ObjectId a, ObjectId b, const X& value) {
        sketch::Constraint c;
        c.type = type;
        c.refs = {a, b, {}};
        c.parameter = Parameter{.name = myDoc.NextParameterName(), .expression = value.text, .quantity = Quantity::Length,
                                .value = value.v, .error = {}};
        sk.sketch.AddConstraint(myDoc.Ids(), c);
    }

public:
    /// The feature that made the body (its look is set on it).
    ObjectId FirstBody() const { return myFirstBody; }
    IdGenerator& Ids() { return myDoc.Ids(); }
    /// A constraint without a value between two entities.
    void Align(SketchFeature& sk, sketch::ConstraintType type, ObjectId a, ObjectId b) { Geometric(sk, type, a, b); }
    void     RealThread(bool real) { myRealThread = real; }

    /// A circle centred at `centre` (held by dimensions) with a diameter dimension.
    void CircleAt(SketchFeature& sk, const P& centre, const X& diameter) {
        const ObjectId c = sk.sketch.AddCircle(myDoc.Ids(), Point(sk, centre), diameter.v / 2);
        Dimension(sk, sketch::ConstraintType::Diameter, c, {}, diameter);
    }

private:
    Document& myDoc;
    ObjectId  myOrigin;
    ObjectId  myFirstBody;
    bool      myRealThread = false;
};

/// Shank from z = 0 down to -L with a 45° tip chamfer, and a head profile
/// above it given as points from the shank top outward/up (x >= 0, y >= 0),
/// ending on the axis. Returns the revolve.
RevolveFeature& RevolvedScrew(Builder& b, const X& d, const X& L, const X& pitch, const std::vector<P>& head) {
    SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
    const X r = Half(d);
    std::vector<P> pts;
    pts.push_back({N(0), L, false, true});   // tip on the axis
    pts.push_back({r - pitch, L, false, true});
    pts.push_back({r, L - pitch, false, true});
    for (const P& p : head) pts.push_back(p);
    const std::vector<ObjectId> lines = b.Polygon(sk, pts);
    Builder::MarkAxis(sk, lines.back());   // the closing line runs along the axis
    return b.Revolve(sk, lines.back());
}

void Finish(Document& d, std::string name, std::string designation) {
    d.info.name = std::move(name);
    d.info.designation = std::move(designation);
    d.info.material = "Сталь";
    d.MarkChanged(0);
}

// --- Parts of packs: a recipe of simple shapes over the table's variables ------

/// A signed coordinate with the expression that gives it ("-(b/2)", -20).
struct S {
    std::string text;
    double      v = 0;
};

/// A + or - at the top level of `t` (not inside parentheses, not a sign).
bool HasAddSub(std::string_view t) {
    int depth = 0;
    char before = '(';   // the last non-space character: a sign follows an operator or '('
    for (const char c : t) {
        if (c == ' ') continue;
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if ((c == '+' || c == '-') && depth == 0 && std::string_view("+-*/(").find(before) == std::string_view::npos) return true;
        before = c;
    }
    return false;
}
/// The whole of `t` is one group in parentheses.
bool WholeGroup(std::string_view t) {
    if (t.size() < 2 || t.front() != '(' || t.back() != ')') return false;
    int depth = 0;
    for (std::size_t i = 0; i + 1 < t.size(); ++i) {
        depth += t[i] == '(' ? 1 : t[i] == ')' ? -1 : 0;
        if (depth == 0) return false;   // "(a) * (b)": the first group closes early
    }
    return true;
}
std::string Paren(const std::string& t) { return HasAddSub(t) ? "(" + t + ")" : t; }

/// The expression with the opposite sign, kept readable: "-b/2" -> "b/2",
/// "R - b/2" -> "b/2 - R", "a + b" -> "-(a + b)".
std::string Negated(const std::string& t) {
    if (t.starts_with("-")) {
        const std::string rest = t.substr(1);
        if (WholeGroup(rest)) return rest.substr(1, rest.size() - 2);
        if (!HasAddSub(rest)) return rest;
    }
    // One subtraction at the top level: swap its sides.
    int depth = 0, subs = 0;
    std::size_t at = std::string::npos;
    char before = '(';
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == ' ') continue;
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if ((c == '+' || c == '-') && depth == 0 && std::string_view("+-*/(").find(before) == std::string_view::npos) {
            if (c == '+') subs = 99;
            else ++subs, at = i;
        }
        before = c;
    }
    if (subs == 1 && t[0] != '-') {
        std::string a = t.substr(0, at), b = t.substr(at + 1);
        while (!a.empty() && a.back() == ' ') a.pop_back();
        while (!b.empty() && b.front() == ' ') b.erase(0, 1);
        return b + " - " + Paren(a);
    }
    return HasAddSub(t) ? "-(" + t + ")" : "-" + t;
}

bool IsZero(const S& a) { return std::abs(a.v) < 1e-12 && (a.text.empty() || a.text == "0"); }
S Minus(const S& a, const S& b) {
    if (IsZero(b)) return a;
    if (IsZero(a)) return {Negated(b.text), -b.v};
    return {a.text + " - " + Paren(b.text), a.v - b.v};
}
S Plus(const S& a, const S& b) {
    if (IsZero(a)) return b;
    if (IsZero(b)) return a;
    if (b.text.starts_with("-") && !HasAddSub(b.text.substr(1))) return Minus(a, {b.text.substr(1), -b.v});
    if (a.text.starts_with("-") && !HasAddSub(a.text.substr(1))) {   // -x + b
        const std::string x = a.text.substr(1);
        if (a.v + b.v < 0) return {"-(" + x + " - " + Paren(b.text) + ")", a.v + b.v};   // its magnitude reads "x - b"
        return Minus(b, {x, -a.v});
    }
    return {a.text + " + " + b.text, a.v + b.v};
}
S HalfOf(const S& a) { return {Paren(a.text) + "/2", a.v / 2}; }

/// The point (x, y) as the builder holds it: magnitudes with directions.
P PointOf(const S& x, const S& y) {
    const auto mag = [](const S& a) { return X{a.v < 0 ? Negated(a.text) : (a.text.empty() ? std::string("0") : a.text), std::abs(a.v)}; };
    return {mag(x), mag(y), x.v < 0, y.v < 0};
}

class PackBuilder {
public:
    PackBuilder(Builder& b, const PackStandard& s, std::size_t row, double length) : myB(b), myStandard(s) {
        for (std::size_t i = 0; i < s.columns.size(); ++i)
            if (s.columns[i].length) myValues.emplace_back(s.columns[i].name, s.rows[row][i]);
        if (s.length.mode != PackLength::Mode::None) myValues.emplace_back("L", length);
        for (const auto& [name, value] : myValues) myB.Var(name, value, std::format("{}", value));   // shortest exact form
    }

    /// An expression of the recipe; empty text is 0.
    Expected<S> Ex(const std::string& text) const {
        if (text.empty()) return S{"0", 0};
        const Expected<double> v = EvaluateExpression(text, Quantity::Length, UnitSystem{}, [this](std::string_view name) -> std::optional<NamedValue> {
            for (const auto& [n, value] : myValues)
                if (n == name) return NamedValue{value, Quantity::Length};
            return std::nullopt;
        });
        if (!v) return Error{std::format("{}: «{}»: {}", myStandard.id, text, v.GetError().message)};
        return S{text, *v};
    }

    Status Shape(SketchFeature& sk, const PackShape& shape, ObjectId& axis) {
        switch (shape.type) {
            case PackShape::Type::Rect: {
                const Expected<S> w = Ex(shape.w), h = Ex(shape.h), r = Ex(shape.r), cx = Ex(shape.x), cy = Ex(shape.y);
                for (const Expected<S>* e : {&w, &h, &r, &cx, &cy})
                    if (!*e) return e->GetError();
                Rect(sk, *w, *h, *r, *cx, *cy);
                return Status::Ok();
            }
            case PackShape::Type::Circle: {
                const Expected<S> d = Ex(shape.d), cx = Ex(shape.x), cy = Ex(shape.y);
                for (const Expected<S>* e : {&d, &cx, &cy})
                    if (!*e) return e->GetError();
                if (IsZero(*cx) && IsZero(*cy)) myB.Circle(sk, X{d->text, d->v});
                else myB.CircleAt(sk, PointOf(*cx, *cy), X{d->text, d->v});
                return Status::Ok();
            }
            case PackShape::Type::Hexagon: {
                const Expected<S> across = Ex(shape.s);
                if (!across) return across.GetError();
                myB.Hexagon(sk, X{across->text, across->v});
                return Status::Ok();
            }
            case PackShape::Type::Axis: {
                const Expected<S> x0 = Ex(shape.points[0][0]), y0 = Ex(shape.points[0][1]), x1 = Ex(shape.points[1][0]),
                                  y1 = Ex(shape.points[1][1]);
                for (const Expected<S>* e : {&x0, &y0, &x1, &y1})
                    if (!*e) return e->GetError();
                axis = myB.Axis(sk, PointOf(*x0, *y0), PointOf(*x1, *y1));
                return Status::Ok();
            }
            case PackShape::Type::Polygon: {
                std::vector<P> points;
                for (const auto& [px, py] : shape.points) {
                    const Expected<S> x = Ex(px), y = Ex(py);
                    if (!x) return x.GetError();
                    if (!y) return y.GetError();
                    points.push_back(PointOf(*x, *y));
                }
                const std::vector<ObjectId> lines = myB.Polygon(sk, points);
                if (shape.axis) {
                    Builder::MarkAxis(sk, lines.back());
                    axis = lines.back();
                }
                return Status::Ok();
            }
        }
        return Status::Ok();
    }

private:
    /// w × h centred on (cx, cy); corners rounded by r unless r is 0. Each
    /// arc's centre is held under its end on the horizontal side; the
    /// solver's equal radii place it.
    void Rect(SketchFeature& sk, const S& w, const S& h, const S& r, const S& cx, const S& cy) {
        const S hw = HalfOf(w), hh = HalfOf(h);
        const S right = Plus(cx, hw), left = Minus(cx, hw), top = Plus(cy, hh), bottom = Minus(cy, hh);
        if (r.v < 1e-9) {
            myB.Polygon(sk, {PointOf(right, bottom), PointOf(right, top), PointOf(left, top), PointOf(left, bottom)});
            return;
        }
        const S innerR = Minus(right, r), innerL = Plus(left, r), innerT = Minus(top, r), innerB = Plus(bottom, r);
        // Counter-clockwise from the bottom of the right side.
        const ObjectId a1 = myB.Point(sk, PointOf(right, innerB)), a2 = myB.Point(sk, PointOf(right, innerT));
        const ObjectId b1 = myB.Point(sk, PointOf(innerR, top)), b2 = myB.Point(sk, PointOf(innerL, top));
        const ObjectId c1 = myB.Point(sk, PointOf(left, innerT)), c2 = myB.Point(sk, PointOf(left, innerB));
        const ObjectId d1 = myB.Point(sk, PointOf(innerL, bottom)), d2 = myB.Point(sk, PointOf(innerR, bottom));
        IdGenerator& ids = myB.Ids();
        sk.sketch.AddLine(ids, a1, a2);
        Corner(sk, a2, b1, b1, {innerR.v, innerT.v});
        sk.sketch.AddLine(ids, b1, b2);
        Corner(sk, b2, c1, b2, {innerL.v, innerT.v});
        sk.sketch.AddLine(ids, c1, c2);
        Corner(sk, c2, d1, d1, {innerL.v, innerB.v});
        sk.sketch.AddLine(ids, d1, d2);
        Corner(sk, d2, a1, d2, {innerR.v, innerB.v});
    }

    /// An arc from `start` to `end` counter-clockwise; its centre at `at`, held vertically under `under`.
    void Corner(SketchFeature& sk, ObjectId start, ObjectId end, ObjectId under, Vec2 at) {
        const ObjectId centre = sk.sketch.AddPoint(myB.Ids(), at);
        myB.Align(sk, sketch::ConstraintType::Vertical, under, centre);
        sk.sketch.AddArc(myB.Ids(), centre, start, end);
    }

    Builder&                                     myB;
    const PackStandard&                          myStandard;
    std::vector<std::pair<std::string, double>>  myValues;
};

/// Builds a part of a pack's kind from a row of its standard.
Status BuildPack(Builder& b, const PackKind& kind, const PackStandard& standard, std::size_t row, double length, Envelope& envelope) {
    PackBuilder pb(b, standard, row, length);
    SketchFeature* sketch = nullptr;
    ObjectId axis;
    for (const PackStep& step : kind.build) {
        const auto op = step.operation == PackStep::Operation::Cut    ? BooleanOp::Cut
                        : step.operation == PackStep::Operation::Join ? BooleanOp::Join
                                                                      : BooleanOp::NewBody;
        switch (step.type) {
            case PackStep::Type::Sketch: {
                const ObjectId plane = step.plane == "XZ" ? origin::kPlaneXZ : step.plane == "YZ" ? origin::kPlaneYZ : origin::kPlaneXY;
                sketch = &b.Sketch(plane);
                axis = {};
                for (const PackShape& shape : step.shapes)
                    if (const Status ok = pb.Shape(*sketch, shape, axis); !ok) return ok;
                break;
            }
            case PackStep::Type::Extrude: {
                const Expected<S> d = pb.Ex(step.distance);
                if (!d) return d.GetError();
                b.Extrude(*sketch, X{d->text, d->v}, op, step.direction == PackStep::Direction::Reversed,
                          step.direction == PackStep::Direction::Symmetric ? ExtentType::Symmetric : ExtentType::Distance);
                break;
            }
            case PackStep::Type::Revolve:
                if (!axis) return Error{std::format("{}: вращение без осевой (polygon с «axis»: true)", kind.id)};
                b.Revolve(*sketch, axis, op);
                break;
        }
    }
    double* out[] = {&envelope.x, &envelope.y, &envelope.z};
    for (std::size_t i = 0; i < 3; ++i) {
        const Expected<S> e = pb.Ex(kind.envelope[i]);
        if (!e) return e.GetError();
        *out[i] = e->v;
    }
    return Status::Ok();
}

}   // namespace

// ===========================================================================

std::span<const KindInfo> Kinds() { return Cat().kinds; }

const KindInfo& KindOf(PartKind kind) {
    for (const KindInfo& k : Cat().kinds)
        if (k.kind == kind) return k;
    return kKinds[0];
}

std::span<const GroupInfo> Groups() { return Cat().groups; }

Family FamilyOf(const StandardInfo& s) {
    std::string_view by = s.title;
    if (const PackStandard* p = PackOf(s); p && !p->family.empty()) by = p->family;
    if (by.starts_with("ГОСТ")) return Family::Gost;
    return by.starts_with("DIN") ? Family::Din : Family::Iso;
}

std::string ShortName(const StandardInfo& s) { return std::format("{} {}", KindOf(s.kind).noun, s.title); }

std::vector<const StandardInfo*> Analogues(const StandardInfo& standard) {
    std::vector<const StandardInfo*> out;
    ForEachStandard([&](const StandardInfo& s) {
        if (s.kind == standard.kind && s.id != standard.id) out.push_back(&s);
    });
    return out;
}

bool HasThread(PartKind kind) {
    switch (kind) {
        case PartKind::HexBolt:
        case PartKind::SocketScrew:
        case PartKind::CountersunkScrew:
        case PartKind::PanScrew:
        case PartKind::HexNut:
        case PartKind::LockNut: return true;
        default:                return false;
    }
}

std::string_view DefaultAppearance(PartKind kind) {
    if (const PackKind* k = PackKindOf(kind)) return k->appearance;
    switch (kind) {
        case PartKind::SpringWasher:
        case PartKind::RingExternal:
        case PartKind::RingInternal:
        case PartKind::CotterPin: return "std.blued";   // spring steel, oxidised
        default:                  return "std.steel";
    }
}

std::vector<const StandardInfo*> StandardsOf(PartKind kind) {
    std::vector<const StandardInfo*> out;
    ForEachStandard([&](const StandardInfo& s) {
        if (s.kind == kind) out.push_back(&s);
    });
    return out;
}

const StandardInfo* FindStandard(std::string_view id) {
    const StandardInfo* found = nullptr;
    ForEachStandard([&](const StandardInfo& s) {
        if (!found && s.id == id) found = &s;
    });
    return found;
}

std::vector<std::string> Sizes(const StandardInfo& s) {
    std::vector<std::string> out;
    if (const PackStandard* p = PackOf(s)) {
        for (std::size_t i = 0; i < p->rows.size(); ++i) out.push_back(PackSize(*p, i));
        return out;
    }
    const auto threads = [&](auto rows) {
        for (const auto& r : rows) out.push_back(ThreadName(s, r.d));
    };
    switch (s.kind) {
        case PartKind::HexBolt:          threads(BoltRows(s.id)); break;
        case PartKind::SocketScrew:      threads(Rows(kIso4762)); break;
        case PartKind::CountersunkScrew: s.id == "iso10642" ? threads(Rows(kIso10642)) : threads(SlotRows(s.id)); break;
        case PartKind::PanScrew:         threads(SlotRows(s.id)); break;
        case PartKind::HexNut:
        case PartKind::LockNut:          threads(NutRows(s.id)); break;
        case PartKind::FlatWasher:
        case PartKind::SpringWasher:
            for (const WasherRow& r : WasherRows(s.id)) out.push_back(Num(r.d));
            break;
        case PartKind::BallBearing:
            for (const BearingRow& r : kBearings) out.push_back(std::to_string(s.id == "gost8338" ? GostBearingNumber(r.number) : r.number));
            break;
        case PartKind::RingExternal:
            for (const RingRow& r : kRingsExternal) out.push_back(Num(r.d1));
            break;
        case PartKind::RingInternal:
            for (const RingRow& r : kRingsInternal) out.push_back(Num(r.d1));
            break;
        case PartKind::PinCylindrical:
            for (const PinRow& r : kPinsCylindrical) out.push_back(Num(r.d));
            break;
        case PartKind::PinTaper:
            for (const PinRow& r : kPinsTaper) out.push_back(Num(r.d));
            break;
        case PartKind::CotterPin:
            for (const CotterRow& r : kCotters) out.push_back(Num(r.d));
            break;
    }
    return out;
}

std::string MatchingSize(const StandardInfo& from, std::string_view size, const StandardInfo& to) {
    const std::vector<std::string> sizes = Sizes(to);
    if (sizes.empty()) return {};
    if (const PackStandard* p = PackOf(to)) {   // the same size if the other table has it
        const std::optional<std::size_t> row = PackRow(*p, size);
        return row ? sizes[*row] : sizes.front();
    }
    if (from.kind == PartKind::BallBearing && to.kind == PartKind::BallBearing) {
        // Through the ISO number: 204 (ГОСТ) = 6204.
        for (const BearingRow& r : kBearings) {
            const std::string inFrom = std::to_string(from.id == "gost8338" ? GostBearingNumber(r.number) : r.number);
            if (inFrom == size) return std::to_string(to.id == "gost8338" ? GostBearingNumber(r.number) : r.number);
        }
        return sizes.front();
    }
    const std::optional<double> d = SizeNumber(size);
    for (const std::string& s : sizes)
        if (const auto e = SizeNumber(s); d && e && std::abs(*d - *e) < 1e-9) return s;
    return sizes.front();
}

std::string MatchSize(const StandardInfo& standard, std::string_view given) {
    const std::vector<std::string> sizes = Sizes(standard);
    if (std::ranges::find(sizes, given) != sizes.end()) return std::string(given);
    if (const PackStandard* p = PackOf(standard)) {   // "40x20x2", "40×20×2,5"
        const std::optional<std::size_t> row = PackRow(*p, given);
        return row ? sizes[*row] : std::string(given);
    }
    if (given.starts_with("M") || given.starts_with("m")) {   // a Latin M typed for «М»
        const std::string cyrillic = "\xD0\x9C" + std::string(given.substr(1));
        if (std::ranges::find(sizes, cyrillic) != sizes.end()) return cyrillic;
    }
    // By the number: "M6" for a washer "6", "6" for a screw "М6".
    if (const auto d = SizeNumber(given))
        for (const std::string& s : sizes)
            if (const auto e = SizeNumber(s); e && std::abs(*e - *d) < 1e-9) return s;
    return std::string(given);
}

std::vector<double> Lengths(const StandardInfo& s, std::string_view size) {
    if (const PackStandard* p = PackOf(s)) return p->length.mode == PackLength::Mode::List ? p->length.list : std::vector<double>{};
    const std::optional<double> d = SizeNumber(size);
    if (!d) return {};
    switch (s.kind) {
        case PartKind::HexBolt:
            if (const BoltRow* r = FindRow(BoltRows(s.id), *d)) return LengthsBetween(s, r->lMin, r->lMax);
            break;
        case PartKind::SocketScrew:
            if (const SocketRow* r = FindRow(Rows(kIso4762), *d)) return LengthsBetween(s, r->lMin, r->lMax);
            break;
        case PartKind::CountersunkScrew:
            if (s.id == "iso10642") {
                if (const SocketRow* r = FindRow(Rows(kIso10642), *d)) return LengthsBetween(s, r->lMin, r->lMax);
                break;
            }
            [[fallthrough]];
        case PartKind::PanScrew:
            if (const SlotRow* r = FindRow(SlotRows(s.id), *d)) return LengthsBetween(s, r->lMin, r->lMax);
            break;
        case PartKind::PinCylindrical:
            if (const PinRow* r = FindRow(Rows(kPinsCylindrical), *d)) return LengthsBetween(s, r->lMin, r->lMax);
            break;
        case PartKind::PinTaper:
            if (const PinRow* r = FindRow(Rows(kPinsTaper), *d)) return LengthsBetween(s, r->lMin, r->lMax);
            break;
        case PartKind::CotterPin:
            if (const CotterRow* r = FindRow(Rows(kCotters), *d)) return LengthsBetween(s, r->lMin, r->lMax);
            break;
        default: break;
    }
    return {};
}

std::optional<LengthRange> FreeLength(const StandardInfo& s) {
    const PackStandard* p = PackOf(s);
    if (!p || p->length.mode != PackLength::Mode::Free) return std::nullopt;
    return LengthRange{p->length.min, p->length.max, p->length.preset};
}

std::string_view SizeTitle(const StandardInfo& s) {
    const PackKind* k = PackKindOf(s.kind);
    return k ? std::string_view(k->sizeTitle) : std::string_view{};
}

namespace {
std::string DesignationOf(const Request& q);
}

std::string Designation(const Request& q) {
    std::string out = DesignationOf(q);
    const StandardInfo* s = FindStandard(q.standard);
    // ГОСТ writes decimals with a comma: «Шплинт 3,2×20 ГОСТ 397-79».
    if (s && IsGost(*s)) std::ranges::replace(out, '.', ',');
    return out;
}

namespace {
std::string DesignationOf(const Request& q) {
    const StandardInfo* s = FindStandard(q.standard);
    if (!s) return {};
    if (const PackStandard* p = PackOf(*s)) {
        const std::optional<std::size_t> row = PackRow(*p, q.size);
        if (!row) return {};
        const PackKind* kind = PackKindOf(s->kind);
        const bool length = p->length.mode != PackLength::Mode::None;
        std::string pattern = p->designation;
        if (pattern.empty()) pattern = length ? "{noun} {size}\xC3\x97{L} {title}" : "{noun} {size} {title}";
        const std::pair<std::string, std::string> extra[] = {
            {"size", PackSize(*p, *row)}, {"L", PackNumber(q.length)}, {"title", p->title}, {"noun", kind ? kind->noun : std::string{}}};
        return FillPackTemplate(pattern, *p, *row, extra);
    }
    const std::string title(s->title);
    const bool gost = IsGost(*s);
    const auto withLength = [&](const char* noun) {
        const std::string size = q.size + "\xC3\x97" + Num(q.length);   // М10×40
        return gost ? std::format("{} {} {}", noun, size, title) : std::format("{} {} \xE2\x80\x93 {}", noun, title, size);
    };
    const auto plain = [&](const char* noun) {
        return gost ? std::format("{} {} {}", noun, q.size, title) : std::format("{} {} \xE2\x80\x93 {}", noun, title, q.size);
    };
    switch (s->kind) {
        case PartKind::HexBolt:          return withLength("Болт");
        case PartKind::SocketScrew:
        case PartKind::CountersunkScrew:
        case PartKind::PanScrew:         return withLength("Винт");
        case PartKind::HexNut:
        case PartKind::LockNut:          return plain("Гайка");
        case PartKind::FlatWasher:       return plain("Шайба");
        case PartKind::SpringWasher:     return gost ? std::format("Шайба {} 65Г {}", q.size, title) : plain("Шайба");
        case PartKind::BallBearing:      return plain("Подшипник");
        case PartKind::RingExternal:
        case PartKind::RingInternal: {
            if (gost) return std::format("Кольцо {}{} {}", s->kind == PartKind::RingExternal ? "А" : "В", q.size, title);
            const auto d = SizeNumber(q.size);
            const auto rows = s->kind == PartKind::RingExternal ? Rows(kRingsExternal) : Rows(kRingsInternal);
            for (const RingRow& r : rows)
                if (d && std::abs(r.d1 - *d) < 1e-9) return std::format("Кольцо {} \xE2\x80\x93 {}\xC3\x97{}", title, q.size, Num(r.s));
            return plain("Кольцо");
        }
        case PartKind::PinCylindrical:
        case PartKind::PinTaper:         return withLength("Штифт");
        case PartKind::CotterPin:        return withLength("Шплинт");
    }
    return {};
}
} // namespace

Expected<Made> MakePart(const Request& asked) {
    const StandardInfo* s = FindStandard(asked.standard);
    if (!s) return Error{std::format("Неизвестный стандарт «{}»", asked.standard)};
    Request q = asked;
    q.size = MatchSize(*s, asked.size);
    const std::vector<std::string> sizes = Sizes(*s);
    if (std::ranges::find(sizes, q.size) == sizes.end())
        return Error{std::format("{}: нет размера «{}»", s->title, q.size)};
    const std::vector<double> lengths = Lengths(*s, q.size);
    if (!lengths.empty() && std::ranges::none_of(lengths, [&](double l) { return std::abs(l - q.length) < 1e-9; }))
        return Error{std::format("{} {}: нет длины {} мм", s->title, q.size, Num(q.length))};
    if (const std::optional<LengthRange> range = FreeLength(*s)) {
        if (q.length <= 0) q.length = range->preset;   // not given: the usual one
        if (q.length < range->min - 1e-9 || q.length > range->max + 1e-9)
            return Error{std::format("{} {}: длина {} мм вне диапазона {}–{} мм", s->title, q.size, Num(q.length), Num(range->min),
                                     Num(range->max))};
    }

    Made made;
    Document& doc = made.doc;
    Builder b(doc);
    b.RealThread(q.realThread && HasThread(s->kind));
    const double dn = SizeNumber(q.size).value_or(0);
    const std::string designation = Designation(q);

    if (const PackStandard* p = PackOf(*s)) {
        const PackKind* kind = PackKindOf(s->kind);
        const std::optional<std::size_t> row = PackRow(*p, q.size);
        if (!kind || !row) return Error{std::format("{}: нет размера «{}»", s->title, q.size)};
        if (const Status built = BuildPack(b, *kind, *p, *row, q.length, made.envelope); !built) return built.GetError();
        Finish(doc, kind->noun, designation);
        doc.info.material = kind->material;
    } else switch (s->kind) {
        case PartKind::HexBolt: {
            const BoltRow& r = *FindRow(BoltRows(s->id), dn);
            const X d = b.Var("d", r.d), L = b.Var("L", q.length), P_ = b.Var("P", Pitch(r.d));
            const X sw = b.Var("s", r.s), k = b.Var("k", r.k);
            const bool full = s->id == "iso4017" || q.length <= 2 * r.d + 6 + 1e-9;
            const std::optional<X> thread = full ? std::nullopt : std::optional<X>(b.Var("b", 2 * r.d + (q.length > 125 ? 12 : 6)));
            const X r0 = Half(d);
            RevolvedScrew(b, d, L, P_, {{r0, N(0)}, {N(0), N(0)}});
            SketchFeature& head = b.Sketch(origin::kPlaneXY);
            b.Hexagon(head, sw);
            b.Extrude(head, k, BooleanOp::Join);
            b.Thread(d, P_, -q.length / 2, thread, r.d);
            made.envelope = {2 * r.s / std::sqrt(3.0), r.s, q.length + r.k};
            Finish(doc, "Болт", designation);
            break;
        }
        case PartKind::SocketScrew: {
            const SocketRow& r = *FindRow(Rows(kIso4762), dn);
            const X d = b.Var("d", r.d), L = b.Var("L", q.length), P_ = b.Var("P", Pitch(r.d));
            const X dk = b.Var("dk", r.dk), k = b.Var("k", r.k), sw = b.Var("s", r.s), t = b.Var("t", r.t);
            const std::optional<X> thread = q.length <= r.b + 1e-9 ? std::nullopt : std::optional<X>(b.Var("b", r.b));
            RevolveFeature& body = RevolvedScrew(b, d, L, P_, {{Half(d), N(0)}, {Half(dk), N(0)}, {Half(dk), k}, {N(0), k}});
            SketchFeature& socket = b.SketchOnTop(body.id, k);
            b.Hexagon(socket, sw);
            b.Extrude(socket, t, BooleanOp::Cut, true);
            b.Thread(d, P_, -q.length / 2, thread, r.d);
            made.envelope = {r.dk, r.dk, q.length + r.k};
            Finish(doc, "Винт", designation);
            break;
        }
        case PartKind::CountersunkScrew: {
            // The head top is at z = 0, the length L counts from it.
            const bool socket = s->id == "iso10642";
            double dkV = 0, kV = 0, recessV = 0, tV = 0;
            if (socket) {
                const SocketRow& r = *FindRow(Rows(kIso10642), dn);
                dkV = r.dk, kV = r.k, recessV = r.s, tV = r.t;
            } else {
                const SlotRow& r = *FindRow(SlotRows(s->id), dn);
                dkV = r.dk, kV = r.k, recessV = r.n, tV = r.t;
            }
            const X d = b.Var("d", dn), L = b.Var("L", q.length), P_ = b.Var("P", Pitch(dn));
            const X dk = b.Var("dk", dkV), k = b.Var("k", kV), t = b.Var("t", tV);
            const X rec = b.Var(socket ? "s" : "n", recessV);
            SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
            const std::vector<ObjectId> lines =
                b.Polygon(sk, {{N(0), L, false, true}, {Half(d) - P_, L, false, true}, {Half(d), L - P_, false, true},
                               {Half(d), k, false, true}, {Half(dk), N(0)}, {N(0), N(0)}});
            Builder::MarkAxis(sk, lines.back());
            b.Revolve(sk, lines.back());
            if (socket) {
                SketchFeature& top = b.Sketch(origin::kPlaneXY);
                b.Hexagon(top, rec);
                b.Extrude(top, t, BooleanOp::Cut, true);
            } else {
                SketchFeature& slot = b.Sketch(origin::kPlaneXZ);
                b.Slot(slot, rec, {N(0), t, false, true}, {N(0), N(1)});
                b.Extrude(slot, dk * 2, BooleanOp::Cut, false, ExtentType::Symmetric);
            }
            const double thread = socket ? FindRow(Rows(kIso10642), dn)->b : 38.0;
            b.Thread(d, P_, -q.length / 2, q.length - kV <= thread + 1e-9 ? std::nullopt : std::optional<X>(b.Var("b", thread)), dn);
            made.envelope = {dkV, dkV, q.length};
            Finish(doc, "Винт", designation);
            break;
        }
        case PartKind::PanScrew: {
            // Head from z = 0 up to k: a short cylinder (side `h1`) under a dome.
            const SlotRow& r = *FindRow(SlotRows(s->id), dn);
            const double side = s->id == "gost17473" ? 0.0 : s->id == "iso1580" ? 0.75 : 0.55;
            const X d = b.Var("d", r.d), L = b.Var("L", q.length), P_ = b.Var("P", Pitch(r.d));
            const X dk = b.Var("dk", r.dk), k = b.Var("k", r.k), n = b.Var("n", r.n), t = b.Var("t", r.t);
            const X h1 = k * side;
            SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
            const X rr = Half(d);
            std::vector<P> pts{{N(0), L, false, true}, {rr - P_, L, false, true}, {rr, L - P_, false, true}, {rr, N(0)}, {Half(dk), N(0)}};
            if (side > 0) pts.push_back({Half(dk), h1});
            pts.push_back({N(0), k});
            const std::vector<ObjectId> lines = b.Polygon(sk, pts);
            // The line from the head's side up to the top becomes an arc.
            const ObjectId domeLine = lines[lines.size() - 2];
            ObjectId start, end;
            for (auto it = sk.sketch.lines.begin(); it != sk.sketch.lines.end(); ++it)
                if (it->id == domeLine) {
                    start = it->start;
                    end = it->end;
                    sk.sketch.lines.erase(it);
                    break;
                }
            const double a = r.dk / 2, hh = r.k - side * r.k, radius = (a * a + hh * hh) / (2 * hh);
            b.Arc(sk, start, end, r.k - radius);
            Builder::MarkAxis(sk, lines.back());
            b.Revolve(sk, lines.back());
            SketchFeature& slot = b.Sketch(origin::kPlaneXZ);
            b.Slot(slot, n, {N(0), k - t}, {N(0), k + N(1)});
            b.Extrude(slot, dk * 2, BooleanOp::Cut, false, ExtentType::Symmetric);
            if (s->id == "iso7045") {   // cross recess: a second slot across
                SketchFeature& cross = b.Sketch(origin::kPlaneYZ);
                b.Slot(cross, n, {N(0), k - t}, {N(0), k + N(1)});
                b.Extrude(cross, dk * 2, BooleanOp::Cut, false, ExtentType::Symmetric);
            }
            b.Thread(d, P_, -q.length / 2, q.length <= 45 + 1e-9 ? std::nullopt : std::optional<X>(b.Var("b", 38)), r.d);
            made.envelope = {r.dk, r.dk, q.length + r.k};
            Finish(doc, "Винт", designation);
            break;
        }
        case PartKind::HexNut:
        case PartKind::LockNut: {
            const NutRow& r = *FindRow(NutRows(s->id), dn);
            const double minor = r.d - 1.0825 * Pitch(r.d);
            const X sw = b.Var("s", r.s), m = b.Var("H", r.m), P_ = b.Var("P", Pitch(r.d)), D1 = b.Var("D1", minor);
            SketchFeature& sk = b.Sketch(origin::kPlaneXY);
            b.Hexagon(sk, sw);
            b.Circle(sk, D1);
            b.Extrude(sk, m, BooleanOp::NewBody);
            double height = r.m;
            if (s->kind == PartKind::LockNut) {
                const X h = b.Var("h", r.h), dw = b.Var("dw", r.dw);
                SketchFeature& collar = b.Sketch(origin::kPlaneXZ);
                b.Polygon(collar, {{Half(D1), m}, {Half(dw), m}, {Half(dw), h}, {Half(D1), h}});
                const ObjectId axis = b.Axis(collar, {N(0), N(0)}, {N(0), h});
                b.Revolve(collar, axis, BooleanOp::Join);
                height = r.h;
            }
            b.Thread(D1, P_, r.m / 2, std::nullopt, r.d);
            made.envelope = {2 * r.s / std::sqrt(3.0), r.s, height};
            Finish(doc, "Гайка", designation);
            break;
        }
        case PartKind::FlatWasher:
        case PartKind::SpringWasher: {
            const WasherRow& r = *FindRow(WasherRows(s->id), dn);
            const X inner = b.Var("d_вн", r.inner), outer = b.Var("D_нар", r.outer), h = b.Var("h", r.h);
            SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
            b.Polygon(sk, {{Half(inner), N(0)}, {Half(outer), N(0)}, {Half(outer), h}, {Half(inner), h}});
            b.Revolve(sk, b.Axis(sk, {N(0), N(0)}, {N(0), h}));
            if (s->kind == PartKind::SpringWasher) {   // the cut: a gap as wide as the section
                SketchFeature& gap = b.Sketch(origin::kPlaneYZ);
                b.Slot(gap, h, {N(0), N(1), false, true}, {N(0), h + N(1)});
                b.Extrude(gap, outer, BooleanOp::Cut);
            }
            made.envelope = {r.outer, r.outer, r.h};
            Finish(doc, "Шайба", designation);
            break;
        }
        case PartKind::BallBearing: {
            const BearingRow* row = nullptr;
            for (const BearingRow& r : kBearings)
                if (std::to_string(s->id == "gost8338" ? GostBearingNumber(r.number) : r.number) == q.size) row = &r;
            const BearingRow& r = *row;
            const X d = b.Var("d", r.d), D = b.Var("D", r.D), B = b.Var("B", r.B);
            // One body: inner and outer rings with a shallow groove on each side between them.
            const X w = (D - d) * 0.11, g = B * 0.12;
            const X ri = Half(d), ro = Half(D), a = ri + w, c = ro - w;
            SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
            b.Polygon(sk, {{ri, N(0)}, {a, N(0)}, {a, g}, {c, g}, {c, N(0)}, {ro, N(0)}, {ro, B}, {c, B}, {c, B - g}, {a, B - g}, {a, B}, {ri, B}});
            b.Revolve(sk, b.Axis(sk, {N(0), N(0)}, {N(0), B}));
            made.envelope = {r.D, r.D, r.B};
            Finish(doc, "Подшипник", designation);
            break;
        }
        case PartKind::RingExternal:
        case PartKind::RingInternal: {
            const bool external = s->kind == PartKind::RingExternal;
            const RingRow* row = nullptr;
            for (const RingRow& r : external ? Rows(kRingsExternal) : Rows(kRingsInternal))
                if (std::abs(r.d1 - dn) < 1e-9) row = &r;
            const RingRow& r = *row;
            // Radial width of the band (the standards give it at the lugs; simplified to a plain band).
            const double width = 0.6 + 0.07 * r.d1;
            const X s_ = b.Var("s", r.s), d3 = b.Var("d3", r.d3), wv = b.Var("b", width);
            const X in = external ? Half(d3) : Half(d3) - wv, out = external ? Half(d3) + wv : Half(d3);
            SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
            b.Polygon(sk, {{in, N(0)}, {out, N(0)}, {out, s_}, {in, s_}});
            b.Revolve(sk, b.Axis(sk, {N(0), N(0)}, {N(0), s_}));
            SketchFeature& gap = b.Sketch(origin::kPlaneYZ);
            b.Slot(gap, d3 * 0.25, {N(0), N(1), false, true}, {N(0), s_ + N(1)});
            b.Extrude(gap, d3, BooleanOp::Cut);
            const double outerD = external ? r.d3 + 2 * width : r.d3;
            // The gap is cut on +X: across X the ring reaches the gap's corners only.
            const double R = outerD / 2, g = r.d3 * 0.25 / 2;
            made.envelope = {R + std::sqrt(R * R - g * g), outerD, r.s};
            Finish(doc, "Кольцо", designation);
            break;
        }
        case PartKind::PinCylindrical: {
            // From z = 0 up to L, a small chamfer at both ends.
            const X d = b.Var("d", dn), L = b.Var("L", q.length), c = d * 0.1, r = Half(d);
            SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
            const std::vector<ObjectId> lines =
                b.Polygon(sk, {{N(0), N(0)}, {r - c, N(0)}, {r, c}, {r, L - c}, {r - c, L}, {N(0), L}});
            Builder::MarkAxis(sk, lines.back());
            b.Revolve(sk, lines.back());
            made.envelope = {dn, dn, q.length};
            Finish(doc, "Штифт", designation);
            break;
        }
        case PartKind::PinTaper: {
            // The small end (d) at z = 0; the diameter grows by 1/50 of the length.
            const X d = b.Var("d", dn), L = b.Var("L", q.length);
            SketchFeature& sk = b.Sketch(origin::kPlaneXZ);
            const std::vector<ObjectId> lines =
                b.Polygon(sk, {{N(0), N(0)}, {Half(d), N(0)}, {Half(d + L / 50), L}, {N(0), L}});
            Builder::MarkAxis(sk, lines.back());
            b.Revolve(sk, lines.back());
            const double big = dn + q.length / 50;
            made.envelope = {big, big, q.length};
            Finish(doc, "Штифт", designation);
            break;
        }
        case PartKind::CotterPin: {
            // Shank (both halves together) from z = 0 down to -l, split along
            // its length; an eye of the bent wire on top, in the XZ plane.
            const CotterRow& r = *FindRow(Rows(kCotters), dn);
            const X w = b.Var("dw", r.wire), c = b.Var("c", r.eye), l = b.Var("l", q.length);
            SketchFeature& shank = b.Sketch(origin::kPlaneXZ);
            const std::vector<ObjectId> lines =
                b.Polygon(shank, {{N(0), l, false, true}, {Half(w), l, false, true}, {Half(w), N(0)}, {N(0), N(0)}});
            Builder::MarkAxis(shank, lines.back());
            b.Revolve(shank, lines.back());
            SketchFeature& eye = b.Sketch(origin::kPlaneXZ);
            const P centre{N(0), Half(c) - w / 4};
            b.CircleAt(eye, centre, c);
            b.CircleAt(eye, centre, c - w);
            // Thinner than the shank: faces tangent to it would make the union invalid.
            b.Extrude(eye, w * 0.8, BooleanOp::Join, false, ExtentType::Symmetric);
            SketchFeature& split = b.Sketch(origin::kPlaneXZ);
            b.Slot(split, w * 0.1, {N(0), l + N(1), false, true}, {N(0), w / 2, false, true});
            b.Extrude(split, w * 2, BooleanOp::Cut, false, ExtentType::Symmetric);
            made.envelope = {r.eye, r.wire, q.length + r.eye - r.wire / 4};
            Finish(doc, "Шплинт", designation);
            break;
        }
    }
    const std::string appearance = q.appearance.empty() ? std::string(DefaultAppearance(s->kind)) : q.appearance;
    if (const Appearance* a = ::bz::doc::FindStandard(appearance); a && b.FirstBody()) doc.look.SetBody(b.FirstBody(), a);
    return made;
}

} // namespace bz::doc::parts

// ===========================================================================
// Fitting into a hole of an assembly (AutoDrop).
// ===========================================================================

namespace bz::doc::parts {

double ThreadPitch(double d) { return Pitch(d); }

std::string SizeForHole(const StandardInfo& standard, double diameter) {
    if (IsPackKind(standard.kind)) return {};   // pack parts are not put into holes
    std::string best;
    double bestD = 0;
    for (const std::string& size : Sizes(standard)) {
        const std::optional<double> d = SizeNumber(size);
        if (!d) continue;
        // Passes through, and the hole is not much wider than a clearance
        // hole (ISO 273 coarse: 1.1·d + 1 at most) — then it is meant for a
        // bigger part.
        if (*d > diameter + 0.05 || diameter > 1.3 * *d + 1.2) continue;
        if (*d > bestD) bestD = *d, best = size;
    }
    return best;
}

double LengthFor(const StandardInfo& standard, std::string_view size, double needed) {
    std::vector<double> lengths = Lengths(standard, size);
    std::ranges::sort(lengths);
    for (double l : lengths)
        if (l >= needed - 1e-6) return l;
    return 0;
}

double HeightOf(const StandardInfo& standard, std::string_view size) {
    const std::optional<double> d = SizeNumber(size);
    if (!d) return 0;
    switch (standard.kind) {
        case PartKind::HexNut:
        case PartKind::LockNut:
            if (const NutRow* r = FindRow(NutRows(standard.id), *d)) return standard.kind == PartKind::LockNut ? r->h : r->m;
            break;
        case PartKind::FlatWasher:
        case PartKind::SpringWasher:
            if (const WasherRow* r = FindRow(WasherRows(standard.id), *d)) return r->h;
            break;
        default: break;
    }
    return 0;
}

std::optional<Seat> SeatOf(const Request& request) {
    const StandardInfo* s = FindStandard(request.standard);
    if (!s) return std::nullopt;
    const std::optional<double> d = SizeNumber(request.size);
    if (!d) return std::nullopt;
    const Vec3 down{0, 0, -1};
    switch (s->kind) {
        case PartKind::HexBolt:
        case PartKind::SocketScrew:
        case PartKind::PanScrew:   // under the head, around the shank
            return Seat{{0, 0, 0}, down, *d / 2};
        case PartKind::HexNut:
        case PartKind::LockNut:    // the underside, around the threaded hole
            if (!FindRow(NutRows(s->id), *d)) return std::nullopt;
            return Seat{{0, 0, 0}, down, (*d - 1.0825 * Pitch(*d)) / 2};
        case PartKind::FlatWasher:
        case PartKind::SpringWasher:
            if (const WasherRow* r = FindRow(WasherRows(s->id), *d)) return Seat{{0, 0, 0}, down, r->inner / 2};
            return std::nullopt;
        default: return std::nullopt;
    }
}

std::optional<Seat> TopOf(const Request& request) {
    const StandardInfo* s = FindStandard(request.standard);
    if (!s || (s->kind != PartKind::FlatWasher && s->kind != PartKind::SpringWasher)) return std::nullopt;
    std::optional<Seat> seat = SeatOf(request);
    if (!seat) return std::nullopt;
    seat->centre = {0, 0, HeightOf(*s, request.size)};
    seat->normal = {0, 0, 1};
    return seat;
}

} // namespace bz::doc::parts
