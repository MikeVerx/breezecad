#include "app/features/ExtrudeDirection.h"

#include <doctest.h>

using bz::app::ExtrudeAutoDirection;
using bz::doc::BooleanOp;

// 0.2.5: a cut was started pointing away from the body (the extrude's
// default, up the sketch's normal) and cut nothing until the direction was
// turned by hand. Switching to «Вырез» now turns it into the material, and
// switching back undoes that — unless the user chose the direction himself.
TEST_CASE("выдавливание: «Вырез» разворачивает направление внутрь тела, «Добавить» — обратно") {
    SUBCASE("материал под эскизом (эскиз на верхней грани): вырез идёт вниз и назад вверх") {
        ExtrudeAutoDirection a;
        bool reversed = a.OnOperation(BooleanOp::Join, BooleanOp::Cut, false, -1);
        CHECK(reversed);
        reversed = a.OnOperation(BooleanOp::Cut, BooleanOp::Join, reversed, -1);
        CHECK_FALSE(reversed);
    }
    SUBCASE("материал над эскизом: направление уже внутрь, ничего не меняется") {
        ExtrudeAutoDirection a;
        bool reversed = a.OnOperation(BooleanOp::Join, BooleanOp::Cut, false, +1);
        CHECK_FALSE(reversed);
        reversed = a.OnOperation(BooleanOp::Cut, BooleanOp::Join, reversed, +1);
        CHECK_FALSE(reversed);
    }
    SUBCASE("материал с обеих сторон или не найден: не трогаем") {
        ExtrudeAutoDirection a;
        CHECK(a.OnOperation(BooleanOp::Join, BooleanOp::Cut, true, 0));
        CHECK_FALSE(a.OnOperation(BooleanOp::Join, BooleanOp::Cut, false, 0));
    }
    SUBCASE("пользователь сам выбрал направление до выреза: не разворачиваем") {
        ExtrudeAutoDirection a;
        a.UserChanged();
        CHECK_FALSE(a.OnOperation(BooleanOp::Join, BooleanOp::Cut, false, -1));
    }
    SUBCASE("пользователь развернул после авторазворота: обратно на «Добавить» не возвращаем") {
        ExtrudeAutoDirection a;
        bool reversed = a.OnOperation(BooleanOp::Join, BooleanOp::Cut, false, -1);
        REQUIRE(reversed);
        a.UserChanged();
        reversed = false;   // he turned it up again himself
        CHECK_FALSE(a.OnOperation(BooleanOp::Cut, BooleanOp::Join, reversed, -1));
    }
    SUBCASE("«Новое тело» после выреза тоже возвращает направление") {
        ExtrudeAutoDirection a;
        bool reversed = a.OnOperation(BooleanOp::Join, BooleanOp::Cut, false, -1);
        CHECK_FALSE(a.OnOperation(BooleanOp::Cut, BooleanOp::NewBody, reversed, -1));
    }
    SUBCASE("переключение между не-вырезами направление не трогает") {
        ExtrudeAutoDirection a;
        CHECK_FALSE(a.OnOperation(BooleanOp::Join, BooleanOp::NewBody, false, -1));
        CHECK(a.OnOperation(BooleanOp::NewBody, BooleanOp::Join, true, +1));
    }
    SUBCASE("вырез → «Добавить» → снова вырез: снова внутрь") {
        ExtrudeAutoDirection a;
        bool reversed = a.OnOperation(BooleanOp::Join, BooleanOp::Cut, false, -1);
        reversed = a.OnOperation(BooleanOp::Cut, BooleanOp::Join, reversed, -1);
        reversed = a.OnOperation(BooleanOp::Join, BooleanOp::Cut, reversed, -1);
        CHECK(reversed);
    }
}
