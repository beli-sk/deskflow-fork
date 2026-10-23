/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EiKeyStateTests.h"

#include "base/EventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/KeyTypes.h"
#include "platform/EiKeyState.h"
#include "platform/XDGKeyUtil.h"

#include <QByteArray>
#include <QTemporaryFile>

#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace {
class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

const char TestKeymap[] = R"XKB(xkb_keymap {
xkb_keycodes "test" {
    minimum = 8;
    maximum = 255;
    <LFSH> = 50;
    <NMLK> = 77;
};
xkb_types "test" {
    type "ONE_LEVEL" {
        modifiers = none;
        level_name[Level1] = "Any";
    };
};
xkb_compat "test" {
    interpret Shift_L+AnyOfOrNone(all) {
        action = SetMods(modifiers=Shift);
    };
    interpret Num_Lock+AnyOfOrNone(all) {
        action = LockMods(modifiers=Mod2);
    };
};
xkb_symbols "test" {
    key <LFSH> { [ Shift_L ] };
    key <NMLK> { [ Num_Lock ] };
    modifier_map Shift { <LFSH> };
    modifier_map Mod2 { <NMLK> };
};
};)XKB";

// XKB keycodes for TestKeymap.
constexpr std::uint32_t LeftShiftKeycode = 50;
constexpr std::uint32_t NumLockKeycode = 77;

// A keymap where the Menu key (xkb keycode 135, evdev 127) is remapped to
// the compose key, as the xkb option "compose:menu" would do.
const char ComposeKeymap[] = R"XKB(xkb_keymap {
xkb_keycodes "test" {
    minimum = 8;
    maximum = 255;
    <MENU> = 135;
    <AB01> = 38;
};
xkb_types "test" {
    type "ONE_LEVEL" {
        modifiers = none;
        level_name[Level1] = "Any";
    };
};
xkb_compat "test" {
};
xkb_symbols "test" {
    key <MENU> { [ Multi_key ] };
    key <AB01> { [ q ] };
};
};)XKB";

constexpr std::uint32_t MenuKeycode = 135;

// A keymap where the right Alt key (xkb keycode 108, evdev 100) is remapped
// to the compose key, as the xkb option "compose:ralt" would do, together
// with a left Alt key that generates the (virtual) Alt modifier.
const char RAltComposeKeymap[] = R"XKB(xkb_keymap {
xkb_keycodes "test" {
    minimum = 8;
    maximum = 255;
    <LALT> = 64;
    <RALT> = 108;
    <AB01> = 38;
};
xkb_types "test" {
    type "ONE_LEVEL" {
        modifiers = none;
        level_name[Level1] = "Any";
    };
};
xkb_compat "test" {
    virtual_modifiers Alt;
    interpret Alt_L+Any {
        virtualModifier = Alt;
        action = SetMods(modifiers=modMapMods);
    };
};
xkb_symbols "test" {
    key <LALT> { [ Alt_L ] };
    key <RALT> { [ Multi_key ] };
    key <AB01> { [ q ] };
    modifier_map Mod1 { <LALT> };
};
};)XKB";

constexpr std::uint32_t LeftAltKeycode = 64;
constexpr std::uint32_t RAltKeycode = 108;
constexpr std::uint32_t AB01Keycode = 38;

// The alias KeyID of the compose key on keycode is the stock keysym of that
// keycode, which depends on the default layout (ISO_Level3_Shift on AltGr
// layouts, Alt_R otherwise).
KeyID composeAliasID(std::uint32_t keycode)
{
  auto *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (ctx == nullptr) {
    return kKeyNone;
  }
  xkb_rule_names names{};
  names.options = "";
  auto *stock = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (stock == nullptr) {
    xkb_context_unref(ctx);
    return kKeyNone;
  }
  const xkb_keysym_t *stockSyms;
  const auto id = xkb_keymap_key_get_syms_by_level(stock, keycode, 0, 0, &stockSyms) > 0
                      ? XDGKeyUtil::mapKeySymToKeyID(stockSyms[0])
                      : kKeyNone;
  xkb_keymap_unref(stock);
  xkb_context_unref(ctx);
  return id;
}

