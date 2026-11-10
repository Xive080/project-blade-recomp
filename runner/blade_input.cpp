// Teclado y raton como mando 1 (DualShock). Se activa solo; BLADE_KBM=0 lo desactiva. Con BLADE_INPUT (guion de
// pruebas) no arranca, para no mezclar entradas.
//
//   Stick izquierdo   W A S D (inclinacion completa; con Alt, a medias)
//   Cruceta           flechas
//   Cruz (X)          Espacio, K, clic izquierdo
//   Cuadrado          J, clic derecho
//   Circulo           L, clic central
//   Triangulo         I, Esc, Retroceso (atras en los menus)
//   L1 / R1           Q / E, rueda del raton arriba / abajo
//   L2 / R2           1 / 3, Mayus izquierda / Mayus derecha
//   L3 / R3           Z / C
//   Start / Select    Enter / Tab
//   Stick derecho     movimiento del raton (BLADE_MOUSE_GAIN, por defecto 6)
//
// Si hay un mando conectado se sigue leyendo y se suma a lo anterior. Mientras no se toca el teclado ni el raton se
// suelta el estado forzado y el runtime lee el mando como siempre.
// Usa el estado forzado de libpad (setPadOverrideState), igual que blade_devtools.cpp: no toca el runtime.
#include "ps2_runtime.h"

#include "raylib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

namespace ps2_stubs
{
    void setPadOverrideState(uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
    void clearPadOverrideState();
}

namespace
{
    enum : uint16_t
    {
        kSelect = 1u << 0, kL3 = 1u << 1, kR3 = 1u << 2, kStart = 1u << 3,
        kUp = 1u << 4, kRight = 1u << 5, kDown = 1u << 6, kLeft = 1u << 7,
        kL2 = 1u << 8, kR2 = 1u << 9, kL1 = 1u << 10, kR1 = 1u << 11,
        kTriangle = 1u << 12, kCircle = 1u << 13, kCross = 1u << 14, kSquare = 1u << 15,
    };

    uint8_t toByte(float v)
    {
        v = std::clamp(v, -1.0f, 1.0f);
        return static_cast<uint8_t>(std::lround(127.5f + v * 127.5f));
    }

    struct Frame
    {
        uint16_t mask = 0; // 1 = pulsado
        float lx = 0, ly = 0, rx = 0, ry = 0;
        bool keyboardOrMouse = false;
    };

    void readKeyboard(Frame &f)
    {
        static const struct { int key; uint16_t bit; } kKeys[] = {
            {KEY_UP, kUp}, {KEY_DOWN, kDown}, {KEY_LEFT, kLeft}, {KEY_RIGHT, kRight},
            {KEY_SPACE, kCross}, {KEY_K, kCross}, {KEY_J, kSquare}, {KEY_L, kCircle},
            {KEY_I, kTriangle}, {KEY_ESCAPE, kTriangle}, {KEY_BACKSPACE, kTriangle},
            {KEY_Q, kL1}, {KEY_E, kR1}, {KEY_ONE, kL2}, {KEY_THREE, kR2},
            {KEY_LEFT_SHIFT, kL2}, {KEY_RIGHT_SHIFT, kR2}, {KEY_Z, kL3}, {KEY_C, kR3},
            {KEY_ENTER, kStart}, {KEY_KP_ENTER, kStart}, {KEY_TAB, kSelect},
        };
        for (const auto &k : kKeys)
            if (IsKeyDown(k.key))
                f.mask |= k.bit;

        float ax = 0, ay = 0;
        if (IsKeyDown(KEY_D)) ax += 1;
        if (IsKeyDown(KEY_A)) ax -= 1;
        if (IsKeyDown(KEY_S)) ay += 1;
        if (IsKeyDown(KEY_W)) ay -= 1;
        if (ax != 0 && ay != 0) { ax *= 0.7071f; ay *= 0.7071f; }
        if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) { ax *= 0.5f; ay *= 0.5f; }
        f.lx += ax;
        f.ly += ay;
        f.keyboardOrMouse = f.mask != 0 || ax != 0 || ay != 0;
    }

