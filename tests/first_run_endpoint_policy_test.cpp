// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/FirstRunEndpointPolicy.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
using P = armix::device::FirstRunEndpointPolicy;
using E = P::Endpoint;
static E ep (const wchar_t* id, const wchar_t* name, P::Direction direction,
             P::Kind kind, bool current = false, bool historical = false)
{
    E e;
    e.id = id; e.name = name; e.direction = direction; e.kind = kind;
    e.active = true; e.verifiedIdentity = true; e.uniqueNativeName = true;
    e.stereo = true; e.currentDefaultRender = current;
    e.preInstallDefaultRender = historical;
    return e;
}
static void expect (bool good, const char* why)
{
    if (!good) throw std::runtime_error (why);
}
int main()
{
    try
    {
        const auto physical = ep (L"{real-01}", L"Speakers Realtek",
            P::Direction::render, P::Kind::physical, true);
        const auto cable = ep (L"{cable-01}", L"CABLE Output (VB-Audio)",
            P::Direction::capture, P::Kind::standardVbCable);
        const auto virtualRender = ep (L"{vb-play}", L"CABLE Input (VB-Audio)",
            P::Direction::render, P::Kind::otherVirtual, true);
        std::vector<E> inventory {physical, cable};
        auto d = P::choose (inventory, true);
        expect (d.outcome == P::Outcome::preserveSavedProfile &&
                !d.mayAttemptApply(), "saved ASIO must be untouchable");

        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::cableToSpeakers &&
                d.captureId == cable.id && d.renderId == physical.id &&
                d.mayAttemptApply(), "verified first-run cable to physical");

        inventory = {virtualRender, physical, cable};
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::cableToSpeakers &&
                d.renderId == physical.id, "never choose CABLE Input as speaker");

        inventory = {physical};
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::playbackOnly && !d.hasCapture() &&
                d.hasPlayback(), "driver absent: Player still has physical output");

        inventory = {virtualRender, cable};
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::noVerifiedPlayback &&
                !d.mayAttemptApply(), "virtual output only: fail closed");

        auto secondPhysical = ep (L"{real-02}", L"USB Headphones",
            P::Direction::render, P::Kind::physical);
        inventory = {physical, secondPhysical, cable};
        inventory[0].currentDefaultRender = false;
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::chooseSpeakers,
                "two physical outputs with no default require selection");
        inventory[1].preInstallDefaultRender = true;
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::cableToSpeakers &&
                d.renderId == secondPhysical.id,
                "historical physical speaker preferred over changed default");
        inventory[0].currentDefaultRender = true;
        d = P::choose (inventory, false);
        expect (d.renderId == secondPhysical.id,
                "preinstall physical default outranks new current default");

        inventory = {physical, cable, cable};
        inventory[2].id = L"{cable-02}";
        inventory[2].name = L"CABLE Output 2";
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::chooseCapture &&
                !d.hasCapture() && !d.mayAttemptApply(),
                "multiple verified capture cables cannot be guessed");

        inventory = {physical, cable};
        inventory[1].id = physical.id;
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::invalidInventory,
                "duplicate native IDs rejected");
        inventory[1].id = L"";
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::invalidInventory,
                "blank native ID rejected");

        inventory = {physical, cable};
        inventory[1].verifiedIdentity = false;
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::playbackOnly && !d.hasCapture(),
                "unverified VB-CABLE identity cannot be authorized");
        inventory[1].verifiedIdentity = true;
        inventory[1].uniqueNativeName = false;
        d = P::choose (inventory, false);
        expect (!d.hasCapture(), "ambiguous name cannot open through JUCE");

        inventory = {physical, cable};
        inventory[0].active = false;
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::noVerifiedPlayback,
                "unplugged speaker never accepted");
        inventory[0].active = true;
        inventory[0].stereo = false;
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::noVerifiedPlayback,
                "mono output cannot claim MAIN stereo");

        inventory = {physical, cable};
        inventory[1].kind = P::Kind::unknown;
        d = P::choose (inventory, false);
        expect (d.outcome == P::Outcome::playbackOnly && !d.hasCapture(),
                "name alone never certifies vendor identity");

        std::cout << "FirstRunEndpointPolicy 15 deterministic safety scenarios PASS\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FirstRunEndpointPolicy FAIL: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