// An EiKeyState that records the buttons injected via fakeKey() instead of
// forwarding them to a computer.
class RecordingEiKeyState : public deskflow::EiKeyState
{
public:
  using deskflow::EiKeyState::EiKeyState;

  std::vector<std::pair<KeyButton, bool>> fakeKeys;

protected:
  void fakeKey(const deskflow::KeyMap::Keystroke &keystroke) override
  {
    fakeKeys.emplace_back(keystroke.m_data.m_button.m_button, keystroke.m_data.m_button.m_press);
  }
};
} // namespace

void EiKeyStateTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void EiKeyStateTests::clearStaleModifiers_shiftDownAndNumLockOn_shiftClearedAndNumLockPreserved()
{
  TestAppUtil appUtil;
  EventQueue eventQueue;
  deskflow::EiKeyState keyState(nullptr, &eventQueue);

  QTemporaryFile keymapFile;
  QVERIFY(keymapFile.open());
  const QByteArray keymapData = QByteArray::fromRawData(TestKeymap, sizeof(TestKeymap) - 1);
  QCOMPARE(keymapFile.write(keymapData), keymapData.size());
  QVERIFY(keymapFile.flush());
  keyState.init(keymapFile.handle(), keymapFile.size());

  keyState.updateXkbState(LeftShiftKeycode, true);
  keyState.updateXkbState(NumLockKeycode, true);
  keyState.updateXkbState(NumLockKeycode, false);

  QVERIFY((keyState.pollActiveModifiers() & KeyModifierShift) != 0);
  QVERIFY((keyState.pollActiveModifiers() & KeyModifierNumLock) != 0);

  keyState.clearStaleModifiers();

  QVERIFY((keyState.pollActiveModifiers() & KeyModifierShift) == 0);
  QVERIFY((keyState.pollActiveModifiers() & KeyModifierNumLock) != 0);
}

void EiKeyStateTests::updateLockedModifiers_compositorLockState_numLockFollowsCompositor()
{
  // Real modifier Mod2 is index 4 (Shift, Lock, Control, Mod1, Mod2, ...).
  constexpr std::uint32_t xkbMod2Mask = 1 << 4;

  TestAppUtil appUtil;
  EventQueue eventQueue;
  deskflow::EiKeyState keyState(nullptr, &eventQueue);

  QTemporaryFile keymapFile;
  QVERIFY(keymapFile.open());
  const QByteArray keymapData = QByteArray::fromRawData(TestKeymap, sizeof(TestKeymap) - 1);
  QCOMPARE(keymapFile.write(keymapData), keymapData.size());
  QVERIFY(keymapFile.flush());
  keyState.init(keymapFile.handle(), keymapFile.size());

  keyState.updateXkbState(LeftShiftKeycode, true);
  QVERIFY((keyState.pollActiveModifiers() & KeyModifierNumLock) == 0);

  // NumLock toggled on locally, reported by the compositor
  keyState.updateLockedModifiers(xkbMod2Mask);
  QVERIFY((keyState.pollActiveModifiers() & KeyModifierNumLock) != 0);
  QVERIFY((keyState.pollActiveModifiers() & KeyModifierShift) != 0);

  keyState.clearStaleModifiers();
  QVERIFY((keyState.pollActiveModifiers() & KeyModifierNumLock) != 0);

  // NumLock toggled off locally
  keyState.updateLockedModifiers(0);
  QVERIFY((keyState.pollActiveModifiers() & KeyModifierNumLock) == 0);
}

void EiKeyStateTests::getKeyMap_composeKeyOnMenu_aliasesMenuKey()
{
  TestAppUtil appUtil;
  EventQueue eventQueue;
  RecordingEiKeyState keyState(nullptr, &eventQueue);

  QTemporaryFile keymapFile;
  QVERIFY(keymapFile.open());
  const QByteArray keymapData = QByteArray::fromRawData(ComposeKeymap, sizeof(ComposeKeymap) - 1);
  QCOMPARE(keymapFile.write(keymapData), keymapData.size());
  QVERIFY(keymapFile.flush());
  keyState.init(keymapFile.handle(), keymapFile.size());

  keyState.updateKeyMap();

  // The server (e.g. Windows) sends Menu; the alias entry must map it to the
  // same button as the compose key.
  keyState.fakeKeyDown(kKeyMenu, 0, 0, "en");

  QVERIFY2(keyState.fakeKeys.size() == 1, "expected exactly one injected key press");
  QCOMPARE(keyState.fakeKeys[0].first, static_cast<KeyButton>(MenuKeycode - 8));
  QCOMPARE(keyState.fakeKeys[0].second, true);
}