    void readMouse(Frame &f, float gain, float &smx, float &smy, int &wheelHold, uint16_t &wheelBit)
    {
        uint16_t m = 0;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) m |= kCross;
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) m |= kSquare;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) m |= kCircle;

        // la rueda da un pulso: se mantiene L1/R1 unos ciclos para que el juego lo vea en algun VSync
        const float wheel = GetMouseWheelMove();
        if (wheel > 0) { wheelBit = kL1; wheelHold = 30; }
        else if (wheel < 0) { wheelBit = kR1; wheelHold = 30; }
        if (wheelHold > 0) { m |= wheelBit; --wheelHold; }

        const Vector2 d = GetMouseDelta();
        const float tx = d.x * gain / 100.0f, ty = d.y * gain / 100.0f;
        smx = smx * 0.8f + tx * 0.2f * 5.0f;
        smy = smy * 0.8f + ty * 0.2f * 5.0f;
        if (std::fabs(smx) < 0.02f) smx = 0;
        if (std::fabs(smy) < 0.02f) smy = 0;
        f.rx += smx;
        f.ry += smy;
        f.mask |= m;
        f.keyboardOrMouse = f.keyboardOrMouse || m != 0 || smx != 0 || smy != 0;
    }

    void readGamepad(Frame &f)
    {
        for (int g = 0; g < 4; ++g)
        {
            if (!IsGamepadAvailable(g))
                continue;
            static const struct { int b; uint16_t bit; } kBtn[] = {
                {GAMEPAD_BUTTON_LEFT_FACE_UP, kUp}, {GAMEPAD_BUTTON_LEFT_FACE_DOWN, kDown},
                {GAMEPAD_BUTTON_LEFT_FACE_LEFT, kLeft}, {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, kRight},
                {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, kCross}, {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, kCircle},
                {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, kSquare}, {GAMEPAD_BUTTON_RIGHT_FACE_UP, kTriangle},
                {GAMEPAD_BUTTON_LEFT_TRIGGER_1, kL1}, {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, kR1},
                {GAMEPAD_BUTTON_LEFT_TRIGGER_2, kL2}, {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, kR2},
                {GAMEPAD_BUTTON_LEFT_THUMB, kL3}, {GAMEPAD_BUTTON_RIGHT_THUMB, kR3},
                {GAMEPAD_BUTTON_MIDDLE_RIGHT, kStart}, {GAMEPAD_BUTTON_MIDDLE_LEFT, kSelect},
            };
            for (const auto &b : kBtn)
                if (IsGamepadButtonDown(g, b.b))
                    f.mask |= b.bit;
            f.lx += GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_X);
            f.ly += GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_Y);
            f.rx += GetGamepadAxisMovement(g, GAMEPAD_AXIS_RIGHT_X);
            f.ry += GetGamepadAxisMovement(g, GAMEPAD_AXIS_RIGHT_Y);
            return;
        }
    }

    void inputLoop(float gain)
    {
        bool overrideOn = false;
        float smx = 0, smy = 0;
        int wheelHold = 0;
        uint16_t wheelBit = 0;
        bool exitKeyOff = false;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            if (IsWindowReady() && !exitKeyOff)
            {
                SetExitKey(KEY_NULL); // raylib cierra la ventana con Esc por defecto; aqui Esc es "atras"
                exitKeyOff = true;
            }
            if (!IsWindowReady() || !IsWindowFocused())
            {
                if (overrideOn) { ps2_stubs::clearPadOverrideState(); overrideOn = false; }
                continue;
            }
            Frame f;
            readKeyboard(f);
            readMouse(f, gain, smx, smy, wheelHold, wheelBit);
            if (!f.keyboardOrMouse)
            {
                if (overrideOn) { ps2_stubs::clearPadOverrideState(); overrideOn = false; }
                continue;
            }
            readGamepad(f);
            ps2_stubs::setPadOverrideState(static_cast<uint16_t>(~f.mask), toByte(f.lx), toByte(f.ly), toByte(f.rx),
                                           toByte(f.ry));
            overrideOn = true;
        }
    }
}

void bladeStartKeyboardMouse(PS2Runtime & /*runtime*/)
{
    if (const char *off = std::getenv("BLADE_KBM"); off && std::string_view(off) == "0")
        return;
    if (const char *script = std::getenv("BLADE_INPUT"); script && *script)
        return;
    float gain = 6.0f;
    if (const char *g = std::getenv("BLADE_MOUSE_GAIN"); g && *g)
        gain = std::max(0.0f, static_cast<float>(std::atof(g)));
    std::thread(inputLoop, gain).detach();
    std::cout << "[blade] teclado y raton activos (WASD stick, Espacio X, J cuadrado, L circulo, I triangulo, "
                 "Enter Start; raton: clic X/cuadrado/circulo, movimiento = stick derecho)" << std::endl;
}
