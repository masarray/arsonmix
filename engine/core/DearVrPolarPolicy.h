// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string_view>
namespace armix::core {
inline bool isDearVrPro2(std::string_view name) noexcept {
 char buf[96]{};std::size_t n=0;
 for(char c:name){if(c==' '||c=='-'||c=='_')continue;
   if(n>=95)return false;
   buf[n++]=(c>='A'&&c<='Z')?char(c-'A'+'a'):c;}
 return std::string_view(buf,n).find("dearvrpro2")!=std::string_view::npos;
}
inline bool isCartesianPolarMode(std::string_view name,std::string_view zero,std::string_view one) noexcept {
 return (name.find("mode")!=std::string_view::npos ||
         name.find("coordinate")!=std::string_view::npos ||
         name.find("input")!=std::string_view::npos) &&
        zero.find("cartesian")!=std::string_view::npos &&
        one.find("polar")!=std::string_view::npos;
}
}