void EiKeyStateTests::getKeyMap_composeKeyOnRAlt_doesNotSyncAltModifier()
{
  TestAppUtil appUtil;
  EventQueue eventQueue;
  RecordingEiKeyState keyState(nullptr, &eventQueue);

  QTemporaryFile keymapFile;
  QVERIFY(keymapFile.open());
  const QByteArray keymapData = QByteArray::fromRawData(RAltComposeKeymap, sizeof(RAltComposeKeymap) - 1);
  QCOMPARE(keymapFile.write(keymapData), keymapData.size());
  QVERIFY(keymapFile.flush());
  keyState.init(keymapFile.handle(), keymapFile.size());

  keyState.updateKeyMap();

  const auto aliasID = composeAliasID(RAltKeycode);
  QVERIFY2(aliasID != kKeyNone && aliasID != kKeyCompose, "expected a usable alias for the compose key");

  // The Windows server sends RAlt as AltGr with the Alt modifier held (its
  // mask includes VK_RMENU as Alt).  The client must inject only the compose
  // key press; it must not synthesize a phantom Alt press to match the
  // server's mask, because apps like Firefox refuse to start a compose
  // sequence while Alt is held.
  keyState.fakeKeyDown(aliasID, KeyModifierAlt, 0, "en");

  QVERIFY2(keyState.fakeKeys.size() == 1, "expected exactly one injected key press");
  QCOMPARE(keyState.fakeKeys[0].first, static_cast<KeyButton>(RAltKeycode - 8));
  QCOMPARE(keyState.fakeKeys[0].second, true);
  QVERIFY2(keyState.fakeKeys[0].first != static_cast<KeyButton>(LeftAltKeycode - 8), "phantom Alt must not be pressed");
}

void EiKeyStateTests::getKeyMap_composeKeyHeld_altMaskNotSyncedForOtherKeys()
{
  TestAppUtil appUtil;
  EventQueue eventQueue;
  RecordingEiKeyState keyState(nullptr, &eventQueue);

  QTemporaryFile keymapFile;
  QVERIFY(keymapFile.open());
  const QByteArray keymapData = QByteArray::fromRawData(RAltComposeKeymap, sizeof(RAltComposeKeymap) - 1);
  QCOMPARE(keymapFile.write(keymapData), keymapData.size());
  QVERIFY(keymapFile.flush());
  keyState.init(keymapFile.handle(), keymapFile.size());

  keyState.updateKeyMap();

  const auto aliasID = composeAliasID(RAltKeycode);
  QVERIFY2(aliasID != kKeyNone && aliasID != kKeyCompose, "expected a usable alias for the compose key");

  // The Windows server sends RAlt as AltGr with the Alt modifier held.
  keyState.fakeKeyDown(aliasID, KeyModifierAlt, 1, "en");

  // When typing fast, the next key of the compose sequence can be pressed
  // while RAlt is still held on the server, so it arrives with Alt in the
  // mask.  The client must not synthesize a phantom Alt press for it either.
  keyState.fakeKeyDown('q', KeyModifierAlt, 2, "en");

  QVERIFY2(keyState.fakeKeys.size() == 2, "expected exactly two injected key presses");
  QCOMPARE(keyState.fakeKeys[0].first, static_cast<KeyButton>(RAltKeycode - 8));
  QCOMPARE(keyState.fakeKeys[0].second, true);
  QCOMPARE(keyState.fakeKeys[1].first, static_cast<KeyButton>(AB01Keycode - 8));
  QCOMPARE(keyState.fakeKeys[1].second, true);
  QVERIFY2(
      keyState.fakeKeys[1].first != static_cast<KeyButton>(LeftAltKeycode - 8),
      "phantom Alt must not be pressed for keys typed while the compose key is held"
  );
}

QTEST_MAIN(EiKeyStateTests)
