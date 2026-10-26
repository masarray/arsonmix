// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AsioStartupPolicy.h"
#include <array>
#include <cstdlib>
#include <string_view>

int main()
{
    using P=armix::device::AsioStartupPolicy;
    constexpr std::array<std::string_view,1> i={"Pro Interface"};
    constexpr std::array<std::string_view,1> o={"Pro Interface"};
    constexpr std::array<std::string_view,1> none={""};
    constexpr std::array<std::string_view,1> other={"ASIO4ALL"};
    constexpr std::array<std::string_view,2> multiple={"Pro Interface","ASIO4ALL"};
    const std::array<std::string_view,0> empty={};
    if(!P::preferAsio(i,o))std::abort();
    if(P::preferAsio(i,other))std::abort();
    if(P::preferAsio(i,multiple))std::abort();
    if(P::preferAsio(multiple,i))std::abort();
    if(P::preferAsio(empty,o))std::abort();
    if(P::preferAsio(i,empty))std::abort();
    if(P::preferAsio(none,none))std::abort();
    return 0;
}
